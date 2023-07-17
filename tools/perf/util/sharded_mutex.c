// SPDX-License-Identifier: GPL-2.0
#include "sharded_mutex.h"

#include <stdlib.h>

struct sharded_mutex *sharded_mutex__new(size_t num_shards)
{
	struct sharded_mutex *result;
	size_t size;
	unsigned int bits;

	for (bits = 0; ((size_t)1 << bits) < num_shards; bits++)
		;

	size = sizeof(*result) + sizeof(struct mutex) * (1 << bits);
	result = malloc(size);
	if (!result)
		return NULL;

	result->cap_bits = bits;
	for (size_t i = 0; i < ((size_t)1 << bits); i++)
		mutex_init(&result->mutexes[i]);

	return result;
}

void sharded_mutex__delete(struct sharded_mutex *sm)
{
	for (size_t i = 0; i < ((size_t)1 << sm->cap_bits); i++)
		mutex_destroy(&sm->mutexes[i]);

	free(sm);
}
