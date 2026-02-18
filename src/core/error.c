#include "error.h"

const char *swl_error_string(SwlError err)
{
    switch (err) {
    case SWL_OK:
        return "success";
    case SWL_ERR_NOMEM:
        return "out of memory";
    case SWL_ERR_BACKEND:
        return "backend error";
    case SWL_ERR_CONFIG:
        return "configuration error";
    case SWL_ERR_WAYLAND:
        return "wayland error";
    case SWL_ERR_INVALID_ARG:
        return "invalid argument";
    case SWL_ERR_NOT_FOUND:
        return "not found";
    case SWL_ERR_ALREADY_EXISTS:
        return "already exists";
    case SWL_ERR_IO:
        return "I/O error";
    case SWL_ERR_XWAYLAND:
        return "XWayland error";
    default:
        return "unknown error";
    }
}
