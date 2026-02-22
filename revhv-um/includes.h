#pragma once
#include <cstdint>
#include <intrin.h>
#include <phnt_windows.h>
#include <phnt.h>
#include <vector>
#include <string>
#include <format>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <optional>
#include <thread>
#include <chrono>
#include <fstream>

using namespace std::chrono_literals;

// Helpers
#define _PTR_OP(a, op, b) ((ULONG_PTR)(a)op(ULONG_PTR)(b))
#define PTR_ADD(a, b) _PTR_OP(a, +, b)
#define PTR_SUB(a, b) _PTR_OP(a, -, b)
#define WITHIN_RANGE(value, low, high) ((ULONG_PTR)value >= (ULONG_PTR)low && (ULONG_PTR)value < (ULONG_PTR)high)
#define WITHIN_SIZE(value, m, s) WITHIN_RANGE(value, m, PTR_ADD(m, s))