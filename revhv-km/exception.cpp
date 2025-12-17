#include "exception.h"

namespace hv::exception
{
	void handle_exception(trap_frame* trap_frame, vcpu::vcpu* vcpu)
	{
		// TODO: Implement exception handling
		LOG_ERROR("Exception occurred on vCPU %lu, vector: %llx", vcpu->core_id, trap_frame->vector);
		// TODO: Fail appropriately
		__halt();
	}
}  // namespace hv::exception