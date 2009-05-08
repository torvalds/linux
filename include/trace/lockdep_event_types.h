
#ifndef TRACE_FORMAT
# error Do not include this file directly.
# error Unless you know what you are doing.
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM lock

#ifdef CONFIG_LOCKDEP

TRACE_FORMAT(lock_acquire,
	TP_PROTO(struct lockdep_map *lock, unsigned int subclass,
		int trylock, int read, int check,
		struct lockdep_map *next_lock, unsigned long ip),
	TP_ARGS(lock, subclass, trylock, read, check, next_lock, ip),
	TP_FMT("%s%s%s", trylock ? "try " : "",
		read ? "read " : "", lock->name)
	);

TRACE_FORMAT(lock_release,
	TP_PROTO(struct lockdep_map *lock, int nested, unsigned long ip),
	TP_ARGS(lock, nested, ip),
	TP_FMT("%s", lock->name)
	);

#ifdef CONFIG_LOCK_STAT

TRACE_FORMAT(lock_contended,
	TP_PROTO(struct lockdep_map *lock, unsigned long ip),
	TP_ARGS(lock, ip),
	TP_FMT("%s", lock->name)
	);

TRACE_FORMAT(lock_acquired,
	TP_PROTO(struct lockdep_map *lock, unsigned long ip),
	TP_ARGS(lock, ip),
	TP_FMT("%s", lock->name)
	);

#endif
#endif

#undef TRACE_SYSTEM
