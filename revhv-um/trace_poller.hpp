#pragma once

#include "includes.h"
#include "hypercall.h"
#include "logger.hpp"
#include "utils.hpp"
#include "../common/trace_log.hpp"

#include <atomic>
#include <thread>
#include <vector>
#include <fstream>
#include <filesystem>

namespace trace
{
	/// @brief Per-core polling thread pool that drains binary trace entries from the
	/// hypervisor and streams them to per-core binary files on disk.
	///
	/// Each worker thread is pinned to one logical core and issues flush_trace_logs
	/// hypercalls in a tight loop, writing raw trace::entry structs to trace_core_<N>.bin
	class poller
	{
	public:
		poller() = default;
		~poller() { stop(); }

		poller(const poller&) = delete;
		poller& operator=(const poller&) = delete;

		/// @brief Starts one polling thread per logical core.
		/// @param output_dir Directory where per-core binary files are written.
		/// @brief Registers a one-shot callback that fires the first time any worker drains a
		/// non-empty batch of trace entries. The callback is invoked from a worker thread.
		/// Must be called before start(). Resets automatically on each start().
		void set_on_first_data(std::function<void()> cb)
		{
			m_on_first_data = std::move(cb);
			m_first_data_fired.store(false, std::memory_order_release);
		}

		void start(const std::filesystem::path& output_dir = ".")
		{
			if (m_running.exchange(true))
				return;	 // already running

			SYSTEM_INFO info{};
			GetSystemInfo(&info);
			const uint32_t core_count = info.dwNumberOfProcessors;

			m_output_dir = output_dir;
			std::filesystem::create_directories(m_output_dir);

			m_first_data_fired.store(false, std::memory_order_release);

			m_threads.reserve(core_count);
			for (uint32_t i = 0; i < core_count; ++i)
				m_threads.emplace_back(&poller::worker, this, i);

			logger::info("Trace poller started ({} cores, output: {})", core_count, m_output_dir.string());
		}

		/// @brief Signals all worker threads to stop and waits for them to finish.
		void stop()
		{
			if (!m_running.exchange(false))
				return;	 // already stopped / never started

			for (auto& t : m_threads)
			{
				if (t.joinable())
					t.join();
			}
			m_threads.clear();

			logger::info("Trace poller stopped");
		}

		bool is_running() const { return m_running.load(std::memory_order_relaxed); }

		const std::filesystem::path& get_output_dir() const { return m_output_dir; }

	private:
		std::atomic_bool m_running{false};
		std::vector<std::thread> m_threads;
		std::filesystem::path m_output_dir;

		std::function<void()> m_on_first_data;
		std::atomic_bool m_first_data_fired{false};

		void worker(uint32_t core_id)
		{
			const auto prev_affinity = SetThreadAffinityMask(GetCurrentThread(), 1ull << core_id);

			auto filename = m_output_dir / std::format("trace_core_{}.bin", core_id);
			std::ofstream file(filename, std::ios::binary | std::ios::trunc);
			if (!file.is_open())
			{
				logger::error("Trace poller: failed to open {} for writing", filename.string());
				SetThreadAffinityMask(GetCurrentThread(), prev_affinity);
				return;
			}

			// Write file header
			trace::file_header hdr{};
			hdr.magic = trace::file_magic;
			hdr.version = trace::file_version;
			hdr.core_id = static_cast<uint16_t>(core_id);
			hdr.entry_size = static_cast<uint16_t>(sizeof(trace::entry));
			hdr.entry_count = trace::ring_buffer_entry_count;
			file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

			// Scratch buffer for one batch of entries
			std::vector<trace::entry> batch(trace::max_flush_entries);

			uint64_t total_written = 0;

			while (m_running.load(std::memory_order_relaxed))
			{
				uint64_t count = hv::hypercall::drain_trace_logs(core_id, batch.data(), static_cast<uint64_t>(batch.size()));

				if (count > 0)
				{
					file.write(reinterpret_cast<const char*>(batch.data()), static_cast<std::streamsize>(count * sizeof(trace::entry)));

					total_written += count;

					// Fire the first-data callback on the very first non-empty drain
					if (!m_first_data_fired.exchange(true, std::memory_order_acq_rel))
					{
						if (m_on_first_data)
							m_on_first_data();
					}

					// Keep going immediately if the batch was full (more data likely pending)
					if (count == batch.size())
						continue;
				}

				// Nothing to drain (or partial batch) — yield briefly to avoid busy-spinning
				std::this_thread::sleep_for(std::chrono::microseconds(500));
			}

			// Final drain: pick up any remaining entries after stop() was requested
			for (;;)
			{
				uint64_t count = hv::hypercall::drain_trace_logs(core_id, batch.data(), static_cast<uint64_t>(batch.size()));

				if (count == 0)
					break;

				file.write(reinterpret_cast<const char*>(batch.data()), static_cast<std::streamsize>(count * sizeof(trace::entry)));
				total_written += count;
			}

			file.flush();
			file.close();
			SetThreadAffinityMask(GetCurrentThread(), prev_affinity);

			logger::info("Trace poller core {}: wrote {} entries to {}", core_id, total_written, filename.string());
		}
	};

}  // namespace trace
