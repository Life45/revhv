#pragma once
#include "includes.h"
#include "vcpu.h"

namespace hv::vmx
{
	/// @brief VMLAUNCH stub
	/// @note: Defined in vmlaunch_stub.asm
	extern "C" bool vmx_vmlaunch_stub(uint64_t* rflags);

	/// @brief Checks if VMX is supported by the CPU
	/// @return True if VMX is supported, false otherwise
	bool check_vmx_support();

	/// @brief Enables VMX
	void enable_vmx();

	/// @brief Enters VMX operation using VMXON
	/// @param vcpu Current vcpu
	/// @return True if VMX operation was entered successfully, false otherwise
	bool enter_vmx_operation(vcpu::vcpu* vcpu);

	/// @brief Exits VMX operation using VMXOFF
	void exit_vmx_operation();

	/// @brief Launches the VM using VMLAUNCH
	/// @return True if VM was launched successfully, false otherwise
	bool launch_vm();

	extern "C" void __invept(invept_type type, const invept_descriptor* descriptor);
}  // namespace hv::vmx

// Inline wrappers
namespace hv::vmx
{
	/// @brief Executes VMXON
	/// @param vmxonPhys Physical address of the VMXON region
	/// @return True if VMXON was executed successfully, false otherwise
	inline bool vmx_vmxon(uint64_t vmxonPhys)
	{
		uint64_t vmxon_pa = vmxonPhys;
		return __vmx_on(reinterpret_cast<uint64_t*>(&vmxon_pa)) == 0;
	}

	/// @brief Executes VMXOFF
	inline void vmx_vmxoff()
	{
		__vmx_off();
	}

	/// @brief Executes VMCLEAR
	/// @param vmcsPhys Physical address of the VMCS region
	/// @return True if VMCLEAR was executed successfully, false otherwise
	inline bool vmx_vmclear(uint64_t vmcsPhys)
	{
		uint64_t vmcs_pa = vmcsPhys;
		return __vmx_vmclear(reinterpret_cast<uint64_t*>(&vmcs_pa)) == 0;
	}

	/// @brief Executes VMPTRLD
	/// @param vmcsPhys Physical address of the VMCS region
	/// @return True if VMPTRLD was executed successfully, false otherwise
	inline bool vmx_vmptrld(uint64_t vmcsPhys)
	{
		uint64_t vmcs_pa = vmcsPhys;
		return __vmx_vmptrld(reinterpret_cast<uint64_t*>(&vmcs_pa)) == 0;
	}

	/// @brief Executes VMWRITE
	/// @param field Field to write
	/// @param value Value to write
	/// @return True if VMWRITE was executed successfully, false otherwise
	inline bool vmx_vmwrite(uint64_t field, uint64_t value)
	{
		return __vmx_vmwrite(field, value) == 0;
	}

	/// @brief Executes VMREAD
	/// @param field Field to read
	/// @return Value of the field
	inline uint64_t vmx_vmread(uint64_t field)
	{
		uint64_t value;
		if (__vmx_vmread(field, &value) != 0)
		{
			return 0;
		}
		return value;
	}

	/// @brief Executes INVEPT
	/// @param type invept_single_context or invept_all_context
	/// @param eptp EPTP to invalidate (only used for invept_single_context)
	inline void invept(invept_type type, uint64_t eptp)
	{
		invept_descriptor descriptor = {0};
		descriptor.ept_pointer = eptp;
		descriptor.reserved = 0;

		__invept(type, &descriptor);
	}

	/// @brief Clears the VM-entry interruption information field
	inline void clear_vmentry_interrupt_info()
	{
		vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, 0);
	}

	/// @brief Gets the CPL of the guest
	/// @return CPL of the guest
	inline uint16_t get_guest_cpl()
	{
		vmx_segment_access_rights ss = {0};
		ss.flags = vmx_vmread(VMCS_GUEST_SS_ACCESS_RIGHTS);
		return ss.descriptor_privilege_level;
	}

	/// @brief Gets the CR3 of the guest
	/// @return CR3 of the guest
	inline cr3 get_guest_cr3()
	{
		cr3 guest_cr3 = {0};
		guest_cr3.flags = vmx_vmread(VMCS_GUEST_CR3);
		return guest_cr3;
	}

	/// @brief Injects a hardware exception with an error code
	/// @param vector Vector of the exception
	/// @param error_code Error code of the exception
	inline void inject_hw_exception(uint16_t vector, uint16_t error_code)
	{
		vmentry_interrupt_information interrupt_info = {0};
		interrupt_info.vector = vector;
		interrupt_info.interruption_type = hardware_exception;
		interrupt_info.deliver_error_code = 1;
		interrupt_info.valid = 1;
		vmx_vmwrite(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE, error_code);
		vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, interrupt_info.flags);
	}

	/// @brief Injects a hardware exception
	/// @param vector Vector of the exception
	inline void inject_hw_exception(uint16_t vector)
	{
		vmentry_interrupt_information interrupt_info = {0};
		interrupt_info.vector = vector;
		interrupt_info.interruption_type = hardware_exception;
		interrupt_info.deliver_error_code = 0;
		interrupt_info.valid = 1;
		vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, interrupt_info.flags);
	}

	/// @brief Injects a NMI
	inline void inject_nmi()
	{
		vmentry_interrupt_information interrupt_info = {0};
		interrupt_info.vector = nmi;
		interrupt_info.interruption_type = non_maskable_interrupt;
		interrupt_info.valid = 1;
		vmx_vmwrite(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, interrupt_info.flags);
	}

	/// @brief Returns a pointer to the current vCPU structure in vmx-root
	/// @return Pointer to the current vCPU structure
	inline vcpu::vcpu* current_vcpu()
	{
		return reinterpret_cast<vcpu::vcpu*>(_readfsbase_u64());
	}
}  // namespace hv::vmx