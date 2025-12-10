namespace utils
{
	template <typename T>
	void set_bit(T& value, const uint64_t bit)
	{
		value |= (1ull << bit);
	}

	template <typename T>
	void clear_bit(T& value, const uint64_t bit)
	{
		value &= ~(1ull << bit);
	}

	constexpr uint64_t canonicalize(const uint64_t va)
	{
		constexpr uint64_t canonical_high = 0xFFFF000000000000ULL;
		constexpr uint64_t canonical_low = 0x0000FFFFFFFFFFFFULL;
		return (va & (1ULL << 47)) ? (va | canonical_high) : (va & canonical_low);
	}

	namespace segment
	{
		uint64_t base_address(const segment_descriptor_register_64& gdt, const segment_selector& selector)
		{
			//
			// 3.4.5 Segment Descriptors
			//

			segment_descriptor_32* descriptor = reinterpret_cast<segment_descriptor_32*>(gdt.base_address + selector.index * 8);

			// 3.5.2 Segment Descriptor Tables in IA-32e Mode
			if (descriptor->type == SEGMENT_DESCRIPTOR_TYPE_SYSTEM)
			{
				segment_descriptor_64* descriptor64 = reinterpret_cast<segment_descriptor_64*>(descriptor);
				uint64_t base_address = descriptor64->base_address_upper << 32 | descriptor64->base_address_high << 24 | descriptor64->base_address_middle << 16 | descriptor64->base_address_low;
				return base_address;
			}

			uint64_t base_address = descriptor->base_address_high << 24 | descriptor->base_address_middle << 16 | descriptor->base_address_low;
			return base_address;
		}

		uint32_t limit(const segment_descriptor_register_64& gdt, const segment_selector& selector)
		{
			//
			// 3.4.5 Segment Descriptors
			//

			segment_descriptor_32* descriptor = reinterpret_cast<segment_descriptor_32*>(gdt.base_address + selector.index * 8);

			// Since the limit isn't calculated differently for IA-32e Mode, we don't need to use segment_descriptor_64

			uint64_t limit = descriptor->segment_limit_high << 16 | descriptor->segment_limit_low;
			return limit;
		}

		vmx_segment_access_rights access_rights(const segment_descriptor_register_64& gdt, const segment_selector& selector)
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