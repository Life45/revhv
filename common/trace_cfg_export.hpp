#pragma once
#include "trace_log.hpp"
using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;

namespace trace_cfg_export
{
	constexpr uint64_t file_magic = 0x47464354565200ULL;  // "RVTCFG\0" little-endian
	constexpr uint32_t file_version = 2;

	// Maximum length (including null terminator) for a custom display format string.
	constexpr size_t max_format_length = 256;

	/// @brief File header written at the start of a trace_cfg.bin export.
	/// Immediately followed by exact_entry_count records of type exact_entry.
	struct file_header
	{
		uint64_t magic;							 // trace_cfg_export::file_magic
		uint32_t version;						 // file format version
		uint32_t data_field_count;				 // = trace::max_data_fields
		uint32_t exact_entry_count;				 // number of exact_entry records that follow
		uint32_t _pad;							 // zero padding
		trace::ept_transition_cfg generic_cfg;	 // fallback config
		char generic_format[max_format_length];	 // optional display format for generic config
	};

	/// @brief One exact-address record in a trace_cfg.bin export.
	struct exact_entry
	{
		uint64_t addr;
		trace::ept_transition_cfg cfg;
		char format[max_format_length];	 // optional display format for this address
	};

}  // namespace trace_cfg_export
