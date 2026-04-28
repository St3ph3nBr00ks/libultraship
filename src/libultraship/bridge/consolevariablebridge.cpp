#include "libultraship/bridge/consolevariablebridge.h"
#include "ship/Context.h"
#include <spdlog/spdlog.h>
#include <cstdlib>

// KB-19 / #171 Experiment 4 — recursion-depth instrumentation for CVarSetString.
// The #171 Deku Tree crash signature is 17+ stack frames of CVarSetString
// before SEGV at RIP that does not correspond to this function's body. Two
// hypotheses:
//   (a) Real recursion via a callback that re-enters CVarSetString. If true,
//       sCVarSetStringDepth reaches >1 at runtime.
//   (b) Stack-walker artifact: unwinder falls into CVarSetString's small
//       address range when the real frames are corrupted. If true,
//       sCVarSetStringDepth never exceeds 1 even when the crash still
//       reproduces.
// This counter is thread_local because CVar reads/writes can fire on the
// receive thread (Anchor packet handlers) AND the main thread.
static thread_local int sCVarSetStringDepth = 0;
static thread_local int sCVarSetStringPeakDepth = 0;

namespace {
struct CVarSetStringDepthGuard {
    CVarSetStringDepthGuard(const char* name, const char* value) : mName(name) {
        ++sCVarSetStringDepth;
        if (sCVarSetStringDepth > sCVarSetStringPeakDepth) {
            sCVarSetStringPeakDepth = sCVarSetStringDepth;
        }
        // Log the first time we cross each new depth threshold within a thread.
        // 5+ is suspicious; 16+ is almost certainly real recursion (#171).
        if (sCVarSetStringDepth >= 5 && sCVarSetStringDepth == sCVarSetStringPeakDepth) {
            SPDLOG_CRITICAL("[KB19/171] CVarSetString depth={} name=\"{}\" value=\"{}\" — possible recursion",
                            sCVarSetStringDepth, name ? name : "<null>", value ? value : "<null>");
        }
        if (sCVarSetStringDepth >= 32) {
            SPDLOG_CRITICAL("[KB19/171] CVarSetString depth={} — aborting before stack overflow",
                            sCVarSetStringDepth);
            std::abort();
        }
    }
    ~CVarSetStringDepthGuard() {
        --sCVarSetStringDepth;
    }
    const char* mName;
};
} // namespace

std::shared_ptr<Ship::CVar> CVarGet(const char* name) {
    return Ship::Context::GetInstance()->GetConsoleVariables()->Get(name);
}

extern "C" {
int32_t CVarGetInteger(const char* name, int32_t defaultValue) {
    return Ship::Context::GetInstance()->GetConsoleVariables()->GetInteger(name, defaultValue);
}

float CVarGetFloat(const char* name, float defaultValue) {
    return Ship::Context::GetInstance()->GetConsoleVariables()->GetFloat(name, defaultValue);
}

const char* CVarGetString(const char* name, const char* defaultValue) {
    return Ship::Context::GetInstance()->GetConsoleVariables()->GetString(name, defaultValue);
}

Color_RGBA8 CVarGetColor(const char* name, Color_RGBA8 defaultValue) {
    return Ship::Context::GetInstance()->GetConsoleVariables()->GetColor(name, defaultValue);
}

Color_RGB8 CVarGetColor24(const char* name, Color_RGB8 defaultValue) {
    return Ship::Context::GetInstance()->GetConsoleVariables()->GetColor24(name, defaultValue);
}

void CVarSetInteger(const char* name, int32_t value) {
    Ship::Context::GetInstance()->GetConsoleVariables()->SetInteger(name, value);
}

void CVarSetFloat(const char* name, float value) {
    Ship::Context::GetInstance()->GetConsoleVariables()->SetFloat(name, value);
}

void CVarSetString(const char* name, const char* value) {
    CVarSetStringDepthGuard depthGuard(name, value);
    Ship::Context::GetInstance()->GetConsoleVariables()->SetString(name, value);
}

void CVarSetColor(const char* name, Color_RGBA8 value) {
    Ship::Context::GetInstance()->GetConsoleVariables()->SetColor(name, value);
}

void CVarSetColor24(const char* name, Color_RGB8 value) {
    Ship::Context::GetInstance()->GetConsoleVariables()->SetColor24(name, value);
}

void CVarRegisterInteger(const char* name, int32_t defaultValue) {
    Ship::Context::GetInstance()->GetConsoleVariables()->RegisterInteger(name, defaultValue);
}

void CVarRegisterFloat(const char* name, float defaultValue) {
    Ship::Context::GetInstance()->GetConsoleVariables()->RegisterFloat(name, defaultValue);
}

void CVarRegisterString(const char* name, const char* defaultValue) {
    Ship::Context::GetInstance()->GetConsoleVariables()->RegisterString(name, defaultValue);
}

void CVarRegisterColor(const char* name, Color_RGBA8 defaultValue) {
    Ship::Context::GetInstance()->GetConsoleVariables()->RegisterColor(name, defaultValue);
}

void CVarRegisterColor24(const char* name, Color_RGB8 defaultValue) {
    Ship::Context::GetInstance()->GetConsoleVariables()->RegisterColor24(name, defaultValue);
}

void CVarClear(const char* name) {
    Ship::Context::GetInstance()->GetConsoleVariables()->ClearVariable(name);
}

void CVarClearBlock(const char* name) {
    Ship::Context::GetInstance()->GetConsoleVariables()->ClearBlock(name);
}

void CVarCopy(const char* from, const char* to) {
    Ship::Context::GetInstance()->GetConsoleVariables()->CopyVariable(from, to);
}

void CVarLoad() {
    Ship::Context::GetInstance()->GetConsoleVariables()->Load();
}

void CVarSave() {
    Ship::Context::GetInstance()->GetConsoleVariables()->Save();
}
}
