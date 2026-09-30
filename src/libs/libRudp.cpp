#include "common/abi.h"
#include "common/logging/log.h"
#include "common/loadDiagnostics.h"
#include "libs/errno.h"
#include "libs/libs.h"
#include "loader/symbolDatabase.h"

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Libs {

LIB_VERSION("Rudp", 1, "Rudp", 1, 1);

namespace Rudp {

using RudpEventHandler = void (*)(int ctx_id, int event_id, int error_code, void* arg);

static RudpEventHandler g_event_handler = nullptr;
static void*            g_event_arg     = nullptr;
static bool             g_initialized   = false;

enum class StatusMode { Pass, Zero, One };

static StatusMode GetStatusMode() {
	static const StatusMode mode = [] {
		const char* value = std::getenv("KYTY_RUDP_STATUS_MODE");
		if (value != nullptr && std::strcmp(value, "zero") == 0) {
			return StatusMode::Zero;
		}
		if (value != nullptr && std::strcmp(value, "one") == 0) {
			return StatusMode::One;
		}
		return StatusMode::Pass;
	}();
	return mode;
}

// HFW's eboot calls this with a 0xf8-byte output buffer. The field layout is not known yet.
static KYTY_SYSV_ABI int RudpGetStatus(void* status, uint64_t size) {
	LoadDiagnostics::RudpStatusCalled();
	const auto mode = GetStatusMode();
	static std::atomic_bool first_call {true};
	if (first_call.exchange(false)) {
		std::printf("RUDP GetStatus experiment: mode=%s size=%" PRIu64 " buffer=%p\n",
		            mode == StatusMode::Zero ? "zero" : mode == StatusMode::One ? "one" : "pass",
		            size, status);
	}
	if (mode != StatusMode::Pass) {
		if (status == nullptr || size != 0xf8) {
			return -1;
		}
		std::memset(status, 0, static_cast<size_t>(size));
		if (mode == StatusMode::One) {
			const uint32_t first_field = 1;
			std::memcpy(status, &first_field, sizeof(first_field));
		}
	}
	return OK;
}

static KYTY_SYSV_ABI int RudpInit(void* mem_pool, int mem_pool_size) {
	PRINT_NAME();

	LOGF("\t mem_pool      = 0x%016" PRIx64 "\n"
	     "\t mem_pool_size = %d\n",
	     reinterpret_cast<uint64_t>(mem_pool), mem_pool_size);

	g_initialized = true;

	return OK;
}

static KYTY_SYSV_ABI int RudpEnableInternalIOThread(uint32_t stack_size, uint32_t priority) {
	PRINT_NAME();

	LOGF("\t stack_size = %" PRIu32 "\n"
	     "\t priority   = %" PRIu32 "\n",
	     stack_size, priority);

	return OK;
}

static KYTY_SYSV_ABI int RudpSetEventHandler(RudpEventHandler handler, void* arg) {
	PRINT_NAME();

	LOGF("\t handler = 0x%016" PRIx64 "\n"
	     "\t arg     = 0x%016" PRIx64 "\n",
	     reinterpret_cast<uint64_t>(handler), reinterpret_cast<uint64_t>(arg));

	g_event_handler = handler;
	g_event_arg     = arg;

	return OK;
}

} // namespace Rudp

LIB_DEFINE(InitRudp_1) {
	LIB_FUNC("i3STzxuwPx0", Rudp::RudpGetStatus);
	LIB_FUNC("amuBfI-AQc4", Rudp::RudpInit);
	LIB_FUNC("6PBNpsgyaxw", Rudp::RudpEnableInternalIOThread);
	LIB_FUNC("SUEVes8gvmw", Rudp::RudpSetEventHandler);
}

} // namespace Libs
