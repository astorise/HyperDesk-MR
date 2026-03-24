#pragma once
// Host stub for <media/NdkMediaCodec.h>

#include "NdkMediaFormat.h"
#include "../android/native_window.h"
#include <cstdint>
#include <cstddef>
#include <sys/types.h>   // ssize_t

typedef struct AMediaCodec AMediaCodec;
typedef struct AMediaCrypto AMediaCrypto;

typedef enum {
    AMEDIA_OK                       =  0,
    AMEDIACODEC_ERROR_INSUFFICIENT_RESOURCE = 1100,
    AMEDIACODEC_ERROR_RECLAIMED     = 1101,
} media_status_t;

#define AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM 4

typedef struct {
    int32_t  offset;
    int32_t  size;
    int64_t  presentationTimeUs;
    uint32_t flags;
} AMediaCodecBufferInfo;

typedef struct {
    void (*onAsyncInputAvailable) (AMediaCodec*, void*, int32_t);
    void (*onAsyncOutputAvailable)(AMediaCodec*, void*, int32_t, AMediaCodecBufferInfo*);
    void (*onAsyncFormatChanged)  (AMediaCodec*, void*, AMediaFormat*);
    void (*onAsyncError)          (AMediaCodec*, void*, media_status_t, int32_t, const char*);
} AMediaCodecOnAsyncNotifyCallback;

#ifdef __cplusplus
extern "C" {
#endif

AMediaCodec*   AMediaCodec_createDecoderByType(const char* mime_type);
void           AMediaCodec_delete(AMediaCodec* codec);
media_status_t AMediaCodec_configure(AMediaCodec* codec,
                                     const AMediaFormat* format,
                                     ANativeWindow* surface,
                                     AMediaCrypto* crypto,
                                     uint32_t flags);
media_status_t AMediaCodec_start(AMediaCodec* codec);
media_status_t AMediaCodec_stop(AMediaCodec* codec);
media_status_t AMediaCodec_setAsyncNotifyCallback(AMediaCodec* codec,
                                                   AMediaCodecOnAsyncNotifyCallback cb,
                                                   void* userdata);
ssize_t        AMediaCodec_dequeueInputBuffer(AMediaCodec* codec, int64_t timeoutUs);
uint8_t*       AMediaCodec_getInputBuffer(AMediaCodec* codec,
                                          size_t idx, size_t* out_size);
media_status_t AMediaCodec_queueInputBuffer(AMediaCodec* codec,
                                            size_t idx, size_t offset,
                                            size_t size, int64_t presentationTimeUs,
                                            uint32_t flags);

#ifdef __cplusplus
}
#endif
