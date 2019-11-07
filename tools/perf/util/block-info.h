/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_BLOCK_H
#define __PERF_BLOCK_H

#include <linux/types.h>
#include <linux/refcount.h>
#include "util/hist.h"
#include "util/symbol.h"

struct block_info {
	struct symbol		*sym;
	u64			start;
	u64			end;
	u64			cycles;
	u64			cycles_aggr;
	s64			cycles_spark[NUM_SPARKS];
	u64			total_cycles;
	int			num;
	int			num_aggr;
	refcount_t		refcnt;
};

struct block_hist;

struct block_info *block_info__new(void);
struct block_info *block_info__get(struct block_info *bi);
void   block_info__put(struct block_info *bi);

static inline void __block_info__zput(struct block_info **bi)
{
	block_info__put(*bi);
	*bi = NULL;
}

#define block_info__zput(bi) __block_info__zput(&bi)

int64_t block_info__cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			struct hist_entry *left, struct hist_entry *right);

int block_info__process_sym(struct hist_entry *he, struct block_hist *bh,
			    u64 *block_cycles_aggr, u64 total_cycles);

#endif /* __PERF_BLOCK_H */
