/* Public domain. */

#ifndef _LINUX_IOPOLL_H
#define _LINUX_IOPOLL_H

#define readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us)	\
({									\
	struct timeval __end, __now, __timeout_tv;			\
	int __timed_out = 0;						\
									\
	if (timeout_us) {						\
		microuptime(&__now);					\
		USEC_TO_TIMEVAL(timeout_us, &__timeout_tv);		\
		timeradd(&__now, &__timeout_tv, &__end);		\
	}								\
									\
	for (;;) {							\
		(val) = (op)(addr);					\
		if (cond)						\
			break;						\
		if (timeout_us) {					\
			microuptime(&__now);				\
			if (timercmp(&__end, &__now, <=)) {		\
				__timed_out = 1;			\
				break;					\
			}						\
		}							\
		if (sleep_us)						\
			delay((sleep_us) / 2);				\
	}								\
	(__timed_out) ? -ETIMEDOUT : 0;					\
})

#endif
