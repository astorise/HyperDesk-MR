#pragma once

#include <freerdp/error.h>
#include <cstdint>

// Maps FreeRDP error codes to user-friendly strings for in-headset display.
namespace ErrorUtils {

inline const char* RdpErrorToString(uint32_t errorCode) {
    switch (errorCode) {
        case 0x00010001:
            return "Server Closed Session";
        case FREERDP_ERROR_AUTHENTICATION_FAILED:
            return "Access Denied";
        case FREERDP_ERROR_CONNECT_TRANSPORT_FAILED:
            return "Network Error";
        case FREERDP_ERROR_DNS_ERROR:
        case FREERDP_ERROR_DNS_NAME_NOT_FOUND:
            return "Host Not Found";
        case FREERDP_ERROR_CONNECT_CANCELLED:
            return "Connection Cancelled";
        case FREERDP_ERROR_SECURITY_NEGO_CONNECT_FAILED:
            return "Security Negotiation Failed";
        case FREERDP_ERROR_CONNECT_FAILED:
            return "Connection Failed";
        case FREERDP_ERROR_INSUFFICIENT_PRIVILEGES:
            return "Insufficient Privileges";
        case FREERDP_ERROR_SERVER_DENIED_CONNECTION:
            return "Server Denied Connection";
        case 0:
            return nullptr;
        default:
            return "Connection Failed";
    }
}

inline const char* RdpErrorHint(uint32_t errorCode) {
    switch (errorCode) {
        case 0x00010001:
            return "Other RDP/admin session";
        case FREERDP_ERROR_AUTHENTICATION_FAILED:
            return "Check username/password";
        case FREERDP_ERROR_CONNECT_TRANSPORT_FAILED:
            return "Check host, port, network";
        case FREERDP_ERROR_DNS_ERROR:
        case FREERDP_ERROR_DNS_NAME_NOT_FOUND:
            return "Check hostname or IP";
        default:
            return nullptr;
    }
}

} // namespace ErrorUtils
