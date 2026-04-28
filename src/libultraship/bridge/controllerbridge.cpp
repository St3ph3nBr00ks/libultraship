#include "libultraship/bridge/controllerbridge.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"

// Defensive null guards (Flotilla #171 mitigation, 2026-04-28).
//
// `Plans/issue_171_room_unload_race.md` hypothesis 5 deep-dive
// identified this file's unguarded `Ship::Context::GetInstance()->
// GetControlDeck()->BlockGameInput(...)` / `UnblockGameInput(...)`
// chain as the most likely null-vtable-deref site for the recurring
// P2 crash on Inside Great Deku Tree Room 10 transitions. Stack
// signature is 14 frames of `0x0000000000000010` (corrupt unwind)
// terminating in `ControllerUnblockGameInput`. Both `GetInstance()`
// and `GetControlDeck()` return `std::shared_ptr<...>`; either being
// empty during teardown / mid-transition produces a deref-null
// crash here. Guard both levels and graceful-fail.
//
// If the crash STOPS reproducing after this lands, hypothesis is
// confirmed and the upstream fix can be filed at libultraship.
// If the crash CONTINUES, root cause is elsewhere — symbolicate the
// next dump via a RelWithDebInfo build.

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
