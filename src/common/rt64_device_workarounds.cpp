//
// RT64
//

#include "rt64_device_workarounds.h"

#include <atomic>
#include <cstdio>

namespace RT64 {
    // Atomic because the host sets it from the thread that applies a configuration
    // change while the render thread reads it during command recording. Relaxed
    // ordering is enough: nothing else is published alongside it.
    //
    // THE RACE WORTH THINKING ABOUT, since this file also says gating one direction
    // and not the other is the bug: a flip can land MID-FRAME, between the
    // copyFromRAM read and the copyToNative writeback, producing a single frame in
    // exactly that mixed configuration. It is still safe, and the reason is that the
    // crash comes from EXECUTING a dispatch, not from omitting its partner:
    //   - flipping to Off can only ever skip work, so a mixed frame is strictly
    //     safer than the frame before it;
    //   - flipping to On resumes dispatches, so a broken device crashes -- but it
    //     was going to crash on the very next full frame anyway, which is precisely
    //     what the user asked for by turning the workaround off.
    // On healthy hardware a mixed frame is at worst one frame of stale framebuffer
    // content. So no lock, and no attempt to make the pair atomic with each other.
    static std::atomic<bool> framebufferSyncEnabled = true;

    void SetFramebufferSyncEnabled(bool enabled) {
        // Logged on every change, because a report of "effects look wrong" is
        // otherwise indistinguishable from a real rendering bug -- this is the one
        // line that says the user turned the workaround on. Only on change: the host
        // re-applies the whole configuration whenever any graphics option moves.
        // The first call always logs, so the absence of this line never has to be
        // interpreted -- a log that only appears when the workaround is on cannot
        // distinguish "sync enabled" from "this build predates the setting".
        // Deliberately a plain bool, not an atomic: the setter has a single caller on
        // a single thread (the host's configuration-apply path). If that ever stops
        // being true the worst case is a duplicated log line, not a data race that
        // matters -- but it would then be wrong to reason about, so make it atomic
        // rather than widening this assumption.
        static bool everLogged = false;
        const bool previous = framebufferSyncEnabled.exchange(enabled, std::memory_order_relaxed);
        if (!everLogged || (previous != enabled)) {
            everLogged = true;
            fprintf(stderr, "[rt64] framebuffer RAM<->native sync %s\n", enabled ? "ENABLED" : "DISABLED (driver workaround: framebuffer effects will be inaccurate)");
        }
    }

    bool GetFramebufferSyncEnabled() {
        return framebufferSyncEnabled.load(std::memory_order_relaxed);
    }
};
