#pragma once
// Host stub for <freerdp/client/disp.h> and the WinPR types it depends on.

#include <cstdint>

// ── WinPR primitive typedefs ─────────────────────────────────────────────────
typedef uint32_t UINT32;
typedef int32_t  INT32;
typedef uint32_t UINT;
typedef int      BOOL;
#define TRUE  1
#define FALSE 0

// ── Return codes ─────────────────────────────────────────────────────────────
#define CHANNEL_RC_OK        0u
#define ERROR_INTERNAL_ERROR 5u

// ── Monitor orientation ───────────────────────────────────────────────────────
#define ORIENTATION_LANDSCAPE          0u
#define ORIENTATION_PORTRAIT           90u
#define ORIENTATION_LANDSCAPE_FLIPPED  180u
#define ORIENTATION_PORTRAIT_FLIPPED   270u

// ── Monitor flags ─────────────────────────────────────────────────────────────
#define DISPLAY_CONTROL_MONITOR_PRIMARY 0x00000001u

// ── PDU structs ───────────────────────────────────────────────────────────────
typedef struct {
    UINT32 Flags;
    INT32  Left;
    INT32  Top;
    UINT32 Width;
    UINT32 Height;
    UINT32 PhysicalWidth;
    UINT32 PhysicalHeight;
    UINT32 Orientation;
    UINT32 DesktopScaleFactor;
    UINT32 DeviceScaleFactor;
} DISPLAY_CONTROL_MONITOR_LAYOUT;

typedef struct {
    UINT32                       NumMonitors;
    DISPLAY_CONTROL_MONITOR_LAYOUT* Monitors;
} DISPLAY_CONTROL_MONITOR_LAYOUT_PDU;

typedef struct {
    UINT32 MaxNumMonitors;
    UINT32 MaxMonitorAreaFactorA;
    UINT32 MaxMonitorAreaFactorB;
} DISPLAY_CONTROL_CAPS_PDU;

// ── DispClientContext ─────────────────────────────────────────────────────────
typedef struct _DispClientContext DispClientContext;

typedef UINT (*pDispClientSendMonitorLayout)(
    DispClientContext* context,
    DISPLAY_CONTROL_MONITOR_LAYOUT_PDU* pMonitorLayoutPdu);

typedef UINT (*pDispClientDisplayControlCaps)(
    DispClientContext* context,
    DISPLAY_CONTROL_CAPS_PDU* caps);

struct _DispClientContext {
    void*                        custom;
    pDispClientSendMonitorLayout SendMonitorLayout;
    pDispClientDisplayControlCaps DisplayControlCaps;
};
