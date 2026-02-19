#ifndef SWL_SESSION_LOCK_H
#define SWL_SESSION_LOCK_H

#include <stdbool.h>

typedef struct SwlSessionLock SwlSessionLock;
typedef struct SwlCompositor SwlCompositor;

SwlSessionLock *swl_session_lock_create(SwlCompositor *comp);
void swl_session_lock_destroy(SwlSessionLock *lock);
bool swl_session_lock_is_locked(const SwlSessionLock *lock);

#endif /* SWL_SESSION_LOCK_H */
