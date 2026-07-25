//
// RT64
//

#include "rt64_render_hooks.h"

namespace RT64 {
    static RenderHookInit *init = nullptr;
    static RenderHookDraw *draw = nullptr;
    static RenderHookDeinit *deinit = nullptr;

    RenderFormat GetSwapChainFormat() {
#   if defined(__ANDROID__)
        // Android/Adreno & Mali surfaces expose R8G8B8A8_UNORM, not B8G8R8A8_UNORM.
        // RT64's final present is a shader composite (logical RGBA float -> driver
        // stores per target format), so swapping the swap chain format is transparent.
        return RenderFormat::R8G8B8A8_UNORM;
#   else
        return RenderFormat::B8G8R8A8_UNORM;
#   endif
    }

    RenderHookInit *GetRenderHookInit() {
        return init;
    }

    RenderHookDraw *GetRenderHookDraw() {
        return draw;
    }

    RenderHookDeinit *GetRenderHookDeinit() {
        return deinit;
    }

    void SetRenderHooks(RenderHookInit *init_, RenderHookDraw *draw_, RenderHookDeinit *deinit_) {
        init = init_;
        draw = draw_;
        deinit = deinit_;
    }
};