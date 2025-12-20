#pragma once
#include "includes.h"

namespace hv::pe
{
	/// @brief Gets the size of the image from the NT header
	/// @param image_base Pointer to the image base
	/// @return Size of the image or 0 if the image is invalid
	size_t get_image_size(const uint8_t* image_base);
}  // namespace hv::pe