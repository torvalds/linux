// SPDX-License-Identifier: GPL-2.0
/*
 * C2C function model - function-level cacheline sharing analysis
 *
 * Displays a 3-level hierarchy showing which functions share cachelines:
 *   Level 1: Read-side functions sorted by Cycles % (estimated load cycles)
 *   Level 2: Functions sampled writing the shared lines read by level 1
 *   Level 3: The specific cachelines where the two functions contend
 *
 * Builds the hierarchy from the existing cacheline histograms
 * (c2c_hist_entry->hists), reusing the shared c2c data structures.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <tools/libc_compat.h> /* reallocarray */
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/zalloc.h>

#include "addr_location.h"
#include "c2c.h"
#include "cacheline.h"
#include "hist.h"
#include "map.h"
#include "mem-events.h"
#include "mem-info.h"
#include "sort.h"
#include "symbol.h"
#include "thread.h"

struct c2c_function_model {
	struct c2c_hists	function_hists;
	/* Total estimated cycles across all level-1 entries. */
	u64			total_cycles;
	/* Source cacheline histograms; not owned here. */
	struct c2c_hists	*cl_hists;
	/* --coalesce field list, used to require iaddr. */
	const char		*cl_sort;
	/* Do not cap long symbol names. */
	bool			 symbol_full;
};

static struct c2c_function_model c2c_ext __maybe_unused;

static inline __maybe_unused u64 c2c_hitm_count(const struct c2c_stats *stats)
{
	return stats->tot_hitm;
}

static inline __maybe_unused bool symbol_name_equal(struct symbol *a, struct symbol *b)
{
	/* Two unknown symbols compare equal, matching cmp_null() in util/sort.c. */
	if (!a || !b)
		return a == b;
	return arch__compare_symbol_names(a->name, b->name) == 0;
}

static inline __maybe_unused u64 hist_entry__iaddr(struct hist_entry *he)
{
	if (he->mem_info)
		return mem_info__iaddr(he->mem_info)->addr;
	return he->ip;
}
