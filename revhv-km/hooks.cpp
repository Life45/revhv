#include "hooks.h"
#include "hv.h"
#include "pe.h"
#include "vmx.h"
#include "hypercall.h"
#include "Zydis.h"

namespace hv::hooks
{
	namespace impl
	{
		static constexpr size_t absolute_jump_size = 14;
		static constexpr size_t page_size = 0x1000;

		/// @brief Case-insensitive ASCII comparison for driver basenames.
		static bool names_match_nocase(const char* a, const char* b)
		{
			while (*a && *b)
			{
				char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + ('a' - 'A')) : *a;
				char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + ('a' - 'A')) : *b;
				if (ca != cb)
					return false;
				++a;
				++b;
			}
			return *a == *b;
		}

		static void write_absolute_jmp(char* target_buffer, size_t target_address)
		{
			// Use "push + ret" instead of "jmp qword ptr [rip + 0]".
			// This avoids reading an extra 8 bytes from the hooked page.
			const uint32_t low32 = static_cast<uint32_t>(target_address);
			const uint32_t high32 = static_cast<uint32_t>(target_address >> 32);

			target_buffer[0] = 0x68;
			*reinterpret_cast<uint32_t*>(&target_buffer[1]) = low32;

			*reinterpret_cast<uint32_t*>(&target_buffer[5]) = 0x042444C7;
			*reinterpret_cast<uint32_t*>(&target_buffer[9]) = high32;

			target_buffer[13] = static_cast<char>(0xC3);
		}

		static bool ept_hook(void* target_function, void* hook_function, void** original_function)
		{
			if (KeGetCurrentIrql() > PASSIVE_LEVEL)
			{
				LOG_INFO("IRQL too high to perform EPT calculations");
				return false;
			}

			ZydisDecoder decoder;
			if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)))
			{
				LOG_INFO("Failed to initialize Zydis decoder for EPT hook");
				return false;
			}

			size_t overwritten_size = 0;
			const size_t offset_into_page = reinterpret_cast<size_t>(target_function) & 0xFFFULL;

			while (overwritten_size < absolute_jump_size)
			{
				ZydisDecodedInstruction instruction;
				if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, nullptr, reinterpret_cast<const void*>(reinterpret_cast<size_t>(target_function) + overwritten_size), page_size - (offset_into_page + overwritten_size) - 1, &instruction)))
				{
					LOG_INFO("Failed to decode instruction for EPT hook");
					return false;
				}

				overwritten_size += instruction.length;
			}

			if ((offset_into_page + overwritten_size) > (page_size - 1))
			{
				LOG_INFO("Function extends past a page boundary");
				return false;
			}

			auto* trampoline = static_cast<char*>(ExAllocatePool(NonPagedPool, overwritten_size + absolute_jump_size));
			if (!trampoline)
			{
				LOG_INFO("Could not allocate trampoline function buffer");
				return false;
			}

			RtlCopyMemory(trampoline, target_function, overwritten_size);
			write_absolute_jmp(&trampoline[overwritten_size], reinterpret_cast<size_t>(target_function) + overwritten_size);
			*original_function = trampoline;

			auto* exec_page = static_cast<char*>(ExAllocatePool(NonPagedPool, page_size));
			if (!exec_page)
			{
				LOG_INFO("Could not allocate executable shadow page buffer");
				return false;
			}

			RtlCopyMemory(exec_page, reinterpret_cast<void*>(reinterpret_cast<uint64_t>(target_function) & ~0xFFFull), page_size);
			write_absolute_jmp(&exec_page[offset_into_page], reinterpret_cast<size_t>(hook_function));

			const uint64_t target_pfn = MmGetPhysicalAddress(target_function).QuadPart >> 12;
			const uint64_t hook_pfn = MmGetPhysicalAddress(exec_page).QuadPart >> 12;

			auto success = true;
			utils::for_each_cpu(
				[&](size_t i)
				{
					if (hv::vmx::vmx_vmcall(hv::hypercall::ept_hook, target_pfn, hook_pfn) == 0)
					{
						LOG_ERROR("Failed to add EPT hook for target PFN 0x%llx in vCPU %lu", target_pfn, i);
						success = false;
					}
				});

			return success;
		}

		using mm_load_system_image_t = NTSTATUS(__fastcall*)(PUNICODE_STRING name, __int64 a2, __int64 a3, unsigned int a4, UINT64* a5, char** base);

		static mm_load_system_image_t original_mm_load_system_image = nullptr;

		static NTSTATUS __fastcall hk_mm_load_system_image(PUNICODE_STRING name, __int64 a2, __int64 a3, unsigned int a4, UINT64* a5, char** base)
		{
			auto result = original_mm_load_system_image(name, a2, a3, a4, a5, base);
			if (!NT_SUCCESS(result))
			{
				LOG_INFO("MmLoadSystemImage(original) failed with status %x", result);
				return result;
			}

			ANSI_STRING ansi_name{};
			if (!NT_SUCCESS(RtlUnicodeStringToAnsiString(&ansi_name, name, TRUE)))
			{
				LOG_INFO("Failed to convert image name to ANSI");
				return result;
			}

			if (ansi_name.Length > 512)
			{
				LOG_INFO("Image name is too long, skipping");
				RtlFreeAnsiString(&ansi_name);
				return result;
			}

			char image_name[512] = {0};
			int separator_index = -1;
			for (int i = static_cast<int>(ansi_name.Length) - 1; i >= 0; --i)
			{
				if (ansi_name.Buffer[i] == '\\')
				{
					separator_index = i;
					break;
				}
			}

			const unsigned short basename_offset = static_cast<unsigned short>(separator_index + 1);
			const unsigned short basename_length = ansi_name.Length - basename_offset;
			RtlCopyMemory(image_name, ansi_name.Buffer + basename_offset, basename_length);

			LOG_INFO("MmLoadSystemImage: Loading system image: %s, base: %p -> Status %x", image_name, *base, result);
			RtlFreeAnsiString(&ansi_name);

			// Check if this image matches the pending onload auto-trace target.
			bool trigger_onload = false;
			{
				sync::scoped_spin_lock guard(hv::g_hv.onload_target.lock);
				if (hv::g_hv.onload_target.active && names_match_nocase(image_name, hv::g_hv.onload_target.name))
				{
					// Clear the target so subsequent loads of the same image don't re-trigger.
					hv::g_hv.onload_target.active = false;
					trigger_onload = true;
				}
			}

			if (trigger_onload)
			{
				if (!base || !*base)
				{
					LOG_ERROR("onload auto-trace: loaded image base is null for %s", image_name);
					return result;
				}

				const size_t image_size = hv::pe::get_image_size(reinterpret_cast<const uint8_t*>(*base));
				if (image_size == 0)
				{
					LOG_ERROR("onload auto-trace: failed to read PE image size for %s, aborting auto-trace", image_name);
					return result;
				}

				bool at_success = true;
				utils::for_each_cpu(
					[&](size_t i)
					{
						if (!vmx::vmx_vmcall(hypercall::enable_auto_trace, reinterpret_cast<uint64_t>(*base), static_cast<uint64_t>(image_size)))
						{
							LOG_ERROR("onload auto-trace: failed to enable on vCPU %llu for %s", static_cast<uint64_t>(i), image_name);
							at_success = false;
						}
					});

				if (at_success)
				{
					LOG_INFO("onload auto-trace: enabled for %s (base: %p, size: 0x%llx)", image_name, *base, static_cast<uint64_t>(image_size));
				}
				else
				{
					LOG_ERROR("onload auto-trace: partially failed for %s", image_name);
				}
			}

			return result;
		}

		static bool install_mm_load_system_image_hook()
		{
			UNICODE_STRING mm_load_system_image_name = RTL_CONSTANT_STRING(L"MmLoadSystemImage");
			auto* mm_load_system_image = MmGetSystemRoutineAddress(&mm_load_system_image_name);
			if (!mm_load_system_image)
			{
				LOG_ERROR("Failed to get MmLoadSystemImage address, static EPT hook unavailable");
				return false;
			}

			if (!ept_hook(mm_load_system_image, reinterpret_cast<void*>(hk_mm_load_system_image), reinterpret_cast<void**>(&original_mm_load_system_image)))
			{
				LOG_ERROR("Failed to hook MmLoadSystemImage");
				return false;
			}

			LOG_INFO("Successfully hooked MmLoadSystemImage at address %p", mm_load_system_image);

			return true;
		}
	}  // namespace impl

	void initialize()
	{
		impl::install_mm_load_system_image_hook();
	}

}  // namespace hv::hooks