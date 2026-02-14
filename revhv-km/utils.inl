namespace utils
{
	template <typename T>
	inline void set_bit(T& value, const uint64_t bit)
	{
		value |= (1ull << bit);
	}

	template <typename T>
	inline void clear_bit(T& value, const uint64_t bit)
	{
		value &= ~(1ull << bit);
	}

	inline bool is_bit_set(const uint64_t value, const uint64_t bit)
	{
		return (value & (1ull << bit)) != 0;
	}

	constexpr uint64_t canonicalize(const uint64_t va)
	{
		constexpr uint64_t canonical_high = 0xFFFF000000000000ULL;
		constexpr uint64_t canonical_low = 0x0000FFFFFFFFFFFFULL;
		return (va & (1ULL << 47)) ? (va | canonical_high) : (va & canonical_low);
	}

	inline void memset(void* dest, uint8_t value, size_t size)
	{
		__stosb(static_cast<uint8_t*>(dest), value, size);
	}

	inline void memcpy(void* dest, const void* src, size_t size)
	{
		__movsb(static_cast<uint8_t*>(dest), static_cast<const uint8_t*>(src), size);
	}

	template <typename Func>
	inline void for_each_cpu(Func func)
	{
		auto logicalProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
		for (uint32_t i = 0; i < logicalProcessorCount; i++)
		{
			auto const orig_affinity = KeSetSystemAffinityThreadEx(1ull << i);

			func(i);

			KeRevertToUserAffinityThreadEx(orig_affinity);
		}
	}

	namespace segment
	{
		inline uint64_t base_address(const segment_descriptor_register_64& gdt, const segment_selector& selector)
		{
			//
			// 3.4.5 Segment Descriptors
			//

			if (selector.index == 0)
				return 0;

			segment_descriptor_32* descriptor = reinterpret_cast<segment_descriptor_32*>(gdt.base_address + selector.index * 8);

			// 3.5.2 Segment Descriptor Tables in IA-32e Mode
			if (descriptor->descriptor_type == SEGMENT_DESCRIPTOR_TYPE_SYSTEM)
			{
				segment_descriptor_64* descriptor64 = reinterpret_cast<segment_descriptor_64*>(descriptor);

				uint64_t base_address = static_cast<uint64_t>(descriptor64->base_address_upper) << 32 | static_cast<uint64_t>(descriptor64->base_address_high) << 24 | static_cast<uint64_t>(descriptor64->base_address_middle) << 16 | static_cast<uint64_t>(descriptor64->base_address_low);
				return base_address;
			}

			uint64_t base_address = static_cast<uint64_t>(descriptor->base_address_high) << 24 | static_cast<uint64_t>(descriptor->base_address_middle) << 16 | static_cast<uint64_t>(descriptor->base_address_low);
			return base_address;
		}

		inline vmx_segment_access_rights access_rights(const segment_descriptor_register_64& gdt, const segment_selector& selector)
		{
			//
			// 3.4.5 Segment Descriptors & 26.4.1 Guest Register State
			//

			segment_descriptor_32* descriptor = reinterpret_cast<segment_descriptor_32*>(gdt.base_address + selector.index * 8);

			vmx_segment_access_rights access_rights = {0};

			access_rights.type = descriptor->type;
			access_rights.descriptor_type = descriptor->descriptor_type;
			access_rights.descriptor_privilege_level = descriptor->descriptor_privilege_level;
			access_rights.present = descriptor->present;
			access_rights.available_bit = descriptor->system;
			access_rights.long_mode = descriptor->long_mode;
			access_rights.default_big = descriptor->default_big;
			access_rights.granularity = descriptor->granularity;
			access_rights.unusable = (selector.index == 0);
			return access_rights;
		}
	}  // namespace segment
}  // namespace utils