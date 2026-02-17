#include "error.h"

const char *dwl_error_string(DwlError err)
{
    switch (err) {
    case DWL_OK:
        return "success";
    case DWL_ERR_NOMEM:
        return "out of memory";
    case DWL_ERR_BACKEND:
        return "backend error";
    case DWL_ERR_CONFIG:
        return "configuration error";
    case DWL_ERR_WAYLAND:
        return "wayland error";
    case DWL_ERR_INVALID_ARG:
        return "invalid argument";
    case DWL_ERR_NOT_FOUND:
        return "not found";
    case DWL_ERR_ALREADY_EXISTS:
        return "already exists";
    case DWL_ERR_IO:
        return "I/O error";
    case DWL_ERR_XWAYLAND:
        return "XWayland error";
    default:
        return "unknown error";
    }
}
