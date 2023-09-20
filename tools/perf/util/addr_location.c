// SPDX-License-Identifier: GPL-2.0
#include "addr_location.h"
#include "map.h"
#include "maps.h"
#include "thread.h"

void addr_location__init(struct addr_location *al)
{
	al->thread = NULL;
	al->maps = NULL;
	al->map = NULL;
	al->sym = NULL;
	al->srcline = NULL;
	al->addr = 0;
	al->level = 0;
	al->filtered = 0;
	al->cpumode = 0;
	al->cpu = 0;
	al->socket = 0;
}

/*
 * The preprocess_sample method will return with reference counts for the
 * in it, when done using (and perhaps getting ref counts if needing to
 * keep a pointer to one of those entries) it must be paired with
 * addr_location__put(), so that the refcounts can be decremented.
 */
void addr_location__exit(struct addr_location *al)
{
	map__zput(al->map);
	thread__zput(al->thread);
	maps__zput(al->maps);
}

void addr_location__copy(struct addr_location *dst, struct addr_location *src)
{
	thread__put(dst->thread);
	maps__put(dst->maps);
	map__put(dst->map);
	*dst = *src;
	dst->thread = thread__get(src->thread);
	dst->maps = maps__get(src->maps);
	dst->map = map__get(src->map);
}
