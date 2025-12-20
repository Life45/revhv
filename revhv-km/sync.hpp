#pragma once
#include <intrin.h>

namespace sync
{
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