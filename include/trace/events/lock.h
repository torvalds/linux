#undef TRACE_SYSTEM
#define TRACE_SYSTEM lock

#if !defined(_TRACE_LOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_LOCK_H

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
		__field(void *, lockdep_addr)
	),

	TP_fast_assign(
		__entry->flags = (trylock ? 1 : 0) | (read ? 2 : 0);
		__assign_str(name, lock->name);
		__entry->lockdep_addr = lock;
	),

	TP_printk("%p %s%s%s", __entry->lockdep_addr,
		  (__entry->flags & 1) ? "try " : "",
		  (__entry->flags & 2) ? "read " : "",
		  __get_str(name))
);

DECLARE_EVENT_CLASS(lock,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip),

	TP_STRUCT__entry(
		__string(	name, 	lock->name	)
		__field(	void *, lockdep_addr	)
	),

	TP_fast_assign(
		__assign_str(name, lock->name);
		__entry->lockdep_addr = lock;
	),

	TP_printk("%p %s",  __entry->lockdep_addr, __get_str(name))
);

DEFINE_EVENT(lock, lock_release,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip)
);

#ifdef CONFIG_LOCK_STAT

DEFINE_EVENT(lock, lock_contended,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip)
);

DEFINE_EVENT(lock, lock_acquired,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip)
);

#endif
#endif

#endif /* _TRACE_LOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
