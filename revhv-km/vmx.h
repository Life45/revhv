#pragma once
#include "includes.h"
#include "vcpu.h"

namespace hv::vmx
{
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
}  // namespace hv::vmx

// Inline wrappers
namespace hv::vmx
{
	/// @brief Executes VMXON
	/// @param vmxonPhys Physical address of the VMXON region
	/// @return True if VMXON was executed successfully, false otherwise
	bool vmx_vmxon(uint64_t vmxonPhys);

	/// @brief Executes VMXOFF
	void vmx_vmxoff();

	/// @brief Executes VMCLEAR
	/// @param vmcsPhys Physical address of the VMCS region
	/// @return True if VMCLEAR was executed successfully, false otherwise
	bool vmx_vmclear(uint64_t vmcsPhys);

	/// @brief Executes VMPTRLD
	/// @param vmcsPhys Physical address of the VMCS region
	/// @return True if VMPTRLD was executed successfully, false otherwise
	bool vmx_vmptrld(uint64_t vmcsPhys);

	/// @brief Executes VMWRITE
	/// @param field Field to write
	/// @param value Value to write
	/// @return True if VMWRITE was executed successfully, false otherwise
	bool vmx_vmwrite(uint64_t field, uint64_t value);
}  // namespace hv::vmx