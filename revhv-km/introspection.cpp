#include "introspection.h"
#include "memory.h"
#include "vmx.h"
#include "exception_wrappers.h"

namespace hv::introspection
{
	size_t read_guest_virtual(const cr3& guest_cr3, const void* guest_virtual_address, void* buffer, size_t size)
	{
		if (!guest_virtual_address || !buffer || size == 0)
		{
			return 0;
		}

		vcpu::vcpu* vcpu = vmx::current_vcpu();

		size_t total_read = 0;
		while (total_read < size)
		{
			size_t offset_to_next_page = 0;
			const auto hva = memory::gva_to_hva(guest_cr3, reinterpret_cast<uint64_t>(guest_virtual_address) + total_read, &offset_to_next_page);
			if (hva == 0)
			{
				break;
			}

			size_t to_read = min(size - total_read, offset_to_next_page);
			exception_wrappers::memcpy_wrapper(static_cast<uint8_t*>(buffer) + total_read, reinterpret_cast<void*>(hva), to_read);

			if (vcpu->exception_info.exception_occurred)
			{
				vcpu::clear_exception_info(vcpu);
				break;
			}
			total_read += to_read;
		}

		return total_read;
	}

	size_t read_guest_virtual(const void* guest_virtual_address, void* buffer, size_t size)
	{
		if (!guest_virtual_address || !buffer || size == 0)
		{
			return 0;
		}

		const auto guest_cr3 = vmx::get_guest_cr3();
		return read_guest_virtual(guest_cr3, guest_virtual_address, buffer, size);
	}

	size_t write_guest_virtual(const cr3& guest_cr3, void* guest_virtual_address, const void* buffer, size_t size)
	{
		if (!guest_virtual_address || !buffer || size == 0)
		{
			return 0;
		}

		vcpu::vcpu* vcpu = vmx::current_vcpu();

		size_t total_written = 0;
		while (total_written < size)
		{
			size_t offset_to_next_page = 0;
			const auto hva = memory::gva_to_hva(guest_cr3, reinterpret_cast<uint64_t>(guest_virtual_address) + total_written, &offset_to_next_page);
			if (hva == 0)
			{
				break;
			}

			size_t to_write = min(size - total_written, offset_to_next_page);
			exception_wrappers::memcpy_wrapper(reinterpret_cast<void*>(hva), static_cast<const uint8_t*>(buffer) + total_written, to_write);

			if (vcpu->exception_info.exception_occurred)
			{
				vcpu::clear_exception_info(vcpu);
				break;
			}
			total_written += to_write;
		}

		return total_written;
	}

	size_t write_guest_virtual(void* guest_virtual_address, const void* buffer, size_t size)
	{
		if (!guest_virtual_address || !buffer || size == 0)
		{
			return 0;
		}

		const auto guest_cr3 = vmx::get_guest_cr3();
		return write_guest_virtual(guest_cr3, guest_virtual_address, buffer, size);
	}
}  // namespace hv::introspection