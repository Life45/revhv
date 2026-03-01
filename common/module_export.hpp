#pragma once
using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;

namespace module_export
{
	constexpr uint64_t file_magic = 0x444F4D5652;  // "RVMOD" little-endian
	constexpr uint32_t file_version = 1;

	/// @brief File header written once at the start of the exported module list.
	struct file_header
	{
		uint64_t magic;			// module_export::file_magic
		uint32_t version;		// file format version
		uint32_t module_count;	// number of module_entry records that follow
	};

	/// @brief Maximum lengths for inline string fields.
	constexpr uint32_t max_name_length = 64;
	constexpr uint32_t max_path_length = 260;

	/// @brief Fixed-size record describing one loaded kernel module.
	/// Strings are null-terminated and padded with zeroes.
	struct module_entry
	{
		uint64_t base;					  // module base address
		uint64_t size;					  // module size in bytes
		char name[max_name_length];		  // short name (no extension), null-terminated
		char full_path[max_path_length];  // full NT/DOS path, null-terminated
	};

}  // namespace module_export
