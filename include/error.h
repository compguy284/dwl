#ifndef SWL_ERROR_H
#define SWL_ERROR_H

typedef enum {
    SWL_OK = 0,
    SWL_ERR_NOMEM,
    SWL_ERR_BACKEND,
    SWL_ERR_CONFIG,
    SWL_ERR_WAYLAND,
    SWL_ERR_INVALID_ARG,
    SWL_ERR_NOT_FOUND,
    SWL_ERR_ALREADY_EXISTS,
    SWL_ERR_IO,
    SWL_ERR_XWAYLAND,
} SwlError;

const char *swl_error_string(SwlError err);

#endif /* SWL_ERROR_H */
