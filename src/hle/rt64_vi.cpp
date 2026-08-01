//
// RT64
//

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory.h>
#include <stdio.h>

#include "common/rt64_common.h"
#include "gbi/rt64_f3d.h"

#include "rt64_vi.h"

namespace RT64 {
    // VI
    
    hlslpp::float4 VI::viewRectangle() const {
        return { 0.0f, 0.0f, 1.0f, 1.0f };
    }

    hlslpp::float4 VI::cropRectangle() const {
        return { 0.0f, 0.0f, 1.0f, 1.0f };
    }

    float VI::gamma() const {
        const float GammaCorrection = 1.0f / 2.2f;
        return status.gammaEnable ? GammaCorrection : 1.0f;
    }

    bool VI::compatibleWith(const VI &vi) const {
        return
            (width == vi.width) &&
            (hRegion.hStart == vi.hRegion.hStart) &&
            (hRegion.hEnd == vi.hRegion.hEnd) &&
            (vRegion.vStart == vi.vRegion.vStart) &&
            (vRegion.vEnd == vi.vRegion.vEnd) &&
            (xTransform.xScale == vi.xTransform.xScale) &&
            (xTransform.xOffset == vi.xTransform.xOffset) &&
            (yTransform.yScale == vi.yTransform.yScale) &&
            (yTransform.yOffset == vi.yTransform.yOffset);
    }

    bool VI::visible() const {
        return (status.type != VI_STATUS_TYPE_BLANK) && (hRegion.hStart > 0);
    }

    bool VI::operator!=(const VI &rhs) const {
        return
            (status.word != rhs.status.word) ||
            (origin != rhs.origin) ||
            (width != rhs.width) ||
            (intr != rhs.intr) ||
            (vCurrentLine != rhs.vCurrentLine) ||
            (burst.word != rhs.burst.word) ||
            (vSync != rhs.vSync) ||
            (hSync.word != rhs.hSync.word) ||
            (leap.word != rhs.leap.word) ||
            (hRegion.word != rhs.hRegion.word) ||
            (vRegion.word != rhs.vRegion.word) ||
            (vBurst.word != rhs.vBurst.word) ||
            (xTransform.word != rhs.xTransform.word) ||
            (yTransform.word != rhs.yTransform.word);
    }

    uint8_t VI::fbSiz() const {
        switch (status.type) {
        case VI_STATUS_TYPE_16_BIT:
            return G_IM_SIZ_16b;
        case VI_STATUS_TYPE_32_BIT:
            return G_IM_SIZ_32b;
        case VI_STATUS_TYPE_BLANK:
        default:
            return 0;
        }
    }

    uint32_t VI::fbAddress() const {
        uint8_t siz = fbSiz();

        // Estimate the origin is off by one or two rows.
        if (siz >= G_IM_SIZ_16b) {
            const bool interlacedStep = status.serrate && (vCurrentLine & 0x1);
            const uint32_t rowBytes = width * (1U << (siz - 1));
            const uint32_t rowCount = interlacedStep ? 2 : 1;
            const uint32_t rowOffset = rowBytes * rowCount;
            if (origin >= rowOffset) {
                return origin - rowOffset;
            }
        }

        return origin;
    }

    hlslpp::uint2 VI::fbSize() const {
        hlslpp::uint2 size = { width, 0 };
        
        // In interlaced without deflickering, the stride of the framebuffer is usually double of 
        // what its actual row size is. We detect for such a case and return half the width.
        if (status.serrate) {
            const float estimatedWidth = (hRegion.hEnd - hRegion.hStart) / xScaleFloat();
            const float interlacedTolerance = 1.875f;
            if (estimatedWidth < (width / interlacedTolerance)) {
                size.x = width / 2;
            }
        }

        // We can make a close estimate of the height the framebuffer will use by using the width
        // that was just fixed to eliminate interlacing.
        size.y = lround(float(vRegion.vEnd - vRegion.vStart) / (2.0f * yScaleFloat() * (float(size.x) / float(width))));

        if ((size.x > 0) && (size.y > 0)) {
            // Most of the time, the height is missing a few rows because the framebuffer is offset
            // at the origin and an extra row is left at the end to account for filtering.
            // We add two extra rows to whatever result we get and try to get the closest clean
            // multiplier of the specified Division factor.
            const uint32_t ExtraRows = 2;
            const uint32_t Divisor = 4;
            size.y += ExtraRows;
            size.y = lround(float(size.y) / Divisor) * Divisor;
            return size;
        } else {
            return hlslpp::uint2(0, 0);
        }
    }

    float VI::xScaleFloat() const {
        return (1024.0f / xTransform.xScale);
    }

    float VI::xOffsetFloat() const {
        return xTransform.xOffset / 1024.0f;
    }

    float VI::yScaleFloat() const {
        return (1024.0f / yTransform.yScale);
    }

    float VI::yOffsetFloat() const {
        return yTransform.yOffset / 1024.0f;
    }

    // VIHistory

    VIHistory::VIHistory() {
        historyCursor = 0;
        factorCursor = 0;
        history.fill({});
        factors.fill(0);
    }

    void VIHistory::pushVI(const VI &vi, uint32_t fbWidth) {
        historyCursor = (historyCursor + 1) % history.size();
        Present &entry = history[historyCursor];
        entry.vi = vi;
        entry.fbWidth = fbWidth;
    }

    void VIHistory::pushFactor(uint32_t factor) {
        factorCursor = (factorCursor + 1) % factors.size();
        factors[factorCursor] = factor;
    }

    // Hybrid Heaven fork patch -- NOT to be offered upstream, by decision.
    //
    // Upstream requires every slot of a three-entry ring to agree exactly, and
    // returns 0 otherwise. Zero is not "unknown, carry on": it makes
    // `displayRateAboveOriginal` false in rt64_workload_queue.cpp, so
    // `generateInterpolatedFrames` is false, so `displayFrames` stays at its
    // initialiser of 1 and interpolation stops outright. The presented rate then
    // collapses to the game's own -- measured on Hybrid Heaven as 75 -> 53 fps
    // for 8-12 seconds at a stretch, while the game kept delivering frames at
    // its normal rate and every RT64 stage cost exactly what it had. It reads as
    // a performance problem and is not one.
    //
    // WHAT THE CADENCE ACTUALLY LOOKS LIKE -- measured, after a first attempt at
    // this fix was built on a guess and failed:
    //
    //   fps    ring composition                  factor histogram
    //   68.9   uniform 62% pair 20% scattered 18%   1:12  2:161  3:10
    //   55.5   uniform  7% pair 37% scattered 57%   1:50  2: 82  3:48
    //   52.3   uniform  5% pair 31% scattered 64%   1:50  2: 86  3:47
    //
    // The signal jitters +/-1 VI in BOTH directions and near-symmetrically -- the
    // 1s and 3s arrive in equal number, so no frames are being lost -- and the
    // mean stays pinned at 1.98-1.99 while the scatter grows and the framerate
    // falls. The cadence information is intact; upstream's rule discards it by
    // demanding exact agreement among three samples of a noisy signal. All three
    // slots disagree up to 64% of the time, which is why a majority vote (the
    // first attempt here) could not work either: it only reaches the `pair` case.
    //
    // So: take the ROUNDED MEAN over a wider window. That is the estimator the
    // measurement asks for -- symmetric zero-mean noise averages out exactly --
    // and it still tracks genuine rates rather than assuming 30, which matters
    // because the game really does run at 60 in places (a boot segment measured
    // a clean 1:183, mean factor 1.000).
    //
    // HH_RT64_LEGACY_VI_RATE=1 restores upstream's rule exactly, including
    // examining only the three most recent samples.
    // Which rule this build actually took, for the log to state rather than the
    // harness to assume. A switch that fails to cross the WSL boundary produces a
    // run indistinguishable from one where the switch did nothing -- so the arm
    // has to be read out of the code that branches on it, not out of the script
    // that meant to set it.
    static const bool viRateLegacy = (getenv("HH_RT64_LEGACY_VI_RATE") != nullptr);

    bool usingLegacyViRate() {
        return viRateLegacy;
    }

    uint32_t VIHistory::logicalRateFromFactors() {
        const uint32_t FullRate = 60; // TODO: PAL support.

        const bool legacy = viRateLegacy;
        if (legacy) {
            // Upstream saw a ring of three. Read the three most recent entries so
            // this arm is a faithful control and not a stricter rule over eight.
            uint32_t recent[3];
            for (int i = 0; i < 3; i++) {
                const int index = ((factorCursor - i) % int(FactorCount) + int(FactorCount)) % int(FactorCount);
                recent[i] = factors[index];
            }

            if ((recent[0] != 0) && (recent[1] == recent[0]) && (recent[2] == recent[0])) {
                return FullRate / recent[0];
            }
            else {
                return 0;
            }
        }

        // Mean of the populated slots. A zero means "never written" -- the ring
        // is still filling after a reset -- and must not be averaged in, or the
        // estimate is dragged toward a rate no frame ever ran at.
        uint32_t sum = 0;
        uint32_t populated = 0;
        for (uint32_t factor : factors) {
            if (factor != 0) {
                sum += factor;
                populated++;
            }
        }

        // Refuse to guess from a nearly empty ring. Below this the mean is not a
        // measurement, and answering would interpolate against a number the game
        // has not demonstrated.
        constexpr uint32_t MinimumSamples = 4;
        if (populated < MinimumSamples) {
            return 0;
        }

        // Round to nearest: the noise is symmetric, so the true factor is the
        // nearest integer to the mean, not the floor of it. Integer arithmetic --
        // (sum + populated/2) / populated -- to keep this free of float rounding
        // on a path taken every frame.
        const uint32_t factor = (sum + (populated / 2)) / populated;
        if (factor == 0) {
            return 0;
        }

        return FullRate / factor;
    }

    const VIHistory::Present &VIHistory::top() const {
        return history[historyCursor];
    }
};