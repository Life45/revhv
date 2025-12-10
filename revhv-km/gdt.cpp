#include "gdt.h"

namespace hv::gdt
{
	void initialize_tss(task_state_segment_64* tss, const uint8_t* ist1, const uint8_t* ist2, const uint8_t* ist3, size_t ist_size)
	{
		memset(tss, 0, sizeof(task_state_segment_64));

		tss->ist1 = reinterpret_cast<uint64_t>(ist1) + ist_size;
		tss->ist2 = reinterpret_cast<uint64_t>(ist2) + ist_size;
		tss->ist3 = reinterpret_cast<uint64_t>(ist3) + ist_size;

		// Volume 1, 20.5.2 I/O Permission Bit Map
		// "If the I/O bit map base address is greater than or equal to the TSS segment limit, there is no I/O permission map,
		// and all I/O instructions generate exceptions when the CPL is greater than the current IOPL."
		tss->io_map_base = MAXUSHORT;
	}

	void initialize(segment_descriptor_32* gdt, task_state_segment_64* tss)
	{
		memset(gdt, 0, host_descriptor_count * sizeof(segment_descriptor_32));

		// 3.4.5.1 Code- and Data-Segment Descriptor Types

		segment_descriptor_32* host_cs_descriptor = &gdt[host_cs.index];
		host_cs_descriptor->long_mode = 1;
		host_cs_descriptor->present = 1;
		host_cs_descriptor->descriptor_type = SEGMENT_DESCRIPTOR_TYPE_CODE_OR_DATA;
		host_cs_descriptor->type = SEGMENT_DESCRIPTOR_TYPE_CODE_EXECUTE_READ_ACCESSED;
		// Segment base and limit is ignored in 64-bit mode for CS

		segment_descriptor_32* host_ss_descriptor = &gdt[host_ss.index];
		host_ss_descriptor->present = 1;
		host_ss_descriptor->descriptor_type = SEGMENT_DESCRIPTOR_TYPE_CODE_OR_DATA;
		host_ss_descriptor->type = SEGMENT_DESCRIPTOR_TYPE_DATA_READ_WRITE;
		// Segment base and limit is ignored in 64-bit mode for SS

		segment_descriptor_64* host_tr_descriptor = reinterpret_cast<segment_descriptor_64*>(&gdt[host_tr.index]);
		host_tr_descriptor->present = 1;
		host_tr_descriptor->descriptor_type = SEGMENT_DESCRIPTOR_TYPE_SYSTEM;
		host_tr_descriptor->type = SEGMENT_DESCRIPTOR_TYPE_TSS_AVAILABLE;
		uint64_t tss_base = reinterpret_cast<uint64_t>(tss);
		host_tr_descriptor->base_address_low = tss_base & 0xFFFF;
		host_tr_descriptor->base_address_middle = (tss_base >> 16) & 0xFF;
		host_tr_descriptor->base_address_high = (tss_base >> 24) & 0xFF;
		host_tr_descriptor->base_address_upper = (tss_base >> 32);
		uint16_t tss_limit = sizeof(task_state_segment_64) - 1;
		host_tr_descriptor->segment_limit_low = tss_limit & 0xFFFF;
		host_tr_descriptor->segment_limit_high = (tss_limit >> 16) & 0xF;
	}
}  // namespace hv::gdt