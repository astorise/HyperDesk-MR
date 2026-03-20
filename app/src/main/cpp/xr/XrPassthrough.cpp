#include "XrPassthrough.h"
#include "XrContext.h"
#include "../util/Logger.h"
#include "../util/Check.h"

XrPassthrough::XrPassthrough(XrContext& ctx) : ctx_(ctx) {
    passthrough_      = ctx_.GetPassthroughFB();
    passthroughLayer_ = ctx_.GetPassthroughLayerFB();

    XR_CHECK(xrGetInstanceProcAddr(ctx_.GetInstance(), "xrPassthroughStartFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&pfnStart_)));
    XR_CHECK(xrGetInstanceProcAddr(ctx_.GetInstance(), "xrPassthroughPauseFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&pfnPause_)));
    XR_CHECK(xrGetInstanceProcAddr(ctx_.GetInstance(), "xrPassthroughLayerResumeFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&pfnLayerResume_)));

    // Pre-fill the layer descriptor.
    layerDesc_.type         = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB;
    layerDesc_.flags        = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    layerDesc_.space        = XR_NULL_HANDLE;
    layerDesc_.layerHandle  = passthroughLayer_;
}

XrPassthrough::~XrPassthrough() = default;  // handles are owned by XrContext

void XrPassthrough::Start() {
    XR_CHECK(pfnStart_(passthrough_));
    XR_CHECK(pfnLayerResume_(passthroughLayer_));
    LOGI("Passthrough started");
}

void XrPassthrough::Pause() {
    XR_CHECK(pfnPause_(passthrough_));
    LOGI("Passthrough paused");
}

XrCompositionLayerBaseHeader* XrPassthrough::GetLayer() {
    return reinterpret_cast<XrCompositionLayerBaseHeader*>(&layerDesc_);
}
