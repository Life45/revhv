#include "pdb_parser.hpp"
#include <raw_pdb/src/PDB.h>
#include <raw_pdb/src/PDB_RawFile.h>
#include <raw_pdb/src/PDB_InfoStream.h>
#include <raw_pdb/src/PDB_DBIStream.h>
#include <raw_pdb/src/PDB_CoalescedMSFStream.h>
#include <raw_pdb/src/PDB_ImageSectionStream.h>
#include <raw_pdb/src/PDB_CoalescedMSFStream.h>
#include <raw_pdb/src/PDB_CoalescedMSFStream.h>
#include <raw_pdb/src/PDB_TPIStream.h>
#include <raw_pdb/src/PDB_PublicSymbolStream.h>
#include <raw_pdb/src/PDB_GlobalSymbolStream.h>
#include <raw_pdb/src/PDB_ModuleInfoStream.h>
#include <raw_pdb/src/PDB_ModuleSymbolStream.h>
#include <raw_pdb/src/PDB_ImageSectionStream.h>
#include <algorithm>
#include "utils.hpp"
#include "logger.hpp"

namespace pdb
{
	static bool is_error(PDB::ErrorCode errorCode)
	{
		switch (errorCode)
		{
		case PDB::ErrorCode::Success:
			return false;
		case PDB::ErrorCode::InvalidSuperBlock:
			logger::error("Invalid Superblock");
			return true;
		case PDB::ErrorCode::InvalidFreeBlockMap:
			logger::error("Invalid free block map");
			return true;
		case PDB::ErrorCode::InvalidStream:
			logger::error("Invalid stream");
			return true;
		case PDB::ErrorCode::InvalidSignature:
			logger::error("Invalid stream signature");
			return true;
		case PDB::ErrorCode::InvalidStreamIndex:
			logger::error("Invalid stream index");
			return true;
		case PDB::ErrorCode::InvalidDataSize:
			logger::error("Invalid data size");
			return true;
		case PDB::ErrorCode::UnknownVersion:
			logger::error("Unknown version");
			return true;
		}
		return true;
	}

	static bool has_valid_dbi_streams(const PDB::RawFile& rawPdbFile, const PDB::DBIStream& dbiStream)
	{
		if (is_error(dbiStream.HasValidSymbolRecordStream(rawPdbFile)))
			return false;
		if (is_error(dbiStream.HasValidPublicSymbolStream(rawPdbFile)))
			return false;
		if (is_error(dbiStream.HasValidGlobalSymbolStream(rawPdbFile)))
			return false;
		if (is_error(dbiStream.HasValidSectionContributionStream(rawPdbFile)))
			return false;
		if (is_error(dbiStream.HasValidImageSectionStream(rawPdbFile)))
			return false;
		return true;
	}

	static bool get_symbols(const PDB::RawFile& rawPdbFile, const PDB::DBIStream& dbiStream, std::vector<symbol_info>& output)
	{
		// Prepare the image section stream for RVA conversion
		const PDB::ImageSectionStream imageSectionStream = dbiStream.CreateImageSectionStream(rawPdbFile);

		// Prepare symbol record stream
		const PDB::CoalescedMSFStream symbolRecordStream = dbiStream.CreateSymbolRecordStream(rawPdbFile);

		// Public symbols
		const PDB::PublicSymbolStream publicSymbolStream = dbiStream.CreatePublicSymbolStream(rawPdbFile);
		const PDB::ArrayView<PDB::HashRecord> publicHashRecords = publicSymbolStream.GetRecords();

		for (const PDB::HashRecord& hashRecord : publicHashRecords)
		{
			const PDB::CodeView::DBI::Record* record = publicSymbolStream.GetRecord(symbolRecordStream, hashRecord);
			if (record->header.kind != PDB::CodeView::DBI::SymbolRecordKind::S_PUB32)
				continue;

			const uint32_t rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_PUB32.section, record->data.S_PUB32.offset);
			if (rva == 0u)
				continue;

			output.emplace_back(symbol_info{record->data.S_PUB32.name, rva});
		}

		// Global symbols
		const PDB::GlobalSymbolStream globalSymbolStream = dbiStream.CreateGlobalSymbolStream(rawPdbFile);
		const PDB::ArrayView<PDB::HashRecord> globalHashRecords = globalSymbolStream.GetRecords();

		for (const PDB::HashRecord& hashRecord : globalHashRecords)
		{
			const PDB::CodeView::DBI::Record* record = globalSymbolStream.GetRecord(symbolRecordStream, hashRecord);

			const char* name = nullptr;
			uint32_t rva = 0u;

			if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GDATA32)
			{
				name = record->data.S_GDATA32.name;
				rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GDATA32.section, record->data.S_GDATA32.offset);
			}
			else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GTHREAD32)
			{
				name = record->data.S_GTHREAD32.name;
				rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GTHREAD32.section, record->data.S_GTHREAD32.offset);
			}
			else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LDATA32)
			{
				name = record->data.S_LDATA32.name;
				rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LDATA32.section, record->data.S_LDATA32.offset);
			}
			else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LTHREAD32)
			{
				name = record->data.S_LTHREAD32.name;
				rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LTHREAD32.section, record->data.S_LTHREAD32.offset);
			}

			if (rva == 0u || name == nullptr)
				continue;

			output.emplace_back(symbol_info{name, rva});
		}

		// Module symbols
		const PDB::ModuleInfoStream moduleInfoStream = dbiStream.CreateModuleInfoStream(rawPdbFile);
		const PDB::ArrayView<PDB::ModuleInfoStream::Module> modules = moduleInfoStream.GetModules();

		for (const PDB::ModuleInfoStream::Module& module : modules)
		{
			if (!module.HasSymbolStream())
				continue;

			const PDB::ModuleSymbolStream moduleSymbolStream = module.CreateSymbolStream(rawPdbFile);
			moduleSymbolStream.ForEachSymbol(
				[&imageSectionStream, &output](const PDB::CodeView::DBI::Record* record)
				{
					const char* name = nullptr;
					uint32_t rva = 0u;

					if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32)
					{
						name = record->data.S_LPROC32.name;
						rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32.section, record->data.S_LPROC32.offset);
					}
					else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32)
					{
						name = record->data.S_GPROC32.name;
						rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32.section, record->data.S_GPROC32.offset);
					}
					else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32_ID)
					{
						name = record->data.S_LPROC32_ID.name;
						rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32_ID.section, record->data.S_LPROC32_ID.offset);
					}
					else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32_ID)
					{
						name = record->data.S_GPROC32_ID.name;
						rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32_ID.section, record->data.S_GPROC32_ID.offset);
					}
					else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LDATA32)
					{
						name = record->data.S_LDATA32.name;
						rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LDATA32.section, record->data.S_LDATA32.offset);
					}
					else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LTHREAD32)
					{
						name = record->data.S_LTHREAD32.name;
						rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LTHREAD32.section, record->data.S_LTHREAD32.offset);
					}

					if (rva == 0u || name == nullptr)
						return;

					output.emplace_back(symbol_info{name, rva});
				});
		}

		return true;
	}

	bool load_symbols(const std::string& pdb_path, std::vector<symbol_info>& output)
	{
		HANDLE file_handle = INVALID_HANDLE_VALUE;
		void* base_address = nullptr;
		size_t file_size = 0;

		if (!utils::map_file(pdb_path, file_handle, base_address, file_size))
		{
			logger::error("Failed to open PDB file");
			return false;
		}

		// Validate the PDB file
		if (is_error(PDB::ValidateFile(base_address, file_size)))
		{
			return false;
		}

		// Create raw PDB file
		const PDB::RawFile raw_pdb_file = PDB::CreateRawFile(base_address);
		if (is_error(PDB::HasValidDBIStream(raw_pdb_file)))
		{
			return false;
		}

		// Create info stream
		const PDB::InfoStream info_stream(raw_pdb_file);
		if (info_stream.UsesDebugFastLink())
		{
			logger::error("PDB was linked using unsupported option /DEBUG:FASTLINK");
			return false;
		}

		// Create DBI stream
		const PDB::DBIStream dbi_stream = PDB::CreateDBIStream(raw_pdb_file);
		if (!has_valid_dbi_streams(raw_pdb_file, dbi_stream))
		{
			return false;
		}

		// Get symbols
		if (!get_symbols(raw_pdb_file, dbi_stream, output))
		{
			return false;
		}

		std::sort(output.begin(), output.end(), [](const symbol_info& left, const symbol_info& right) { return left.rva < right.rva; });

		utils::unmap_file(file_handle, base_address);
		return true;
	}
}  // namespace pdb