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
	inline bool vmx_vmxon(uint64_t vmxonPhys)
	{
		return __vmx_on(reinterpret_cast<uint64_t*>(vmxonPhys)) == 0;
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
		return __vmx_vmclear(reinterpret_cast<uint64_t*>(vmcsPhys)) == 0;
	}

	/// @brief Executes VMPTRLD
	/// @param vmcsPhys Physical address of the VMCS region
	/// @return True if VMPTRLD was executed successfully, false otherwise
	inline bool vmx_vmptrld(uint64_t vmcsPhys)
	{
		return __vmx_vmptrld(reinterpret_cast<uint64_t*>(vmcsPhys)) == 0;
	}

	/// @brief Executes VMWRITE
	/// @param field Field to write
	/// @param value Value to write
	/// @return True if VMWRITE was executed successfully, false otherwise
	inline bool vmx_vmwrite(uint64_t field, uint64_t value)
	{
		return __vmx_vmwrite(field, value) == 0;
	}
}  // namespace hv::vmx