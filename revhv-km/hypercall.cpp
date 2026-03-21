#include "hypercall.h"
#include "introspection.h"
#include "hv.h"
#include "vmx.h"
#include "trace.h"
#include "apic.h"

namespace hv::hypercall
{
	static void handle_vmem_read(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// We don't do large stack allocation here since 1- the stack size is limited
		// 2- the compiler can add a _chkstk call which faults since we don't have it mapped
		utils::memset(vcpu->hypercall_local_buffer, 0, sizeof(vcpu->hypercall_local_buffer));

		const void* vmem_request_buffer = reinterpret_cast<const void*>(guest_context->rdx);
		vmem_request request{};

		if (introspection::read_guest_virtual(vmem_request_buffer, &request, sizeof(request)) != sizeof(request))
		{
			LOG_ERROR("Failed to read vmem_request from guest");
			// Failure (0 bytes read)
			guest_context->rax = 0;
			return;
		}

		auto bytes_to_read = min(request.size, MAX_VMEM_CHUNK_SIZE);
		if (bytes_to_read == 0)
		{
			// No bytes to read
			guest_context->rax = 0;
			return;
		}

		void* guest_buffer = reinterpret_cast<void*>(request.guest_buffer);
		const void* target_va = reinterpret_cast<const void*>(request.target_va);

		// If target_cr3 is 0, use system CR3
		cr3 target_cr3;
		target_cr3.flags = request.target_cr3 ? request.target_cr3 : hv::g_hv.system_cr3.flags;

		// Read from the target guest VA to the local buffer
		auto bytes_read = introspection::read_guest_virtual(target_cr3, target_va, vcpu->hypercall_local_buffer, bytes_to_read);
		if (bytes_read == 0)
		{
			LOG_ERROR("Failed to read guest virtual memory at CR3 %llx, VA %llx", target_cr3.flags, target_va);
			// Failure (0 bytes read)
			guest_context->rax = 0;
			return;
		}

		// Write the read data back to the guest buffer
		if (introspection::write_guest_virtual(guest_buffer, vcpu->hypercall_local_buffer, bytes_read) != bytes_read)
		{
			LOG_ERROR("Failed to write data back to guest virtual memory at VA %llx", guest_buffer);
			// Failure (0 bytes read)
			guest_context->rax = 0;
			return;
		}

		guest_context->rax = bytes_read;
	}

	static void handle_vmem_write(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// We don't do large stack allocation here since 1- the stack size is limited
		// 2- the compiler can add a _chkstk call which faults since we don't have it mapped
		utils::memset(vcpu->hypercall_local_buffer, 0, sizeof(vcpu->hypercall_local_buffer));

		const void* vmem_request_buffer = reinterpret_cast<const void*>(guest_context->rdx);
		vmem_request request{};

		if (introspection::read_guest_virtual(vmem_request_buffer, &request, sizeof(request)) != sizeof(request))
		{
			LOG_ERROR("Failed to read vmem_request from guest");
			// Failure (0 bytes written)
			guest_context->rax = 0;
			return;
		}

		auto bytes_to_write = min(request.size, MAX_VMEM_CHUNK_SIZE);
		if (bytes_to_write == 0)
		{
			// No bytes to write
			guest_context->rax = 0;
			return;
		}

		const void* guest_buffer = reinterpret_cast<const void*>(request.guest_buffer);
		void* target_va = reinterpret_cast<void*>(request.target_va);

		// If target_cr3 is 0, use system CR3
		cr3 target_cr3;
		target_cr3.flags = request.target_cr3 ? request.target_cr3 : hv::g_hv.system_cr3.flags;

		// Read the source data from the caller's guest buffer
		auto bytes_read = introspection::read_guest_virtual(guest_buffer, vcpu->hypercall_local_buffer, bytes_to_write);
		if (bytes_read == 0)
		{
			LOG_ERROR("Failed to read source guest buffer at VA %llx", guest_buffer);
			// Failure (0 bytes written)
			guest_context->rax = 0;
			return;
		}

		// Write that data to the target VA in the requested CR3
		auto bytes_written = introspection::write_guest_virtual(target_cr3, target_va, vcpu->hypercall_local_buffer, bytes_read);
		if (bytes_written == 0)
		{
			LOG_ERROR("Failed to write guest virtual memory at CR3 %llx, VA %llx", target_cr3.flags, target_va);
			// Failure (0 bytes written)
			guest_context->rax = 0;
			return;
		}

		guest_context->rax = bytes_written;
	}

	static void handle_ping(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		LOG_INFO("Received ping hypercall from guest");
		guest_context->rax = 1;
	}

	static void handle_flush_standard_logs(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// RDX is the pointer to the buffer in guest memory, and R8 is the maximum number of messages to flush
		auto guest_buffer = reinterpret_cast<logging::standard_log_message*>(guest_context->rdx);
		size_t max_messages = guest_context->r8;

		const size_t flushed = logging::flush_standard_logs(guest_buffer, max_messages, true);

		// Return the number of messages flushed
		guest_context->rax = flushed;
	}

	static void handle_ept_hook(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// RDX is the original PFN, and R8 is the hook PFN
		uint64_t orig_pfn = guest_context->rdx;
		uint64_t hook_pfn = guest_context->r8;

		if (ept::add_hook(vcpu->ept_pages_normal, orig_pfn, hook_pfn) && ept::add_hook(vcpu->ept_pages_target, orig_pfn, hook_pfn))
		{
			LOG_INFO("Added EPT hook: original PFN 0x%llx -> hook PFN 0x%llx", orig_pfn, hook_pfn);
			guest_context->rax = 1;	 // Success
		}
		else
		{
			LOG_ERROR("Failed to add EPT hook: original PFN 0x%llx -> hook PFN 0x%llx", orig_pfn, hook_pfn);
			guest_context->rax = 0;	 // Failure
		}

		vmx::invept(invept_all_context, 0);
	}

	static void handle_enable_auto_trace(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// RDX is the VA, R8 is the size of the target
		uint64_t target_va = guest_context->rdx;
		uint64_t target_size = guest_context->r8;

		// Deactivate EPT hooks before enabling auto-trace to avoid permission conflicts
		ept::deactivate_hooks(vcpu->ept_pages_normal);
		ept::deactivate_hooks(vcpu->ept_pages_target);

		if (ept::enable_auto_trace(vcpu->ept_pages_normal, vcpu->ept_pages_target, target_va, target_size))
		{
			LOG_INFO("Successfully enabled auto-trace.");

			// Activate the per-core trace ring buffer
			vcpu->trace_buffer.write_head = 0;
			vcpu->trace_buffer.read_tail = 0;
			vcpu->trace_buffer.active = 1;

			// Start with normal EPTP, the EPT violation handler will switch when target starts executing
			vmx::change_eptp(vcpu->eptp_normal_execution);
			vcpu->in_normal_execution = true;
			vmx::invept(invept_all_context, 0);

			guest_context->rax = 1;	 // Success
		}
		else
		{
			LOG_ERROR("Failed to enable auto-trace for VA range 0x%llx - 0x%llx", target_va, target_va + target_size);

			// Reactivate hooks since auto-trace failed
			ept::reactivate_hooks(vcpu->ept_pages_normal);
			ept::reactivate_hooks(vcpu->ept_pages_target);
			vmx::invept(invept_all_context, 0);

			guest_context->rax = 0;	 // Failure
		}
	}

	static void handle_disable_auto_trace(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// Deactivate trace buffer before restoring EPT
		vcpu->trace_buffer.active = 0;

		ept::restore_auto_trace(vcpu->ept_pages_normal, vcpu->ept_pages_target);

		// Reactivate EPT hooks now that auto-trace permissions are restored
		ept::reactivate_hooks(vcpu->ept_pages_normal);
		ept::reactivate_hooks(vcpu->ept_pages_target);

		vmx::change_eptp(vcpu->eptp_normal_execution);
		vcpu->in_normal_execution = true;
		vmx::invept(invept_all_context, 0);

		LOG_INFO("Auto-trace disabled, switched back to normal EPTP, hooks reactivated");
	}

	static void handle_flush_trace_logs(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// RDX = core_id whose trace buffer to drain
		// R8  = pointer to guest buffer (array of trace::entry)
		// R9  = max entries to copy
		const uint64_t target_core = guest_context->rdx;
		auto guest_buffer = reinterpret_cast<::trace::entry*>(guest_context->r8);
		uint64_t max_entries = guest_context->r9;

		// Bounds-check the core id
		if (target_core >= hv::g_hv.vcpu_count)
		{
			guest_context->rax = 0;
			return;
		}

		// Clamp to the per-hypercall limit
		if (max_entries > ::trace::max_flush_entries)
			max_entries = ::trace::max_flush_entries;

		auto& target_vcpu = hv::g_hv.vcpus[target_core];

		// Reuse the vcpu-local scratch buffer to avoid any stack allocation that would
		// trigger _chkstk (ntoskrnl is not mapped in VMX-root mode).
		static_assert(sizeof(vcpu->hypercall_local_buffer) >= sizeof(::trace::entry), "hypercall_local_buffer too small for even one trace entry");
		constexpr uint64_t batch_capacity = sizeof(vcpu->hypercall_local_buffer) / sizeof(::trace::entry);
		auto* batch = reinterpret_cast<::trace::entry*>(vcpu->hypercall_local_buffer);

		uint64_t total_flushed = 0;

		while (total_flushed < max_entries)
		{
			uint64_t to_drain = max_entries - total_flushed;
			if (to_drain > batch_capacity)
				to_drain = batch_capacity;

			uint64_t drained = hv::trace::drain(target_vcpu.trace_buffer, batch, to_drain);
			if (drained == 0)
				break;

			// Write the batch to guest memory
			size_t bytes_to_write = static_cast<size_t>(drained) * sizeof(::trace::entry);
			auto dest = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(guest_buffer) + total_flushed * sizeof(::trace::entry));

			if (introspection::write_guest_virtual(dest, batch, bytes_to_write) != bytes_to_write)
			{
				LOG_ERROR("flush_trace_logs: failed to write %llu entries to guest buffer", drained);
				break;
			}

			total_flushed += drained;
		}

		guest_context->rax = total_flushed;
	}

	static void handle_get_apic_info(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		apic_info info = {0};
		info.x2apic = apic::is_x2apic_enabled();
		if (!info.x2apic)
		{
			info.lapic_mmio_phys_base = reinterpret_cast<uint64_t>(apic::get_lapic_mmio_phys_base());
		}

		auto guest_buffer = reinterpret_cast<void*>(guest_context->rdx);
		if (introspection::write_guest_virtual(guest_buffer, &info, sizeof(info)) != sizeof(info))
		{
			LOG_ERROR("Failed to write APIC info back to guest");
			guest_context->rax = 0;	 // Failure
			return;
		}

		guest_context->rax = 1;	 // Success
	}

	bool handle_hypercall(vcpu::guest_context* guest_context, vcpu::vcpu* vcpu)
	{
		// Rax must be the hypercall key, otherwise it's not a valid hypercall and we should inject #UD
		if (guest_context->rax != HYPERCALL_KEY)
		{
			return false;
		}

		switch (guest_context->rcx)
		{
		case hypercall_number::ping:
			handle_ping(guest_context, vcpu);
			break;
		case hypercall_number::flush_standard_logs:
			handle_flush_standard_logs(guest_context, vcpu);
			break;
		case hypercall_number::read_vmem:
			handle_vmem_read(guest_context, vcpu);
			break;
		case hypercall_number::write_vmem:
			handle_vmem_write(guest_context, vcpu);
			break;
		case hypercall_number::ept_hook:
			handle_ept_hook(guest_context, vcpu);
			break;
		case hypercall_number::enable_auto_trace:
			handle_enable_auto_trace(guest_context, vcpu);
			break;
		case hypercall_number::disable_auto_trace:
			handle_disable_auto_trace(guest_context, vcpu);
			break;
		case hypercall_number::flush_trace_logs:
			handle_flush_trace_logs(guest_context, vcpu);
			break;
		case hypercall_number::get_apic_info:
			handle_get_apic_info(guest_context, vcpu);
			break;
		case hypercall_number::test_host_df:
			// This hypercall is meant for testing the robustness of the host crash handling mechanism by intentionally causing a double fault on the host.
			// It does this by thrashing the host RSP
			LOG_INFO("Received test_host_df hypercall, intentionally causing host double fault...");
			utils::segment::test_trash_rsp();
			break;
		default:
			LOG_WARNING("Received unknown hypercall number: %llu", guest_context->rcx);
			break;
		}

		return true;
	}
}  // namespace hv::hypercall