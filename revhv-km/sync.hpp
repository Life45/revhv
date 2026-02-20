#pragma once
#include <intrin.h>

namespace sync
{
	struct atomic_int
	{
		volatile long value;
	};

	__forceinline long atomic_load(const atomic_int& v) noexcept
	{
		return _InterlockedCompareExchange(const_cast<volatile long*>(&v.value), 0, 0);
	}

	__forceinline void atomic_store(atomic_int& v, long new_value) noexcept
	{
		_InterlockedExchange(&v.value, new_value);
	}

	__forceinline long atomic_increment(atomic_int& v) noexcept
	{
		return _InterlockedIncrement(&v.value);
	}
	__forceinline long atomic_decrement(atomic_int& v) noexcept
	{
		return _InterlockedDecrement(&v.value);
	}

	__forceinline long atomic_compare_exchange(atomic_int& v, long new_value, long expected_value) noexcept
	{
		return _InterlockedCompareExchange(&v.value, new_value, expected_value);
	}
	class spin_lock
	{
	private:
		volatile long m_flag;  // 0 = unlocked, 1 = locked
	public:
		spin_lock() = default;
		spin_lock(const spin_lock&) = delete;
		spin_lock& operator=(const spin_lock&) = delete;

		__forceinline void lock() noexcept
		{
			while (_InterlockedCompareExchange(&m_flag, 1, 0) != 0)
			{
				_mm_pause();
			}
		}

		__forceinline bool try_lock() noexcept { return _InterlockedCompareExchange(&m_flag, 1, 0) == 0; }

		__forceinline void unlock() noexcept { _InterlockedExchange(&m_flag, 0); }
	};

	class scoped_spin_lock
	{
	private:
		spin_lock& m_lock;

	public:
		explicit scoped_spin_lock(spin_lock& l) noexcept : m_lock(l) { m_lock.lock(); }
		~scoped_spin_lock() noexcept { m_lock.unlock(); }

		scoped_spin_lock(const scoped_spin_lock&) = delete;
		scoped_spin_lock& operator=(const scoped_spin_lock&) = delete;
	};

	/// @brief Reentrant (recursive) spin lock safe for use in VMX-root.
	/// Uses the initial APIC ID (CPUID leaf 1, EBX[31:24]) biased by +1
	class reentrant_spin_lock
	{
	private:
		volatile long m_owner;	// 0 = unlocked, otherwise owner token
		volatile long m_depth;	// recursion depth

		static constexpr long UNLOCKED = 0;

		/// @brief Returns a per-core owner token that is guaranteed non-zero.
		static __forceinline long get_owner_id() noexcept
		{
			int regs[4];
			__cpuid(regs, 1);
			// EBX[31:24] = initial APIC ID; +1 so the BSP (APIC ID 0) is distinguishable from UNLOCKED
			return static_cast<long>((regs[1] >> 24) & 0xFF) + 1;
		}

	public:
		reentrant_spin_lock() = default;
		reentrant_spin_lock(const reentrant_spin_lock&) = delete;
		reentrant_spin_lock& operator=(const reentrant_spin_lock&) = delete;

		__forceinline void lock() noexcept
		{
			const long id = get_owner_id();

			// Fast path: already the owner — just bump the depth
			if (m_owner == id)
			{
				++m_depth;
				return;
			}

			// Acquire: spin until we swap UNLOCKED → id
			while (_InterlockedCompareExchange(&m_owner, id, UNLOCKED) != UNLOCKED)
			{
				_mm_pause();
			}
			m_depth = 1;
		}

		__forceinline bool try_lock() noexcept
		{
			const long id = get_owner_id();

			if (m_owner == id)
			{
				++m_depth;
				return true;
			}

			if (_InterlockedCompareExchange(&m_owner, id, UNLOCKED) == UNLOCKED)
			{
				m_depth = 1;
				return true;
			}
			return false;
		}

		__forceinline void unlock() noexcept
		{
			if (--m_depth == 0)
			{
				_InterlockedExchange(&m_owner, UNLOCKED);
			}
		}
	};

	class scoped_reentrant_spin_lock
	{
	private:
		reentrant_spin_lock& m_lock;

	public:
		explicit scoped_reentrant_spin_lock(reentrant_spin_lock& l) noexcept : m_lock(l) { m_lock.lock(); }
		~scoped_reentrant_spin_lock() noexcept { m_lock.unlock(); }

		scoped_reentrant_spin_lock(const scoped_reentrant_spin_lock&) = delete;
		scoped_reentrant_spin_lock& operator=(const scoped_reentrant_spin_lock&) = delete;
	};

}  // namespace sync