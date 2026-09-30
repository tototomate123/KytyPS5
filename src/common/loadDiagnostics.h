#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace LoadDiagnostics {

inline std::atomic<bool> hfw_gpu_trace_enabled {false};

struct AprActivity {
	std::chrono::steady_clock::time_point start          = std::chrono::steady_clock::now();
	uint64_t                              command_buffer = 0;
	uint64_t                              record_offset  = 0;
	const char*                           operation      = "decode";
	std::string                           path;
};

// Opt-in, low-volume progress counters for diagnosing a guest loading screen.
struct State {
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	std::atomic<int64_t>                  last_report_ms {0};
	std::atomic<uint64_t>                 opens {0};
	std::atomic<uint64_t>                 open_failures {0};
	std::atomic<uint64_t>                 stats {0};
	std::atomic<uint64_t>                 stat_failures {0};
	std::atomic<uint64_t>                 reads {0};
	std::atomic<uint64_t>                 read_bytes {0};
	std::atomic<uint64_t>                 shaders {0};
	std::atomic<uint64_t>                 flips {0};
	std::atomic<uint64_t>                 rudp_status {0};
	std::atomic<bool>                     rudp_seen {false};
	std::atomic<uint64_t>                 sockets {0};
	std::atomic<uint64_t>                 udp_sockets {0};
	std::atomic<uint64_t>                 connects {0};
	std::atomic<uint64_t>                 sends {0};
	std::mutex                            latest_file_mutex;
	std::string                           latest_file;
	std::string                           latest_failed_path;
	std::mutex                            apr_mutex;
	uint64_t                              apr_next_id = 0;
	uint64_t                              apr_started = 0, apr_completed = 0, apr_errors = 0;
	uint64_t                              apr_resolves = 0, apr_resolve_failures = 0;
	uint64_t    apr_reads = 0, apr_requested = 0, apr_bytes = 0, apr_short_reads = 0;
	uint64_t    apr_events = 0, apr_writes = 0, apr_unsupported_sync = 0;
	uint64_t    apr_error_logs = 0;
	std::string apr_last_read;
	std::unordered_map<uint64_t, AprActivity> apr_active;
	std::unordered_set<std::string>           apr_reported_sync;
};

inline bool Enabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_LOAD_DIAGNOSTICS");
		return value != nullptr && value[0] != '\0' && value[0] != '0';
	}();
	return enabled;
}

inline State& GetState() {
	static State state;
	return state;
}

inline void MaybeReport(bool first_rudp_status = false) {
	if (!Enabled()) {
		return;
	}
	auto&      state       = GetState();
	const auto elapsed_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
	                             std::chrono::steady_clock::now() - state.start)
	                             .count();
	auto       previous_ms = state.last_report_ms.load(std::memory_order_relaxed);
	if (!first_rudp_status && elapsed_ms - previous_ms < 10000) {
		return;
	}
	if (!state.last_report_ms.compare_exchange_strong(previous_ms, elapsed_ms,
	                                                  std::memory_order_relaxed)) {
		return;
	}
	std::string latest_file;
	std::string latest_failed_path;
	{
		std::lock_guard lock(state.latest_file_mutex);
		latest_file.swap(state.latest_file);
		latest_failed_path.swap(state.latest_failed_path);
	}
	std::printf("Load diagnostics +%llds (last %llds): opens=%llu open_failures=%llu stats=%llu "
	            "stat_failures=%llu reads=%llu bytes=%llu "
	            "shaders=%llu flips=%llu rudp_status=%llu sockets=%llu udp_sockets=%llu "
	            "connects=%llu sends=%llu "
	            "last_read=%s last_failed_path=%s\n",
	            static_cast<long long>(elapsed_ms / 1000),
	            static_cast<long long>((elapsed_ms - previous_ms) / 1000),
	            static_cast<unsigned long long>(state.opens.exchange(0)),
	            static_cast<unsigned long long>(state.open_failures.exchange(0)),
	            static_cast<unsigned long long>(state.stats.exchange(0)),
	            static_cast<unsigned long long>(state.stat_failures.exchange(0)),
	            static_cast<unsigned long long>(state.reads.exchange(0)),
	            static_cast<unsigned long long>(state.read_bytes.exchange(0)),
	            static_cast<unsigned long long>(state.shaders.exchange(0)),
	            static_cast<unsigned long long>(state.flips.exchange(0)),
	            static_cast<unsigned long long>(state.rudp_status.exchange(0)),
	            static_cast<unsigned long long>(state.sockets.exchange(0)),
	            static_cast<unsigned long long>(state.udp_sockets.exchange(0)),
	            static_cast<unsigned long long>(state.connects.exchange(0)),
	            static_cast<unsigned long long>(state.sends.exchange(0)),
	            latest_file.empty() ? "<none>" : latest_file.c_str(),
	            latest_failed_path.empty() ? "<none>" : latest_failed_path.c_str());
	{
		std::lock_guard lock(state.apr_mutex);
		std::printf("APR diagnostics: started=%llu completed=%llu active=%zu errors=%llu "
		            "resolves=%llu resolve_failures=%llu reads=%llu requested=%llu bytes=%llu "
		            "short_reads=%llu events=%llu writes=%llu unsupported_sync=%llu last_read=%s\n",
		            static_cast<unsigned long long>(state.apr_started),
		            static_cast<unsigned long long>(state.apr_completed), state.apr_active.size(),
		            static_cast<unsigned long long>(state.apr_errors),
		            static_cast<unsigned long long>(state.apr_resolves),
		            static_cast<unsigned long long>(state.apr_resolve_failures),
		            static_cast<unsigned long long>(state.apr_reads),
		            static_cast<unsigned long long>(state.apr_requested),
		            static_cast<unsigned long long>(state.apr_bytes),
		            static_cast<unsigned long long>(state.apr_short_reads),
		            static_cast<unsigned long long>(state.apr_events),
		            static_cast<unsigned long long>(state.apr_writes),
		            static_cast<unsigned long long>(state.apr_unsupported_sync),
		            state.apr_last_read.empty() ? "<none>" : state.apr_last_read.c_str());
		for (const auto& [id, activity]: state.apr_active) {
			const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
			                     std::chrono::steady_clock::now() - activity.start)
			                     .count();
			std::printf(
			    "APR active: trace=%llu buffer=0x%llx age_ms=%lld op=%s record=0x%llx path=%s\n",
			    static_cast<unsigned long long>(id),
			    static_cast<unsigned long long>(activity.command_buffer),
			    static_cast<long long>(age), activity.operation,
			    static_cast<unsigned long long>(activity.record_offset), activity.path.c_str());
		}
		state.apr_started = state.apr_completed = state.apr_errors = 0;
		state.apr_resolves = state.apr_resolve_failures = 0;
		state.apr_reads = state.apr_requested = state.apr_bytes = state.apr_short_reads = 0;
		state.apr_events = state.apr_writes = state.apr_unsupported_sync = 0;
		state.apr_last_read.clear();
	}
}

// APR executes synchronously in the host implementation. Track each invocation so a
// blocked read/map/event operation remains visible from the presentation heartbeat.
class AprTrace {
public:
	explicit AprTrace(uint64_t command_buffer) {
		if (!Enabled()) {
			return;
		}
		auto&           state = GetState();
		std::lock_guard lock(state.apr_mutex);
		m_id = ++state.apr_next_id;
		state.apr_started++;
		state.apr_active[m_id].command_buffer = command_buffer;
		if (m_id <= 16) {
			std::printf("APR begin: trace=%llu buffer=0x%llx\n",
			            static_cast<unsigned long long>(m_id),
			            static_cast<unsigned long long>(command_buffer));
		}
	}
	void Step(const char* operation, uint64_t offset, const std::string& path = {}) {
		if (m_id == 0) {
			return;
		}
		auto&           state = GetState();
		std::lock_guard lock(state.apr_mutex);
		auto&           activity = state.apr_active.at(m_id);
		activity.operation       = operation;
		activity.record_offset   = offset;
		activity.path            = path;
	}
	void Read(uint32_t file_id, uint64_t offset, uint64_t requested, uint64_t bytes, int result) {
		if (m_id == 0) {
			return;
		}
		auto&           state = GetState();
		std::lock_guard lock(state.apr_mutex);
		state.apr_reads++;
		state.apr_requested += requested;
		state.apr_bytes += bytes;
		state.apr_short_reads += (bytes < requested);
		state.apr_last_read = state.apr_active.at(m_id).path;
		if ((result != 0 || bytes != requested) && state.apr_error_logs++ < 32) {
			std::printf("APR read anomaly: trace=%llu file_id=0x%x offset=0x%llx "
			            "requested=%llu returned=%llu result=0x%x path=%s\n",
			            static_cast<unsigned long long>(m_id), file_id,
			            static_cast<unsigned long long>(offset),
			            static_cast<unsigned long long>(requested),
			            static_cast<unsigned long long>(bytes), static_cast<unsigned>(result),
			            state.apr_last_read.c_str());
		}
	}
	void Signal(bool event) {
		if (m_id == 0) {
			return;
		}
		auto&           state = GetState();
		std::lock_guard lock(state.apr_mutex);
		(event ? state.apr_events : state.apr_writes)++;
	}
	void Finish(int result, uint32_t error_offset) {
		if (m_id == 0) {
			return;
		}
		auto&           state = GetState();
		std::lock_guard lock(state.apr_mutex);
		state.apr_completed++;
		state.apr_errors += (result != 0);
		if (m_id <= 16 || (result != 0 && state.apr_error_logs++ < 32)) {
			const auto& activity = state.apr_active.at(m_id);
			std::printf("APR end: trace=%llu result=0x%x error_offset=0x%x last_op=%s path=%s\n",
			            static_cast<unsigned long long>(m_id), static_cast<unsigned>(result),
			            error_offset, activity.operation, activity.path.c_str());
		}
		state.apr_active.erase(m_id);
	}

private:
	uint64_t m_id = 0;
};

inline void AprResolve(const char* path, int result) {
	if (!Enabled()) {
		return;
	}
	auto&           state = GetState();
	std::lock_guard lock(state.apr_mutex);
	state.apr_resolves++;
	state.apr_resolve_failures += (result != 0);
	if (result != 0 && state.apr_error_logs++ < 32) {
		std::printf("APR resolve failed: result=0x%x path=%s\n", static_cast<unsigned>(result),
		            path);
	}
}

inline void AprUnsupportedSync(const char* function) {
	if (!Enabled()) {
		return;
	}
	auto&           state = GetState();
	std::lock_guard lock(state.apr_mutex);
	state.apr_unsupported_sync++;
	if (state.apr_reported_sync.emplace(function).second) {
		std::printf("APR unsupported synchronization called: %s\n", function);
	}
}

inline void FileOpen() {
	if (Enabled()) {
		GetState().opens.fetch_add(1, std::memory_order_relaxed);
		MaybeReport();
	}
}

inline void FileOpenFailed(const char* path) {
	if (Enabled()) {
		auto& state = GetState();
		state.open_failures.fetch_add(1, std::memory_order_relaxed);
		{
			std::lock_guard lock(state.latest_file_mutex);
			state.latest_failed_path = path;
		}
		MaybeReport();
	}
}

inline void FileStat(const char* path, bool found) {
	if (Enabled()) {
		auto& state = GetState();
		state.stats.fetch_add(1, std::memory_order_relaxed);
		if (!found) {
			state.stat_failures.fetch_add(1, std::memory_order_relaxed);
			std::lock_guard lock(state.latest_file_mutex);
			state.latest_failed_path = path;
		}
		MaybeReport();
	}
}

inline void FileRead(const char* path, uint64_t bytes) {
	if (Enabled() && bytes != 0) {
		auto& state = GetState();
		state.reads.fetch_add(1, std::memory_order_relaxed);
		state.read_bytes.fetch_add(bytes, std::memory_order_relaxed);
		{
			std::lock_guard lock(state.latest_file_mutex);
			state.latest_file = path;
		}
		MaybeReport();
	}
}

inline void ShaderCompiled() {
	if (Enabled()) {
		GetState().shaders.fetch_add(1, std::memory_order_relaxed);
		MaybeReport();
	}
}

inline void FlipCompleted() {
	if (Enabled()) {
		GetState().flips.fetch_add(1, std::memory_order_relaxed);
		MaybeReport();
	}
}

inline void RudpStatusCalled() {
	if (Enabled()) {
		auto& state = GetState();
		state.rudp_status.fetch_add(1, std::memory_order_relaxed);
		const bool first = !state.rudp_seen.exchange(true, std::memory_order_relaxed);
		MaybeReport(first);
	}
}

inline void SocketCreated(bool datagram) {
	if (Enabled()) {
		auto& state = GetState();
		state.sockets.fetch_add(1, std::memory_order_relaxed);
		if (datagram) {
			state.udp_sockets.fetch_add(1, std::memory_order_relaxed);
		}
		MaybeReport();
	}
}

inline void ConnectAttempted() {
	if (Enabled()) {
		GetState().connects.fetch_add(1, std::memory_order_relaxed);
		MaybeReport();
	}
}

inline void NetworkSend() {
	if (Enabled()) {
		GetState().sends.fetch_add(1, std::memory_order_relaxed);
		MaybeReport();
	}
}

} // namespace LoadDiagnostics
