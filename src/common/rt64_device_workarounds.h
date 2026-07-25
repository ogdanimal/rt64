//
// RT64
//

#pragma once

namespace RT64 {
    // Process-wide switches for working around driver bugs that RT64 cannot detect
    // reliably and cannot fix. Deliberately global rather than carried in
    // UserConfiguration: the sites that read them sit deep inside command recording
    // (NativeTarget) with no path to the configuration, and threading one through
    // every intermediate layer would widen the fork's diff against upstream for a
    // value that is constant for the lifetime of a frame.
    //
    // Everything here defaults to the standard, correct behaviour, so a host that
    // never calls these setters gets stock RT64.

    // Framebuffer RAM<->native synchronisation, i.e. the two compute dispatches that
    // convert between emulated RDRAM and the GPU's render targets
    // (NativeTarget::copyFromRAM and NativeTarget::copyToNative).
    //
    // On some Qualcomm Adreno drivers -- confirmed on the 0x801e... branch shipped
    // with the Adreno 630 -- vkCmdDispatch for these two shaders dereferences null
    // inside the driver at command RECORDING time, killing the process about a
    // second after the game starts rendering. Validation (core and synchronisation)
    // is clean at the crash site, and the same binary does not crash under Mesa
    // Turnip on the same device, so the fault is in the driver rather than in RT64's
    // usage.
    //
    // Disabling this skips only those two dispatches. Everything around them --
    // barriers, descriptor sets, pipeline binding, the readback copies -- still runs,
    // which is exactly the configuration that was measured on device. The cost is
    // that anything the game reads back from, or blits into, a framebuffer stops
    // being synchronised, so framebuffer-dependent effects may render incorrectly or
    // not at all. It is a last resort for hardware that cannot run the game at all
    // otherwise, not a performance option.
    void SetFramebufferSyncEnabled(bool enabled);
    bool GetFramebufferSyncEnabled();
};
