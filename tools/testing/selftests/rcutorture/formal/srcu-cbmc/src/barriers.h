#ifndef BARRIERS_H
#define BARRIERS_H

#define barrier() __asm__ __volatile__("" : : : "memory")

#ifdef RUN
#define smp_mb() __sync_synchronize()
#define smp_mb__after_unlock_lock() __sync_synchronize()
#else
/*
 * Copied from CBMC's implementation of __sync_synchronize(), which
 * seems to be disabled by default.
 */
#define smp_mb() __CPROVER_fence("WWfence", "RRfence", "RWfence", "WRfence", \
				 "WWcumul", "RRcumul", "RWcumul", "WRcumul")
#define smp_mb__after_unlock_lock() __CPROVER_fence("WWfence", "RRfence", "RWfence", "WRfence", \
				    "WWcumul", "RRcumul", "RWcumul", "WRcumul")
#endif

/*
 * Allow memory barriers to be disabled in either the read or write side
 * of SRCU individually.
 */

#ifndef NO_SYNC_SMP_MB
#define sync_smp_mb() smp_mb()
#else
#define sync_smp_mb() do {} while (0)
#endif

#ifndef NO_READ_SIDE_SMP_MB
#define rs_smp_mb() smp_mb()
#else
#define rs_smp_mb() do {} while (0)
#endif

#define ACCESS_ONCE(x) (*(volatile typeof(x) *) &(x))
#define READ_ONCE(x) ACCESS_ONCE(x)
#define WRITE_ONCE(x, val) (ACCESS_ONCE(x) = (val))

#endif
