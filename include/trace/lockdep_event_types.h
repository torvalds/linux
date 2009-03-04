
#ifndef TRACE_EVENT_FORMAT
# error Do not include this file directly.
# error Unless you know what you are doing.
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM lock

#ifdef CONFIG_LOCKDEP

TRACE_FORMAT(lock_acquire,
	TPPROTO(struct lockdep_map *lock, unsigned int subclass,
		int trylock, int read, int check,
		struct lockdep_map *next_lock, unsigned long ip),
	TPARGS(lock, subclass, trylock, read, check, next_lock, ip),
	TPFMT("%s%s%s", trylock ? "try " : "",
		read ? "read " : "", lock->name)
	);

TRACE_FORMAT(lock_release,
	TPPROTO(struct lockdep_map *lock, int nested, unsigned long ip),
	TPARGS(lock, nested, ip),
	TPFMT("%s", lock->name)
	);

#ifdef CONFIG_LOCK_STAT

TRACE_FORMAT(lock_contended,
	TPPROTO(struct lockdep_map *lock, unsigned long ip),
	TPARGS(lock, ip),
	TPFMT("%s", lock->name)
	);

TRACE_FORMAT(lock_acquired,
	TPPROTO(struct lockdep_map *lock, unsigned long ip),
	TPARGS(lock, ip),
	TPFMT("%s", lock->name)
	);

#endif
#endif

#undef TRACE_SYSTEM
