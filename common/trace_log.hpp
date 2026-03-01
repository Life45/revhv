#pragma once
using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;

namespace trace
{
	/// Number of uint64_t data fields per trace entry
	constexpr int max_data_fields = 6;

	/// Number of entries in the per-core ring buffer (must be a power of 2)
	constexpr uint64_t ring_buffer_entry_count = 8192;

	/// Mask for fast modulo on power-of-2 entry count
	constexpr uint64_t ring_buffer_index_mask = ring_buffer_entry_count - 1;

	/// Maximum number of entries that can be flushed in a single hypercall
	constexpr uint64_t max_flush_entries = 512;

	/// @brief Format IDs identifying the type and layout of a trace entry.
	/// Each ID implies a fixed set of data fields; usermode uses the ID to select the correct format string.
	enum format_id : uint16_t
	{
		fmt_invalid = 0,

		/// Target execution transitioned back to normal execution.
		/// data[0] = guest RIP at transition point
		fmt_ept_target_transition = 1,

		fmt_max
	};

	/// @brief A single binary trace entry.  Exactly 64 bytes (one cache line).
	/// No string formatting is performed in kernel mode; data is stored raw.
	struct alignas(64) entry
	{
		uint16_t format_id;				 // trace::format_id
		uint16_t core_id;				 // originating logical core
		uint32_t _pad0;					 // padding (zero)
		uint64_t timestamp;				 // rdtsc value at emission time
		uint64_t data[max_data_fields];	 // payload (interpretation depends on format_id)
	};

	static_assert(sizeof(entry) == 64, "trace::entry must be exactly 64 bytes (1 cache line)");

	/// @brief Per-core single-producer / single-consumer ring buffer.
	/// Producer: VMX-root exit handler on the owning core.
	/// Consumer: hypercall handler (any core) draining entries for usermode.
	/// write_head and read_tail are on separate cache lines to avoid false sharing.
	struct ring_buffer
	{
		/// Next index the producer will write to (only modified by the producing core)
		alignas(64) volatile uint64_t write_head;

		/// Next index the consumer will read from (only modified by the consuming hypercall handler)
		alignas(64) volatile uint64_t read_tail;

		/// Whether tracing is active on this core
		volatile uint64_t active;

		/// The ring of entries
		alignas(64) entry entries[ring_buffer_entry_count];
	};

	/// @brief File header written once at the start of each per-core binary trace file.
	struct file_header
	{
		uint64_t magic;		   // 'RVTRACE' = 0x4543415254565200
		uint32_t version;	   // file format version (1)
		uint16_t core_id;	   // which core this file belongs to
		uint16_t entry_size;   // sizeof(trace::entry)
		uint64_t entry_count;  // ring_buffer_entry_count
	};

	constexpr uint64_t file_magic = 0x4543415254565200;	 // "RVTRACE\0" little-endian
	constexpr uint32_t file_version = 1;

}  // namespace trace
