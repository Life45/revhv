#include "pe.hpp"
#include "hypercall.h"
#include "utils.hpp"
#include <filesystem>
#include <type_traits>
#include "logger.hpp"

/// @brief Read function that uses hypervisor hypercalls to read guest kernel memory.
static bool read_via_hypercall(const void* source, void* dest, size_t size)
{
	if (!source || !dest || size == 0)
		return false;
	return hv::hypercall::read_vmemory(reinterpret_cast<uint64_t>(source), dest, size) == size;
}

/// @brief Read function that does a plain memcpy (for local / memory-mapped buffers).
static bool read_via_memcpy(const void* source, void* dest, size_t size)
{
	if (!source || !dest || size == 0)
		return false;
	std::memcpy(dest, source, size);
	return true;
}

/// @brief Helper: read a typed value through the read function.
template <typename T>
static std::remove_const_t<T> read_typed(const pe::read_fn& read, const T* address)
{
	using value_type = std::remove_const_t<T>;
	value_type value{};
	if (!read(address, &value, sizeof(value_type)))
		throw std::runtime_error("Failed to read memory");
	return value;
}

//
// Constructors / destructor / move
//

pe::pe(const uint8_t* base, size_t size) : m_base(base), m_size(size), m_read(read_via_hypercall)
{
}

pe::pe(const uint8_t* base, size_t size, read_fn read) : m_base(base), m_size(size), m_read(std::move(read))
{
}

pe::~pe()
{
	if (m_owns_mapping)
	{
		if (m_mapping_base)
			UnmapViewOfFile(m_mapping_base);
		if (m_mapping_handle && m_mapping_handle != INVALID_HANDLE_VALUE)
			CloseHandle(m_mapping_handle);
	}
}

pe::pe(pe&& other) noexcept : m_base(other.m_base), m_size(other.m_size), m_parsed(other.m_parsed), m_valid(other.m_valid), m_owns_mapping(other.m_owns_mapping), m_mapping_base(other.m_mapping_base), m_mapping_handle(other.m_mapping_handle), m_sections(std::move(other.m_sections)), m_symbols(std::move(other.m_symbols)), m_read(std::move(other.m_read))
{
	other.m_owns_mapping = false;
	other.m_mapping_base = nullptr;
	other.m_mapping_handle = nullptr;
	other.m_base = nullptr;
	other.m_size = 0;
}

pe& pe::operator=(pe&& other) noexcept
{
	if (this != &other)
	{
		if (m_owns_mapping)
		{
			if (m_mapping_base)
				UnmapViewOfFile(m_mapping_base);
			if (m_mapping_handle && m_mapping_handle != INVALID_HANDLE_VALUE)
				CloseHandle(m_mapping_handle);
		}

		m_base = other.m_base;
		m_size = other.m_size;
		m_parsed = other.m_parsed;
		m_valid = other.m_valid;
		m_owns_mapping = other.m_owns_mapping;
		m_mapping_base = other.m_mapping_base;
		m_mapping_handle = other.m_mapping_handle;
		m_sections = std::move(other.m_sections);
		m_symbols = std::move(other.m_symbols);
		m_read = std::move(other.m_read);

		other.m_owns_mapping = false;
		other.m_mapping_base = nullptr;
		other.m_mapping_handle = nullptr;
		other.m_base = nullptr;
		other.m_size = 0;
	}
	return *this;
}

std::optional<pe> pe::from_file(const std::string& file_path, uint64_t image_base)
{
	HANDLE file_handle = nullptr;
	void* base = nullptr;
	size_t file_size = 0;

	if (!utils::map_file(file_path, file_handle, base, file_size))
		return std::nullopt;

	const uint8_t* mapped_view = static_cast<const uint8_t*>(base);

	struct section_mapping
	{
		uint32_t virtual_address;
		uint32_t virtual_size;
		uint32_t raw_offset;
		uint32_t raw_size;
	};

	auto sections = std::make_shared<std::vector<section_mapping>>();
	uint32_t size_of_headers = 0;

	// Quick-parse: read DOS → NT → section table directly from the mapped view.
	if (file_size >= sizeof(IMAGE_DOS_HEADER))
	{
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mapped_view);
		if (dos->e_magic == IMAGE_DOS_SIGNATURE)
		{
			const size_t nt_offset = static_cast<size_t>(dos->e_lfanew);
			if (nt_offset + sizeof(IMAGE_NT_HEADERS) <= file_size)
			{
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(mapped_view + nt_offset);
				if (nt->Signature == IMAGE_NT_SIGNATURE)
				{
					size_of_headers = nt->OptionalHeader.SizeOfHeaders;
					const size_t section_table = nt_offset + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) + nt->FileHeader.SizeOfOptionalHeader;
					const size_t section_count = nt->FileHeader.NumberOfSections;

					for (size_t i = 0; i < section_count; ++i)
					{
						const size_t entry_off = section_table + i * sizeof(IMAGE_SECTION_HEADER);
						if (entry_off + sizeof(IMAGE_SECTION_HEADER) > file_size)
							break;
						const auto* sh = reinterpret_cast<const IMAGE_SECTION_HEADER*>(mapped_view + entry_off);
						sections->push_back({sh->VirtualAddress, sh->Misc.VirtualSize, sh->PointerToRawData, sh->SizeOfRawData});
					}
				}
			}
		}
	}

	const uint8_t* effective_base = image_base ? reinterpret_cast<const uint8_t*>(image_base) : mapped_view;
	const uint8_t* virtual_base = effective_base;
	const uint32_t hdr_size = size_of_headers;

	// Translates virtual addresses to file offsets
	auto local_read = [mapped_view, virtual_base, sections, hdr_size, file_size](const void* source, void* dest, size_t size) -> bool
	{
		if (!source || !dest || size == 0)
			return false;

		const auto rva = static_cast<size_t>(reinterpret_cast<const uint8_t*>(source) - virtual_base);

		// Header region: RVA == file offset
		if (rva < hdr_size)
		{
			if (rva + size > file_size)
				return false;
			std::memcpy(dest, mapped_view + rva, size);
			return true;
		}

		// Section region: translate RVA → PointerToRawData
		for (const auto& sec : *sections)
		{
			if (rva >= sec.virtual_address && rva < sec.virtual_address + (std::max)(sec.virtual_size, sec.raw_size))
			{
				const size_t offset_in_section = rva - sec.virtual_address;
				const size_t file_offset = sec.raw_offset + offset_in_section;
				if (file_offset + size > file_size)
					return false;
				std::memcpy(dest, mapped_view + file_offset, size);
				return true;
			}
		}

		// Fallback: try raw offset (may be in padding or overlay)
		if (rva + size <= file_size)
		{
			std::memcpy(dest, mapped_view + rva, size);
			return true;
		}

		return false;
	};

	pe result(effective_base, file_size, std::move(local_read));
	result.m_owns_mapping = true;
	result.m_mapping_base = base;
	result.m_mapping_handle = file_handle;
	return result;
}

namespace
{
	struct parsed_headers
	{
		IMAGE_DOS_HEADER dos_header{};
		IMAGE_NT_HEADERS nt_header{};
		size_t section_table_offset = 0;
	};

	struct parsed_section
	{
		std::string name;
		uint64_t va = 0;
		uint64_t rva = 0;
		uint64_t size = 0;
	};

	struct codeview_info
	{
		GUID guid{};
		uint32_t age = 0;
		std::string pdb_path;
		std::string pdb_file_name;
	};

	struct codeview_rsds_header
	{
		uint32_t signature = 0;
		GUID guid{};
		uint32_t age = 0;
	};

	constexpr uint32_t codeview_rsds_signature = 0x53445352;  // "RSDS"

	static bool calculate_section_table_offset(const IMAGE_DOS_HEADER& dos_header, const IMAGE_NT_HEADERS& nt_header, size_t& out_offset)
	{
		const size_t nt_offset = static_cast<size_t>(dos_header.e_lfanew);
		const size_t signature_and_file_header_size = sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER);
		const size_t optional_header_size = static_cast<size_t>(nt_header.FileHeader.SizeOfOptionalHeader);

		if (nt_offset > SIZE_MAX - signature_and_file_header_size)
		{
			return false;
		}

		const size_t after_file_header = nt_offset + signature_and_file_header_size;
		if (after_file_header > SIZE_MAX - optional_header_size)
		{
			return false;
		}

		out_offset = after_file_header + optional_header_size;
		return true;
	}

	static bool validate_and_read_headers(const pe::read_fn& read, const uint8_t* base, size_t size, parsed_headers& out_headers)
	{
		if (base == nullptr || size < sizeof(IMAGE_DOS_HEADER))
		{
			return false;
		}

		out_headers.dos_header = read_typed(read, reinterpret_cast<const IMAGE_DOS_HEADER*>(base));
		if (out_headers.dos_header.e_magic != IMAGE_DOS_SIGNATURE)
		{
			return false;
		}

		if (out_headers.dos_header.e_lfanew < 0)
		{
			return false;
		}

		const size_t nt_offset = static_cast<size_t>(out_headers.dos_header.e_lfanew);
		if (nt_offset > size || size - nt_offset < sizeof(IMAGE_NT_HEADERS))
		{
			return false;
		}

		out_headers.nt_header = read_typed(read, reinterpret_cast<const IMAGE_NT_HEADERS*>(base + nt_offset));
		if (out_headers.nt_header.Signature != IMAGE_NT_SIGNATURE)
		{
			return false;
		}

		if (!calculate_section_table_offset(out_headers.dos_header, out_headers.nt_header, out_headers.section_table_offset))
		{
			return false;
		}

		return true;
	}

	static bool read_sections(const pe::read_fn& read, const uint8_t* base, size_t size, const parsed_headers& headers, std::vector<parsed_section>& out_sections)
	{
		const size_t section_count = static_cast<size_t>(headers.nt_header.FileHeader.NumberOfSections);
		const size_t section_header_size = sizeof(IMAGE_SECTION_HEADER);

		if (headers.section_table_offset > size)
		{
			return false;
		}

		if (section_count > 0)
		{
			const size_t table_bytes = section_count * section_header_size;
			if (section_count != 0 && table_bytes / section_header_size != section_count)
			{
				return false;
			}

			if (headers.section_table_offset > size || table_bytes > size - headers.section_table_offset)
			{
				return false;
			}
		}

		out_sections.clear();
		out_sections.reserve(section_count);

		for (size_t index = 0; index < section_count; ++index)
		{
			const size_t section_offset = headers.section_table_offset + (index * section_header_size);
			auto section_header = read_typed(read, reinterpret_cast<const IMAGE_SECTION_HEADER*>(base + section_offset));

			const char* raw_name = reinterpret_cast<const char*>(section_header.Name);
			size_t name_len = 0;
			while (name_len < IMAGE_SIZEOF_SHORT_NAME && raw_name[name_len] != '\0')
			{
				++name_len;
			}

			const uint64_t section_rva = section_header.VirtualAddress;
			const uint64_t section_va = reinterpret_cast<uint64_t>(base) + section_rva;
			const uint64_t section_size = section_header.Misc.VirtualSize != 0 ? section_header.Misc.VirtualSize : section_header.SizeOfRawData;

			out_sections.push_back({std::string(raw_name, name_len), section_va, section_rva, section_size});
		}

		return true;
	}

	static bool try_extract_codeview_info(const pe::read_fn& read, const uint8_t* base, size_t image_size, const parsed_headers& headers, codeview_info& out_codeview)
	{
		const IMAGE_DATA_DIRECTORY debug_directory = headers.nt_header.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
		if (debug_directory.VirtualAddress == 0 || debug_directory.Size < sizeof(IMAGE_DEBUG_DIRECTORY))
		{
			return false;
		}

		const size_t debug_rva = static_cast<size_t>(debug_directory.VirtualAddress);
		if (debug_rva > image_size || debug_directory.Size > image_size - debug_rva)
		{
			return false;
		}

		const size_t debug_entry_count = static_cast<size_t>(debug_directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY));
		for (size_t index = 0; index < debug_entry_count; ++index)
		{
			const size_t entry_rva = debug_rva + (index * sizeof(IMAGE_DEBUG_DIRECTORY));
			auto debug_entry = read_typed(read, reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + entry_rva));
			if (debug_entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW)
			{
				continue;
			}

			if (debug_entry.AddressOfRawData == 0)
			{
				continue;
			}

			const uint32_t codeview_rva = debug_entry.AddressOfRawData;
			const size_t codeview_size = static_cast<size_t>(debug_entry.SizeOfData);
			if (codeview_rva == 0 || codeview_size < sizeof(codeview_rsds_header))
			{
				continue;
			}

			const size_t codeview_offset = static_cast<size_t>(codeview_rva);
			if (codeview_offset > image_size || codeview_size > image_size - codeview_offset)
			{
				continue;
			}

			std::vector<uint8_t> codeview_data(codeview_size);
			if (!read(base + codeview_offset, codeview_data.data(), codeview_size))
			{
				continue;
			}

			const auto* rsds = reinterpret_cast<const codeview_rsds_header*>(codeview_data.data());
			if (rsds->signature != codeview_rsds_signature)
			{
				continue;
			}

			const char* pdb_path = reinterpret_cast<const char*>(codeview_data.data() + sizeof(codeview_rsds_header));
			const size_t path_buffer_size = codeview_size - sizeof(codeview_rsds_header);

			size_t path_length = 0;
			while (path_length < path_buffer_size && pdb_path[path_length] != '\0')
			{
				++path_length;
			}

			if (path_length == 0 || path_length == path_buffer_size)
			{
				continue;
			}

			const std::string full_pdb_path(pdb_path, path_length);
			const std::string pdb_file_name = std::filesystem::path(full_pdb_path).filename().string();
			if (pdb_file_name.empty())
			{
				continue;
			}

			out_codeview.guid = rsds->guid;
			out_codeview.age = rsds->age;
			out_codeview.pdb_path = full_pdb_path;
			out_codeview.pdb_file_name = pdb_file_name;
			return true;
		}

		return false;
	}

	static std::string build_symbol_id(const GUID& guid, uint32_t age)
	{
		return std::format("{:08X}{:04X}{:04X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:X}", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7], age);
	}

	static std::string build_symbol_server_url(const codeview_info& codeview)
	{
		const std::string symbol_id = build_symbol_id(codeview.guid, codeview.age);
		return std::format("https://msdl.microsoft.com/download/symbols/{}/{}/{}", codeview.pdb_file_name, symbol_id, codeview.pdb_file_name);
	}

	static std::string build_local_symbol_path(const codeview_info& codeview)
	{
		char temp_path[MAX_PATH] = {};
		const DWORD temp_path_size = GetTempPathA(MAX_PATH, temp_path);
		if (temp_path_size == 0 || temp_path_size > MAX_PATH)
		{
			return {};
		}

		const std::filesystem::path local_path = std::filesystem::path(temp_path) / "symbols" / codeview.pdb_file_name / build_symbol_id(codeview.guid, codeview.age) / codeview.pdb_file_name;

		return local_path.string();
	}

	static bool try_download_and_parse_symbols(const codeview_info& codeview, std::vector<pdb::symbol_info>& out_symbols)
	{
		const std::string symbol_url = build_symbol_server_url(codeview);
		const std::string local_symbol_path = build_local_symbol_path(codeview);
		if (local_symbol_path.empty())
		{
			return false;
		}

		if (!std::filesystem::exists(local_symbol_path) && !utils::download_file(symbol_url, local_symbol_path))
		{
			return false;
		}

		std::vector<pdb::symbol_info> symbols;
		if (!pdb::load_symbols(local_symbol_path, symbols))
		{
			return false;
		}

		out_symbols = std::move(symbols);
		return true;
	}
}  // namespace

bool pe::parse()
{
	if (m_parsed)
	{
		return m_valid;
	}

	m_parsed = true;
	m_sections.clear();
	m_symbols.clear();

	try
	{
		parsed_headers headers{};
		if (!validate_and_read_headers(m_read, m_base, m_size, headers))
		{
			logger::error("PE header validation failed");
			return m_valid = false;
		}

		std::vector<parsed_section> parsed_sections;
		if (!read_sections(m_read, m_base, m_size, headers, parsed_sections))
		{
			logger::error("Failed to read PE sections");
			return m_valid = false;
		}

		m_sections.reserve(parsed_sections.size());
		for (const auto& section : parsed_sections)
		{
			m_sections.push_back({section.name, section.va, section.rva, section.size});
		}

		codeview_info codeview{};
		if (try_extract_codeview_info(m_read, m_base, m_size, headers, codeview))
		{
			try_download_and_parse_symbols(codeview, m_symbols);
		}

		if (m_symbols.empty())
		{
			logger::warn("No symbols found/symbol parsing failed for PE file");
		}

		return m_valid = true;
	}
	catch (const std::exception&)
	{
		logger::error("Exception occurred while parsing PE file");
		return m_valid = false;
	}
}