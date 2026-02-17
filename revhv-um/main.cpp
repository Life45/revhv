#include "includes.h"
#include "hypercall.h"
#include "logger.hpp"
#include "utils.hpp"

int main()
{
	logger::info("revhv-um started");

	bool fail = false;
	utils::for_each_cpu(
		[&fail](size_t i)
		{
			if (hv::hypercall::ping_hv())
			{
				logger::info("HV alive on core {}", i);
			}
			else
			{
				logger::error("Failed to receive response from hypervisor on CPU core {}", i);
				fail = true;
			}
		});

	if (fail)
	{
		logger::error("Some CPU cores failed to communicate with the hypervisor.");
		return -1;
	}

	system("pause");

	return 0;
}