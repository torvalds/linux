#undef TRACE_SYSTEM
#define TRACE_SYSTEM lockdep

#if !defined(_TRACE_LOCKDEP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_LOCKDEP_H

#include <linux/lockdep.h>
#include <linux/tracepoint.h>

#ifdef CONFIG_LOCKDEP

TRACE_EVENT(lock_acquire,

	TP_PROTO(struct lockdep_map *lock, unsigned int subclass,
		int trylock, int read, int check,
		struct lockdep_map *next_lock, unsigned long ip),

	TP_ARGS(lock, subclass, trylock, read, check, next_lock, ip),

	TP_STRUCT__entry(
		__field(unsigned int, flags)
		__string(name, lock->name)
	),

	TP_fast_assign(
		__entry->flags = (trylock ? 1 : 0) | (read ? 2 : 0);
		__assign_str(name, lock->name);
	),

	TP_printk("%s%s%s", (__entry->flags & 1) ? "try " : "",
		  (__entry->flags & 2) ? "read " : "",
		  __get_str(name))
);

TRACE_EVENT(lock_release,

	TP_PROTO(struct lockdep_map *lock, int nested, unsigned long ip),

	TP_ARGS(lock, nested, ip),

	TP_STRUCT__entry(
		__string(name, lock->name)
	),

	TP_fast_assign(
		__assign_str(name, lock->name);
	),

	TP_printk("%s", __get_str(name))
);

#ifdef CONFIG_LOCK_STAT

TRACE_EVENT(lock_contended,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip),

	TP_STRUCT__entry(
		__string(name, lock->name)
	),

	TP_fast_assign(
		__assign_str(name, lock->name);
	),

	TP_printk("%s", __get_str(name))
);

TRACE_EVENT(lock_acquired,
	TP_PROTO(struct lockdep_map *lock, unsigned long ip, s64 waittime),

	TP_ARGS(lock, ip, waittime),

	TP_STRUCT__entry(
		__string(name, lock->name)
		__field(unsigned long, wait_usec)
		__field(unsigned long, wait_nsec_rem)
	),
	TP_fast_assign(
		__assign_str(name, lock->name);
		__entry->wait_nsec_rem = do_div(waittime, NSEC_PER_USEC);
		__entry->wait_usec = (unsigned long) waittime;
	),
	TP_printk("%s (%lu.%03lu us)", __get_str(name), __entry->wait_usec,
				       __entry->wait_nsec_rem)
);

#endif
#endif

#endif /* _TRACE_LOCKDEP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
