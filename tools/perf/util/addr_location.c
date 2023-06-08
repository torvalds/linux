// SPDX-License-Identifier: GPL-2.0
#include "addr_location.h"
#include "map.h"
#include "thread.h"

/*
 * The preprocess_sample method will return with reference counts for the
 * in it, when done using (and perhaps getting ref counts if needing to
 * keep a pointer to one of those entries) it must be paired with
 * addr_location__put(), so that the refcounts can be decremented.
 */
void addr_location__put(struct addr_location *al)
{
	map__zput(al->map);
	thread__zput(al->thread);
}
