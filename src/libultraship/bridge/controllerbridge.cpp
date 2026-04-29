#include "libultraship/bridge/controllerbridge.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"

// Defensive null guards (Flotilla, 2026-04-28).
//
// History note: these guards were originally landed under the theory
// that #171 (recurring P2 crash on Inside Great Deku Tree room
// transitions) was a null-vtable deref of `GetInstance()` /
// `GetControlDeck()`. The release-build crash dump showed
// `ControllerUnblockGameInput` at the bottom of a 14-frame
// `0x0000000000000010` stack and the symbol resolution made this look
// like the obvious culprit.
//
// A subsequent debug-build repro (log 35, P2 Inside Deku Tree → Room 3)
// disproved the hypothesis. The debug stack showed 20 frames of the
// SAME repeated `CVarSetString` RIP — also unwinder-fallback noise,
// not real call frames. The actual root cause was a buffer overflow
// in the ENEMY_STATE joint/morph deserialization loop (one slot past
// the actor struct's allocated table), which corrupted EnDekubaba's
// `boundFloor` pointer at struct offset 0x234. The next floor
// collision check dereferenced the wild pointer and the Windows
// stack walker landed on whatever exported symbol happened to be
// nearest in the address space — which differed between Release and
// Debug builds.
//
// The guards are kept anyway. `Ship::Context::GetInstance()` and
// `GetControlDeck()` both return `std::shared_ptr<...>`; either being
// empty during teardown / mid-transition is a real risk class even
// though it wasn't the actual #171 trigger. Graceful no-op is the
// right behaviour either way.

extern "C" {

void ControllerBlockGameInput(uint16_t inputBlockId) {
    auto ctx = Ship::Context::GetInstance();
    if (ctx == nullptr) {
        return;
    }
    auto deck = ctx->GetControlDeck();
    if (deck == nullptr) {
        return;
    }
    deck->BlockGameInput(static_cast<int32_t>(inputBlockId));
}

void ControllerUnblockGameInput(uint16_t inputBlockId) {
    auto ctx = Ship::Context::GetInstance();
    if (ctx == nullptr) {
        return;
    }
    auto deck = ctx->GetControlDeck();
    if (deck == nullptr) {
        return;
    }
    deck->UnblockGameInput(static_cast<int32_t>(inputBlockId));
}
}
