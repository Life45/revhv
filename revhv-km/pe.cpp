#include "pe.h"
#include <ntimage.h>

namespace hv::pe
{
	size_t get_image_size(const uint8_t* image_base)
	{
		auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(image_base);
		if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
		{
			return 0;
		}

		auto nt_header = reinterpret_cast<const IMAGE_NT_HEADERS*>(image_base + dos_header->e_lfanew);
		if (nt_header->Signature != IMAGE_NT_SIGNATURE)
		{
			return 0;
		}

		return nt_header->OptionalHeader.SizeOfImage;
	}
}  // namespace hv::pe