#pragma once
#include "includes.h"
#include "logger.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <urlmon.h>

#pragma comment(lib, "urlmon.lib")

namespace utils
{
	inline std::string wstring_to_string(const std::wstring& wstr)
	{
		if (wstr.empty())
			return {};

		const int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);

		if (size_needed <= 0)
			throw std::runtime_error("WideCharToMultiByte failed");

		std::string result(size_needed, 0);

		WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size_needed, nullptr, nullptr);

		return result;
	}

	inline std::wstring string_to_wstring(const std::string& str)
	{
		if (str.empty())
			return {};

		const int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);

		if (size_needed <= 0)
			throw std::runtime_error("MultiByteToWideChar failed");

		std::wstring result(size_needed, 0);

		MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size_needed);

		return result;
	}

	template <typename T>
	inline T from_hexstr(const std::string_view& hex)
	{
		T result;
		std::stringstream ss;
		ss << std::hex << hex;
		ss >> result;
		return result;
	}

	template <typename T>
	inline T from_hexstr_w(const std::wstring_view& hex)
	{
		T result;
		std::wstringstream ss;
		ss << std::hex << hex;
		ss >> result;
		return result;
	}

	template <typename T>
	inline std::string to_hexstr(T dec)
	{
		std::stringstream ss;
		ss << std::hex << dec;
		return ss.str();
	}

	template <typename T>
	inline std::wstring to_hexstr_w(T dec)
	{
		std::wstringstream ss;
		ss << std::hex << dec;
		return ss.str();
	}

	template <typename Func>
	inline void for_each_cpu(Func func)
	{
		SYSTEM_INFO info = {};
		GetSystemInfo(&info);

		for (uint32_t i = 0; i < info.dwNumberOfProcessors; ++i)
		{
			auto const prev_affinity = SetThreadAffinityMask(GetCurrentThread(), 1ull << i);
			func(i);
			SetThreadAffinityMask(GetCurrentThread(), prev_affinity);
		}
	}

	inline bool map_file(const std::string& file_path, HANDLE& out_handle, void*& out_base, size_t& out_size)
	{
		void* file = CreateFileA(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);

		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		void* fileMapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

		if (fileMapping == nullptr)
		{
			CloseHandle(file);

			return false;
		}

		void* baseAddress = MapViewOfFile(fileMapping, FILE_MAP_READ, 0, 0, 0);

		if (baseAddress == nullptr)
		{
			CloseHandle(fileMapping);
			CloseHandle(file);

			return false;
		}

		CloseHandle(fileMapping);

		BY_HANDLE_FILE_INFORMATION fileInformation;
		const bool getInformationResult = GetFileInformationByHandle(file, &fileInformation);
		if (!getInformationResult)
		{
			UnmapViewOfFile(baseAddress);
			CloseHandle(fileMapping);
			CloseHandle(file);

			return false;
		}

		const size_t fileSizeHighBytes = static_cast<size_t>(fileInformation.nFileSizeHigh) << 32;
		const size_t fileSizeLowBytes = fileInformation.nFileSizeLow;
		const size_t fileSize = fileSizeHighBytes | fileSizeLowBytes;
		out_handle = file;
		out_base = baseAddress;
		out_size = fileSize;
		return true;
	}

	inline void unmap_file(HANDLE fileHandle, void* baseAddress)
	{
		UnmapViewOfFile(baseAddress);
		CloseHandle(fileHandle);
	}

	inline bool download_file(const std::string& url, const std::string& output_path)
	{
		if (url.empty() || output_path.empty())
		{
			return false;
		}

		std::error_code error;
		const std::filesystem::path out_path(output_path);
		if (out_path.has_parent_path())
		{
			std::filesystem::create_directories(out_path.parent_path(), error);
			if (error)
			{
				return false;
			}
		}

		logger::info("Downloading file from URL: {} to path: {}", url, output_path);

		const HRESULT result = URLDownloadToFileA(nullptr, url.c_str(), output_path.c_str(), 0, nullptr);
		return SUCCEEDED(result);
	}
}  // namespace utils