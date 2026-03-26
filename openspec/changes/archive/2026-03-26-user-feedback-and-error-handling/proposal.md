# Change Proposal: User Feedback & RDP Error Handling

## Description
Implement a feedback mechanism to inform the user about the connection status and specific RDP errors (auth failure, host unreachable, etc.) directly in the headset. This replaces silent failures with descriptive visual states in Mixed Reality.

## Motivation
A user in a headset cannot see Android Logcat. If a QR code is scanned but the host is down or the password wrong, the user needs to know why the connection failed to take corrective action (e.g., regenerating the QR code).

## Acceptance Criteria
- [ ] Retrieve specific FreeRDP error codes using `freerdp_get_last_error`.
- [ ] Display a "Status Quad" in MR that shows text messages (Connecting, Auth Error, Host Not Found).
- [ ] The Status Quad disappears once the 16 monitors are successfully active.