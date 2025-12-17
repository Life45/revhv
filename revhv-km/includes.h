#pragma once
#include <ntddk.h>
#include <intrin.h>
#include <ia32.hpp>
#include "logging.h"
extern "C" __MACHINE(void _sgdt(void*));