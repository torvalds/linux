#ifndef _LIBLOCKDEP_COMMON_H
#define _LIBLOCKDEP_COMMON_H

#include <pthread.h>

#define NR_LOCKDEP_CACHING_CLASSES 2
#define MAX_LOCKDEP_SUBCLASSES 8UL

#ifndef CALLER_ADDR0
#define CALLER_ADDR0 ((unsigned long)__builtin_return_address(0))
#endif

#ifndef _RET_IP_
#define _RET_IP_ CALLER_ADDR0
#endif

#ifndef _THIS_IP_
#define _THIS_IP_ ({ __label__ __here; __here: (unsigned long)&&__here; })
#endif

struct lockdep_subclass_key {
	char __one_byte;
};

struct lock_class_key {
	struct lockdep_subclass_key subkeys[MAX_LOCKDEP_SUBCLASSES];
};

struct lockdep_map {
	struct lock_class_key	*key;
	struct lock_class	*class_cache[NR_LOCKDEP_CACHING_CLASSES];
	const char		*name;
#ifdef CONFIG_LOCK_STAT
	int			cpu;
	unsigned long		ip;
#endif
};

void lockdep_init_map(struct lockdep_map *lock, const char *name,
			struct lock_class_key *key, int subclass);
void lock_acquire(struct lockdep_map *lock, unsigned int subclass,
			int trylock, int read, int check,
			struct lockdep_map *nest_lock, unsigned long ip);
void lock_release(struct lockdep_map *lock, int nested,
			unsigned long ip);
extern void debug_check_no_locks_freed(const void *from, unsigned long len);
extern void lockdep_init(void);

#define STATIC_LOCKDEP_MAP_INIT(_name, _key) \
	{ .name = (_name), .key = (void *)(_key), }

#endif
