## ADDED Requirements

### Requirement: RDP error codes are captured on connection failure
`RdpConnectionManager::RunEventLoop` SHALL call `freerdp_get_last_error(instance_->context)` when `freerdp_connect` returns `FALSE` and store the resulting error code.

#### Scenario: Error code is captured on connection failure
- **WHEN** `freerdp_connect` returns `FALSE`
- **THEN** the specific FreeRDP error code is retrieved via `freerdp_get_last_error` and made available to the feedback layer

#### Scenario: No error code on successful connection
- **WHEN** `freerdp_connect` returns `TRUE`
- **THEN** no error code is stored and the connection proceeds normally

### Requirement: Error codes are translated to human-readable strings
An `ErrorUtils` helper SHALL map FreeRDP error codes (e.g. `FREERDP_ERROR_AUTHENTICATION_FAILED`, `FREERDP_ERROR_DNS_NAME_NOT_FOUND`, `FREERDP_ERROR_CONNECT_TRANSPORT_FAILED`) to user-friendly English strings.

#### Scenario: Known error code is translated
- **WHEN** a known FreeRDP error code is passed to `ErrorUtils`
- **THEN** a human-readable string describing the error is returned (e.g. "Access Denied", "Host Not Found", "Network Error")

#### Scenario: Unknown error code returns a generic message
- **WHEN** an unrecognized FreeRDP error code is passed to `ErrorUtils`
- **THEN** a generic "Connection Failed" message is returned with the numeric error code
