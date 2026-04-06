# SPEC: QR Code Scanning & Screen Wall Anchoring

| Field       | Value                                           |
|-------------|-------------------------------------------------|
| Status      | Implemented                                     |
| Branch      | `fix_regression`                                |
| Author      | Sébastien ASTORI                                |
| Date        | 2026-04-05                                      |

## 1. Overview

The application SHALL use the Quest 3's passthrough camera to scan a QR code containing RDP connection parameters. Upon successful decode, the system SHALL:

1. Parse RDP connection credentials from the QR code.
2. Anchor the virtual screen wall to the user's current head pose.
3. Initiate the RDP connection.

## 2. QR Code Format

The QR code SHALL encode a JSON object:

```json
{
  "h": "<hostname>",
  "u": "<username>",
  "p": "<password>",
  "d": "<domain>",
  "port": 3389
}
```

All fields are required. `port` defaults to 3389 if omitted.

## 3. Camera Access

### 3.1 Camera Selection

The `CameraManager` SHALL enumerate available cameras via `ACameraManager_getCameraIdList()`. It SHALL select a camera suitable for QR scanning based on available metadata (facing, orientation).

### 3.2 Permissions

The application MUST hold the `android.permission.CAMERA` runtime permission. The APK SHALL be installed with the `-g` flag to auto-grant all permissions.

### 3.3 Camera Position

The `QrScanner` SHALL expose `GetActiveCameraPosition()` returning the camera index (0=left passthrough, 1=right passthrough). This is used to select the corresponding eye pose for anchoring.

## 4. Anchoring Algorithm

### 4.1 Pose Selection

At the moment of QR decode, the system SHALL select an anchor pose with the following priority:

1. **Eye pose** matching the scanning camera position (left or right), if available.
2. **Head pose** (cyclopean), as fallback.
3. If no pose is available, anchoring is deferred to the next render frame.

### 4.2 Anchor Application

The `MonitorLayout::AnchorPrimaryToHeadPose()` method SHALL:

1. Extract the horizontal forward direction from the head pose quaternion.
2. Zero the Y component (keep wall upright).
3. Compute a yaw-only orientation quaternion from the horizontal forward.
4. Place the cylinder center 0.5 m behind the viewer along the gaze direction.
5. Apply this anchor to all monitors via `ApplyPrimaryAnchor()`.

### 4.3 Multi-Frame Stabilization

The anchor SHALL be applied over `kQrAnchorFrames` (30) consecutive frames to smooth pose jitter at scan time. A separate `kLayoutRefreshAnchorFrames` (8) SHALL be used when the DisplayControl channel triggers a re-anchor.

## 5. Post-Scan Sequence

After successful QR decode:

1. Haptic feedback: 200 ms pulse at 80% amplitude on both controllers.
2. Status overlay log: hostname, port, username, domain.
3. Stop camera scanning.
4. Initialize video decoders for all monitors (if not already done).
5. Start RDP connection with parsed credentials.
