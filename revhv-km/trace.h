#pragma once
#include "../common/trace_log.hpp"
#include <intrin.h>

namespace hv::trace
{
	/// @brief Emits a trace entry into the per-core ring buffer.
	/// @param buf  The core-local ring buffer (vcpu->trace_buffer)
	/// @param fmt  Format ID describing this entry type
	/// @param core Core ID of the originating logical processor
	/// @param d0-d5 Raw data fields (interpretation depends on format_id)
	__forceinline void emit(::trace::ring_buffer& buf, ::trace::format_id fmt, uint16_t core, uint64_t d0 = 0, uint64_t d1 = 0, uint64_t d2 = 0, uint64_t d3 = 0, uint64_t d4 = 0, uint64_t d5 = 0)
	{
		if (!buf.active)
			return;

		const uint64_t head = buf.write_head;
		const uint64_t next = (head + 1) & ::trace::ring_buffer_index_mask;

		// If buffer is full, silently drop the entry.
		// This is intentional: we never block on the hot path.
		if (next == buf.read_tail)
			return;

		auto& e = buf.entries[head];
		e.format_id = fmt;
		e.core_id = core;
		e._pad0 = 0;
		e.timestamp = __rdtsc();
		e.data[0] = d0;
		e.data[1] = d1;
		e.data[2] = d2;
		e.data[3] = d3;
		e.data[4] = d4;
		e.data[5] = d5;

		// Compiler barrier: ensure all stores to the entry are visible before we publish the new write_head
		_ReadWriteBarrier();

		buf.write_head = next;
	}

	/// @brief Drains up to max_entries from the ring buffer into a flat array.
	/// @param buf         The ring buffer to drain
	/// @param out_entries Destination array (must hold at least max_entries elements)
	/// @param max_entries Maximum number of entries to drain
	/// @return Number of entries actually copied into out_entries
	__forceinline uint64_t drain(::trace::ring_buffer& buf, ::trace::entry* out_entries, uint64_t max_entries)
	{
		uint64_t count = 0;
		uint64_t tail = buf.read_tail;

		while (count < max_entries)
		{
			// Compiler barrier before reading write_head so we see the latest value
			_ReadWriteBarrier();

			if (tail == buf.write_head)
				break;

			out_entries[count] = buf.entries[tail];
			++count;
			tail = (tail + 1) & ::trace::ring_buffer_index_mask;
		}

		// Publish the new read position
		_ReadWriteBarrier();
		buf.read_tail = tail;

		return count;
	}

}  // namespace hv::trace
