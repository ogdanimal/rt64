//
// RT64
//

#pragma once

#include "common/rt64_plume.h"

namespace RT64 {
    using RenderHookInit = void(RenderInterface *rhi, RenderDevice *device);
    using RenderHookDraw = void(RenderCommandList *list, RenderFramebuffer *swapChainFramebuffer);
    using RenderHookDeinit = void();

    // The swap chain's colour format, and the single source of truth for it.
    //
    // Every pipeline that renders into the swap chain must be created with this same
    // format, or the render pass and the pipeline disagree and Vulkan reports
    // VUID-vkCmdDraw*-renderPass-02684. Android surfaces do not offer B8G8R8A8_UNORM
    // at all, so the format genuinely differs per platform and the three consumers
    // (the swap chain itself, the video interface pipelines, and the host's RmlUi
    // renderer) previously each carried their own copy of the constant.
    //
    // This is a function rather than a getFormat() on the created swap chain because
    // the init render hook -- which is where the host builds its UI pipelines -- runs
    // BEFORE the swap chain is created, so there is nothing to query at that point.
    RenderFormat GetSwapChainFormat();

    RenderHookInit *GetRenderHookInit();
    RenderHookDraw *GetRenderHookDraw();
    RenderHookDeinit *GetRenderHookDeinit();

    void SetRenderHooks(RenderHookInit *init, RenderHookDraw *draw, RenderHookDeinit *deinit);
};
