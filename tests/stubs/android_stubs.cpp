// Stub implementations of Android NDK functions used by production code.
// These are no-ops that allow host (Linux x86-64) compilation and testing.

#include "media/NdkMediaCodec.h"
#include "media/NdkMediaFormat.h"

#include <cstdlib>

// ── AMediaFormat ──────────────────────────────────────────────────────────────

// Use a sentinel value rather than a heap allocation — the stubs ignore the pointer.
static AMediaFormat gFakeFormat;

AMediaFormat* AMediaFormat_new()                                    { return &gFakeFormat; }
void          AMediaFormat_delete(AMediaFormat*)                    {}
void          AMediaFormat_setString(AMediaFormat*, const char*, const char*) {}
void          AMediaFormat_setInt32(AMediaFormat*, const char*, int32_t)      {}

// ── AMediaCodec ───────────────────────────────────────────────────────────────

// Each decoder gets a unique non-null sentinel pointer.  All operations are
// no-ops; `running_` state in MediaCodecDecoder is set by Start()/Stop() which
// check the return value of AMediaCodec_start/stop, both of which return OK.
static AMediaCodec gFakeCodecs[16];
static int         gFakeCodecIdx = 0;

AMediaCodec* AMediaCodec_createDecoderByType(const char* /*mime_type*/) {
    if (gFakeCodecIdx >= 16) gFakeCodecIdx = 0;
    return &gFakeCodecs[gFakeCodecIdx++];
}

void AMediaCodec_delete(AMediaCodec* /*codec*/) {}

media_status_t AMediaCodec_configure(AMediaCodec*, const AMediaFormat*,
                                      ANativeWindow*, AMediaCrypto*, uint32_t) {
    return AMEDIA_OK;
}

media_status_t AMediaCodec_start(AMediaCodec*) { return AMEDIA_OK; }
media_status_t AMediaCodec_stop(AMediaCodec*)  { return AMEDIA_OK; }

// Return -1 to signal "no input buffer available" — SubmitFrame will return false.
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec*, int64_t) { return -1; }

uint8_t* AMediaCodec_getInputBuffer(AMediaCodec*, size_t, size_t* out_size) {
    if (out_size) *out_size = 0;
    return nullptr;
}

media_status_t AMediaCodec_queueInputBuffer(AMediaCodec*, size_t, size_t,
                                             size_t, int64_t, uint32_t) {
    return AMEDIA_OK;
}
