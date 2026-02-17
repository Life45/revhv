#include "hypercall.h"

namespace hv::hypercall
{
	bool ping_hv()
	{
		__try
		{
			return __vmcall(hypercall_number::ping, 0, 0, 0) == 1;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// #UD if the hypercall key is invalid or if VMX is not supported/enabled
			return false;
		}
	}
}  // namespace hv::hypercall