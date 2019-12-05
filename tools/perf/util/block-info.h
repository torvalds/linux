/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_BLOCK_H
#define __PERF_BLOCK_H

#include <linux/types.h>
#include <linux/refcount.h>
#include "hist.h"
#include "symbol.h"
#include "sort.h"
#include "ui/ui.h"

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

struct block_fmt {
	struct perf_hpp_fmt	fmt;
	int			idx;
	int			width;
	const char		*header;
	u64			total_cycles;
	u64			block_cycles;
};

enum {
	PERF_HPP_REPORT__BLOCK_TOTAL_CYCLES_PCT,
	PERF_HPP_REPORT__BLOCK_LBR_CYCLES,
	PERF_HPP_REPORT__BLOCK_CYCLES_PCT,
	PERF_HPP_REPORT__BLOCK_AVG_CYCLES,
	PERF_HPP_REPORT__BLOCK_RANGE,
	PERF_HPP_REPORT__BLOCK_DSO,
	PERF_HPP_REPORT__BLOCK_MAX_INDEX
};

struct block_report {
	struct block_hist	hist;
	u64			cycles;
	struct block_fmt	fmts[PERF_HPP_REPORT__BLOCK_MAX_INDEX];
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

struct block_report *block_info__create_report(struct evlist *evlist,
					       u64 total_cycles);

int report__browse_block_hists(struct block_hist *bh, float min_percent,
			       struct evsel *evsel, struct perf_env *env,
			       struct annotation_options *annotation_opts);

float block_info__total_cycles_percent(struct hist_entry *he);

#endif /* __PERF_BLOCK_H */
