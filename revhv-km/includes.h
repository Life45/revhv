#pragma once
#include <ntddk.h>
#include <intrin.h>
#include <ia32.hpp>
#include "logging.h"
using int64_t = __int64;
extern "C" __MACHINE(void _sgdt(void*));
extern "C" __MACHINE(void _lgdt(const void*));