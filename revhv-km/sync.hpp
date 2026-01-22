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
		volatile long m_flag = 0;  // 0 = unlocked, 1 = locked
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

}  // namespace sync