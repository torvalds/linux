/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_BLOCK_H
#define __PERF_BLOCK_H

#include <linux/types.h>
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
	int			br_cntr_nr;
	u64			*br_cntr;
	struct evsel		*evsel;
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
	PERF_HPP_REPORT__BLOCK_BRANCH_COUNTER,
	PERF_HPP_REPORT__BLOCK_MAX_INDEX
};

struct block_report {
	struct block_hist	hist;
	u64			cycles;
	struct block_fmt	fmts[PERF_HPP_REPORT__BLOCK_MAX_INDEX];
	int			nr_fmts;
};

void block_info__delete(struct block_info *bi);

int64_t __block_info__cmp(struct hist_entry *left, struct hist_entry *right);

int64_t block_info__cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			struct hist_entry *left, struct hist_entry *right);

int block_info__process_sym(struct hist_entry *he, struct block_hist *bh,
			    u64 *block_cycles_aggr, u64 total_cycles,
			    unsigned int br_cntr_nr);

struct block_report *block_info__create_report(struct evlist *evlist,
					       u64 total_cycles,
					       int *block_hpps, int nr_hpps,
					       int *nr_reps);

void block_info__free_report(struct block_report *reps, int nr_reps);

int report__browse_block_hists(struct block_hist *bh, float min_percent,
			       struct evsel *evsel, struct perf_env *env);

float block_info__total_cycles_percent(struct hist_entry *he);

#endif /* __PERF_BLOCK_H */
