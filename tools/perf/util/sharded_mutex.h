/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_SHARDED_MUTEX_H
#define PERF_SHARDED_MUTEX_H

#include "mutex.h"
#include "hashmap.h"

/*
 * In a situation where a lock is needed per object, having a mutex can be
 * relatively memory expensive (40 bytes on x86-64). If the object can be
 * constantly hashed, a sharded mutex is an alternative global pool of mutexes
 * where the mutex is looked up from a hash value. This can lead to collisions
 * if the number of shards isn't large enough.
 */
struct sharded_mutex {
	/* mutexes array is 1<<cap_bits in size. */
	unsigned int cap_bits;
	struct mutex mutexes[];
};

struct sharded_mutex *sharded_mutex__new(size_t num_shards);
void sharded_mutex__delete(struct sharded_mutex *sm);

static inline struct mutex *sharded_mutex__get_mutex(struct sharded_mutex *sm, size_t hash)
{
	return &sm->mutexes[hash_bits(hash, sm->cap_bits)];
}

#endif  /* PERF_SHARDED_MUTEX_H */
