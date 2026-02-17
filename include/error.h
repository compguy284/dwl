#ifndef DWL_ERROR_H
#define DWL_ERROR_H

typedef enum {
    DWL_OK = 0,
    DWL_ERR_NOMEM,
    DWL_ERR_BACKEND,
    DWL_ERR_CONFIG,
    DWL_ERR_WAYLAND,
    DWL_ERR_INVALID_ARG,
    DWL_ERR_NOT_FOUND,
    DWL_ERR_ALREADY_EXISTS,
    DWL_ERR_IO,
    DWL_ERR_XWAYLAND,
} DwlError;

const char *dwl_error_string(DwlError err);

#endif /* DWL_ERROR_H */
