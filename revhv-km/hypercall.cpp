#include "hypercall.h"
#include "introspection.h"
#include "hv.h"

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
		default:
			LOG_WARNING("Received unknown hypercall number: %llu", guest_context->rcx);
			break;
		}

		return true;
	}
}  // namespace hv::hypercall