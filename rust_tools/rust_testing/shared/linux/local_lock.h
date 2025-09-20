#ifndef _LINUX_LOCAL_LOCK
#define _LINUX_LOCAL_LOCK
typedef struct { } local_lock_t;

static inline void local_lock(local_lock_t *lock) { }
static inline void local_unlock(local_lock_t *lock) { }
#define INIT_LOCAL_LOCK(x) { }
#endif
