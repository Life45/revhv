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

	/// @brief Identifies which guest value is captured in a specific data slot of a trace entry.
	/// Used in ept_transition_cfg::data_map to declare what each data[] index holds.
	enum class ept_transition_data_field : uint8_t
	{
		none = 0,
		guest_rip = 1,	// Guest RIP at the transition point (from VMCS)
		guest_rsp = 2,	// Guest RSP at the transition point (from VMCS)
		guest_rax = 3,
		guest_rbx = 4,
		guest_rcx = 5,
		guest_rdx = 6,
		guest_rsi = 7,
		guest_rdi = 8,
		guest_rbp = 9,
		guest_r8 = 10,
		guest_r9 = 11,
		guest_r10 = 12,
		guest_r11 = 13,
		guest_r12 = 14,
		guest_r13 = 15,
		guest_r14 = 16,
		guest_r15 = 17,
		// Below values require memory read(s) from guest and have higher latency impact
		// 64-bit value read from [guest_rsp] at the transition point.
		guest_retaddr = 18,
	};

	/// @brief Per-slot data-field mapping for EPT target-transition trace entries.
	/// Configures which guest value is captured in each data[] slot at emit time.
	struct ept_transition_cfg
	{
		ept_transition_data_field data_map[max_data_fields] = {};
	};

	/// @brief Default generic config: captures guest RIP in slot 0 and return address in slot 1.
	/// Applied automatically on both the hypervisor and trace parser sides when no explicit config is present.
	inline constexpr ept_transition_cfg default_generic_cfg = {{
		ept_transition_data_field::guest_rip,
		ept_transition_data_field::guest_retaddr,
	}};

	/// @brief Format IDs identifying the type and layout of a trace entry.
	/// Each ID implies a fixed set of data fields; usermode uses the ID to select the correct format string.
	enum format_id : uint16_t
	{
		fmt_invalid = 0,

		/// Target execution transitioned back to normal execution.
		/// data[0..max_data_fields-1] are populated according to the configured ept_transition_cfg
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
