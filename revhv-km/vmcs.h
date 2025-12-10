#pragma once
#include "includes.h"
#include "vcpu.h"

namespace hv::vmcs
{
	/// @brief Writes a control field to the VMCS region while ensuring capabilities are respected
	/// @param value Value to write
	/// @param controlField Control field to write
	/// @param capMsr MSR to read capabilities from
	/// @param trueCapMsr MSR to read true capabilities from
	/// @return True if the control field was written successfully, false otherwise
	bool write_control_field(uint64_t value, const uint64_t controlField, const uint64_t capMsr, const uint64_t trueCapMsr);

	/// @brief Loads the VMCS region
	/// @param vcpu Current vcpu
	/// @return True if the VMCS region was loaded successfully, false otherwise
	bool load_vmcs(vcpu::vcpu* vcpu);

	/// @brief Writes the control fields to the VMCS region
	/// @param vcpu Current vcpu
	bool write_control_fields(vcpu::vcpu* vcpu);
}  // namespace hv::vmcs