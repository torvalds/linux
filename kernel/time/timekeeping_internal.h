#ifndef _TIMEKEEPING_INTERNAL_H
#define _TIMEKEEPING_INTERNAL_H
/*
 * timekeeping debug functions
 */
#include <linux/time.h>

#ifdef CONFIG_DEBUG_FS
extern void tk_debug_account_sleep_time(struct timespec *t);
#else
#define tk_debug_account_sleep_time(x)
#endif

#endif /* _TIMEKEEPING_INTERNAL_H */
