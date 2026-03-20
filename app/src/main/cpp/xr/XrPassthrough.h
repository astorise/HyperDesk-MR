#pragma once

#include <openxr/openxr.h>

class XrContext;

// XrPassthrough wraps the XR_FB_passthrough objects and produces a ready-to-submit
// XrCompositionLayerPassthroughFB for each xrEndFrame call.
class XrPassthrough {
public:
    explicit XrPassthrough(XrContext& ctx);
    ~XrPassthrough();

    void Start();
    void Pause();

    // Returns a pointer to the layer descriptor, valid until the next call or destruction.
    XrCompositionLayerBaseHeader* GetLayer();

private:
    XrContext&           ctx_;
    XrPassthroughFB      passthrough_      = XR_NULL_HANDLE;  // borrowed from XrContext
    XrPassthroughLayerFB passthroughLayer_ = XR_NULL_HANDLE;  // borrowed from XrContext

    XrCompositionLayerPassthroughFB layerDesc_{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};

    // Extension function pointers (loaded from instance).
    PFN_xrPassthroughStartFB      pfnStart_       = nullptr;
    PFN_xrPassthroughPauseFB      pfnPause_        = nullptr;
    PFN_xrPassthroughLayerResumeFB pfnLayerResume_ = nullptr;
};
