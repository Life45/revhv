#pragma once

#include "includes.h"
#include "pe.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "../common/trace_log.hpp"
#include "../common/module_export.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace trace
{
	class parser
	{
	public:
		/// @brief Parse, merge, format, and write a combined trace log.
		/// Blocks the calling thread until processing is complete.
		/// @param modules_path  Path to the module list binary file (module_export format).
		/// @param trace_dir     Directory containing trace_core_N.bin files.
		/// @param output_path   Destination for the formatted log file.
		/// @return true on success
		bool run(const std::string& modules_path, const std::string& trace_dir, const std::string& output_path)
		{
			// Load the module list
			if (!load_modules(modules_path))
			{
				logger::error("trace parser: failed to load module list from '{}'", modules_path);
				return false;
			}

			logger::info("trace parser: loaded {} modules", m_modules.size());

			// Discover and memory-map per-core trace files
			std::vector<core_stream> streams;
			if (!open_trace_streams(trace_dir, streams))
			{
				logger::error("trace parser: no valid trace files found in '{}'", trace_dir);
				return false;
			}

			logger::info("trace parser: opened {} core streams", streams.size());

			// K-way merge + format + write (PEs loaded lazily)
			if (!merge_format_write(streams, output_path))
			{
				logger::error("trace parser: failed to write output to '{}'", output_path);
				cleanup_streams(streams);
				return false;
			}

			cleanup_streams(streams);
			logger::info("trace parser: output written to '{}'", output_path);
			return true;
		}

	private:
		// Module info loaded from the export file
		struct module_info
		{
			uint64_t base = 0;
			uint64_t size = 0;
			std::string name;
			std::string full_path;
		};

		std::vector<module_info> m_modules;

		// Lazily-populated PE cache (keyed by module name)
		std::unordered_map<std::string, pe> m_pe_cache;
		std::unordered_set<std::string> m_pe_attempted;	 // modules already tried (including failures)
		std::mutex m_pe_mutex;

		// Memory-mapped trace file stream
		struct core_stream
		{
			uint16_t core_id = 0;
			const trace::entry* entries = nullptr;	// points into the mapped view
			size_t entry_count = 0;
			size_t cursor = 0;	// current read position

			void* mapped_base = nullptr;
			HANDLE file_handle = nullptr;
			size_t mapped_size = 0;

			bool done() const { return cursor >= entry_count; }
			const trace::entry& current() const { return entries[cursor]; }
		};

		// priority queue element — index into the streams vector
		struct heap_elem
		{
			uint64_t timestamp;
			size_t stream_index;
			bool operator>(const heap_elem& o) const { return timestamp > o.timestamp; }
		};

		// Load exported module list
		bool load_modules(const std::string& path)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file.is_open())
				return false;

			module_export::file_header hdr{};
			file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
			if (!file || hdr.magic != module_export::file_magic || hdr.version != module_export::file_version)
				return false;

			m_modules.resize(hdr.module_count);
			for (uint32_t i = 0; i < hdr.module_count; ++i)
			{
				module_export::module_entry me{};
				file.read(reinterpret_cast<char*>(&me), sizeof(me));
				if (!file)
					return false;

				auto& m = m_modules[i];
				m.base = me.base;
				m.size = me.size;
				m.name.assign(me.name, strnlen(me.name, module_export::max_name_length));
				m.full_path.assign(me.full_path, strnlen(me.full_path, module_export::max_path_length));
			}

			return true;
		}

		// Lazy PE loading
		pe* get_or_load_pe(const module_info& mod)
		{
			std::lock_guard lock(m_pe_mutex);

			auto it = m_pe_cache.find(mod.name);
			if (it != m_pe_cache.end())
				return &it->second;

			if (m_pe_attempted.contains(mod.name))
				return nullptr;

			m_pe_attempted.insert(mod.name);

			if (mod.full_path.empty() || !std::filesystem::exists(mod.full_path))
				return nullptr;

			logger::info("trace parser: loading PE for module {} from '{}'", mod.name, mod.full_path);
			auto disk_pe = pe::from_file(mod.full_path, mod.base);
			if (!disk_pe || !disk_pe->parse())
				return nullptr;

			auto [ins_it, _] = m_pe_cache.emplace(mod.name, std::move(*disk_pe));
			return &ins_it->second;
		}

		// Open and memory-map per-core trace files
		bool open_trace_streams(const std::string& dir, std::vector<core_stream>& out)
		{
			namespace fs = std::filesystem;
			if (!fs::is_directory(dir))
				return false;

			for (const auto& entry : fs::directory_iterator(dir))
			{
				if (!entry.is_regular_file())
					continue;

				const auto fname = entry.path().filename().string();
				if (fname.find("trace_core_") != 0 || fname.find(".bin") == std::string::npos)
					continue;

				HANDLE fh = nullptr;
				void* base = nullptr;
				size_t file_size = 0;
				if (!utils::map_file(entry.path().string(), fh, base, file_size))
				{
					logger::warn("trace parser: failed to map '{}'", entry.path().string());
					continue;
				}

				if (file_size < sizeof(trace::file_header))
				{
					utils::unmap_file(fh, base);
					continue;
				}

				const auto* hdr = static_cast<const trace::file_header*>(base);
				if (hdr->magic != trace::file_magic || hdr->version != trace::file_version)
				{
					logger::warn("trace parser: invalid header in '{}'", entry.path().string());
					utils::unmap_file(fh, base);
					continue;
				}

				const size_t payload_bytes = file_size - sizeof(trace::file_header);
				const size_t entry_count = payload_bytes / sizeof(trace::entry);

				if (entry_count == 0)
				{
					utils::unmap_file(fh, base);
					continue;
				}

				core_stream cs{};
				cs.core_id = hdr->core_id;
				cs.entries = reinterpret_cast<const trace::entry*>(static_cast<const uint8_t*>(base) + sizeof(trace::file_header));
				cs.entry_count = entry_count;
				cs.cursor = 0;
				cs.mapped_base = base;
				cs.file_handle = fh;
				cs.mapped_size = file_size;
				out.push_back(cs);
			}

			return !out.empty();
		}

		static void cleanup_streams(std::vector<core_stream>& streams)
		{
			for (auto& cs : streams)
			{
				if (cs.mapped_base)
					utils::unmap_file(cs.file_handle, cs.mapped_base);
				cs.mapped_base = nullptr;
				cs.file_handle = nullptr;
			}
		}

		// K-way merge, format, write
		bool merge_format_write(std::vector<core_stream>& streams, const std::string& output_path)
		{
			std::ofstream out(output_path, std::ios::out | std::ios::trunc);
			if (!out.is_open())
				return false;

			// Use a large buffer for better write throughput
			constexpr size_t write_buf_size = 4 * 1024 * 1024;	// 4 MB
			std::vector<char> write_buf(write_buf_size);
			out.rdbuf()->pubsetbuf(write_buf.data(), write_buf_size);

			// Initialise the min-heap with the first entry of each stream
			std::priority_queue<heap_elem, std::vector<heap_elem>, std::greater<>> min_heap;
			for (size_t i = 0; i < streams.size(); ++i)
			{
				if (!streams[i].done())
					min_heap.push({streams[i].current().timestamp, i});
			}

			// Pre-allocate a format buffer to avoid repeated small allocations
			std::string line;
			line.reserve(512);

			uint64_t entries_written = 0;
			const auto report_interval = std::max<uint64_t>(1, 500'000);

			uint64_t total_entries = 0;
			for (const auto& s : streams)
				total_entries += s.entry_count;

			while (!min_heap.empty())
			{
				const auto [ts, si] = min_heap.top();
				min_heap.pop();

				auto& stream = streams[si];
				const auto& entry = stream.current();

				format_entry(entry, line);
				out.write(line.data(), static_cast<std::streamsize>(line.size()));

				stream.cursor++;
				if (!stream.done())
					min_heap.push({stream.current().timestamp, si});

				++entries_written;
				if (entries_written % report_interval == 0)
				{
					const double pct = total_entries > 0 ? (static_cast<double>(entries_written) / static_cast<double>(total_entries)) * 100.0 : 0.0;
					std::cout << std::format("\r  {}/{} entries processed ({:.1f}%)...", entries_written, total_entries, pct) << std::flush;
				}
			}

			out.flush();
			std::cout << std::format("\r  {}/{} entries written to output ({:.1f}%).       \n", entries_written, total_entries, total_entries > 0 ? (static_cast<double>(entries_written) / static_cast<double>(total_entries)) * 100.0 : 0.0);
			return out.good();
		}

		// Symbol resolution
		std::string resolve_address(uint64_t address)
		{
			for (const auto& mod : m_modules)
			{
				if (address < mod.base || address >= mod.base + mod.size)
					continue;

				const uint64_t rva = address - mod.base;
				if (rva == 0)
					return mod.name;

				auto* cached_pe = get_or_load_pe(mod);
				if (!cached_pe || !cached_pe->is_valid())
					return std::format("{}+{:x}", mod.name, rva);

				const auto* sym = cached_pe->get_closest_symbol(rva);
				if (!sym)
				{
					// Try section-relative
					for (const auto& sect : cached_pe->get_sections())
					{
						if (rva >= sect.rva && rva < sect.rva + sect.size)
							return std::format("{}:{}+{:x}", mod.name, sect.name, rva - sect.rva);
					}
					return std::format("{}+{:x}", mod.name, rva);
				}

				const uint64_t sym_offset = rva - sym->rva;
				if (sym_offset == 0)
					return std::format("{}!{}", mod.name, sym->name);
				return std::format("{}!{}+{:x}", mod.name, sym->name, sym_offset);
			}

			return std::format("{:016x}", address);
		}

		// Entry formatting
		void format_entry(const trace::entry& e, std::string& out)
		{
			out.clear();

			switch (static_cast<trace::format_id>(e.format_id))
			{
			case trace::fmt_ept_target_transition:
			{
				const uint64_t guest_rip = e.data[0];
				const std::string sym = resolve_address(guest_rip);
				std::format_to(std::back_inserter(out), "[{}] -> {}\n", e.core_id, sym);
				break;
			}

			default:
				std::format_to(std::back_inserter(out), "[{}] core {} unknown format_id={} data=[{:x},{:x},{:x},{:x},{:x},{:x}]\n", e.timestamp, e.core_id, e.format_id, e.data[0], e.data[1], e.data[2], e.data[3], e.data[4], e.data[5]);
				break;
			}
		}
	};

}  // namespace trace
