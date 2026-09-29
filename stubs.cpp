// What Flycast's sources compiled here call outside of themselves - see CMakeLists.txt. Filled from the
// linker's list of missing symbols, nothing more.
//
// NONE OF THESE IS REACHED WHEN AN ID IS READ. They belong to the running machine: the SH4 scheduler, the
// system bus registers, the interrupt controller, the GD-ROM drive, the Naomi board's id chip - reached by a
// cartridge's register reads and writes, its DMA, its save state, never by loading it and reading its boot id.
// The ones that are - the log, the translation of a message, a fatal error - do the least that is correct:
// nothing is logged, a message is kept as it is, a fatal error is an exception main() reports.

#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "types.h"
#include "emulator.h"
#include "cfg/option.h"
#include "log/Log.h"
#include "oslib/i18n.h"
#include "serialize.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/holly/holly_intc.h"
#include "hw/gdrom/gdromv3.h"
#include "hw/gdrom/gdrom_if.h"
#include "cfg/cfg.h"
#include "stdclass.h"

settings_t settings;

namespace config {
    // Asked by the GD-ROM cartridge whether to keep a digest for netplay: never.
    Option<bool> GGPOEnable("GGPO", false, "network");
    // What that option's own methods reach for: there is no emu.cfg here.
    bool loadBool(const std::string&, const std::string&, bool def) { return def; }
    void saveBool(const std::string&, const std::string&, bool) {}
    bool isTransient(const std::string&, const std::string&) { return false; }
    void deleteEntry(const std::string&, const std::string&) {}
}

// ── two of stdclass.cpp's: the rest of that file is the emulator's own folders ──
size_t get_last_slash_pos(const std::string& path)
{
    size_t a = path.find_last_of('/'), b = path.find_last_of('\\');
    return a == std::string::npos ? b : b == std::string::npos ? a : std::max(a, b);
}

std::string get_writable_config_path(const std::string& filename) { return filename; }

// ── the log, the messages, the fatal errors ─────────────────────────────
void GenericLog(LogTypes::LOG_LEVELS, LogTypes::LOG_TYPE, const char *, int, const char *, ...) {}

namespace i18n {
    const char *T(const char *msg) { return msg; }
    std::string Ts(const std::string& msg) { return msg; }
}

void fatal_error(const char *text, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, text);
    vsnprintf(buffer, sizeof(buffer), text, args);
    va_end(args);
    throw std::runtime_error(buffer);
}

[[noreturn]] void os_DebugBreak() { throw std::runtime_error("flycast: a check failed"); }

// ── the running machine: never reached here ──────────────────────────────
int sh4_sched_register(int, sh4_sched_callback *, void *) { return 0; }
void sh4_sched_unregister(int) {}
void sh4_sched_request(int, int) {}
bool sh4_sched_is_scheduled(int) { return false; }
void sh4_sched_serialize(Serializer&, int) {}
void sh4_sched_deserialize(Deserializer&, int) {}

u32 sb_regs[0x540];
void asic_RaiseInterrupt(HollyInterruptID) {}
void asic_CancelInterrupt(HollyInterruptID) {}

namespace addrspace {
    u32 DYNACALL read32(u32) { return 0; }
    void DYNACALL write32(u32, u32) {}
    void *writeConst(u32, bool& ismem, u32) { ismem = false; return nullptr; }
}

int sns_asc, sns_ascq, sns_key;
GD_SecNumbT SecNumber;
void gd_setdisc() {}

u16 NaomiGameIDRead() { return 0; }
void NaomiGameIDWrite(u16) {}
const u8 *getGameSerialId() { return nullptr; }
