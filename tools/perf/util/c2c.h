/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_UTIL_C2C_H
#define __PERF_UTIL_C2C_H

#include <stdbool.h>
#include <stdint.h>
#include <linux/types.h>
#include "hist.h"
#include "mem-events.h"
#include "stat.h"

struct sort_entry;

struct c2c_hists {
	struct hists		hists;
	struct perf_hpp_list	list;
	struct c2c_stats	stats;
};

struct compute_stats {
	struct stats		 lcl_hitm;
	struct stats		 rmt_hitm;
	struct stats		 lcl_peer;
	struct stats		 rmt_peer;
	struct stats		 load;
};

struct c2c_hist_entry {
	struct c2c_hists	*hists;
	struct evsel		*evsel;
	struct c2c_stats	 stats;
	unsigned long		*cpuset;
	unsigned long		*nodeset;
	struct c2c_stats	*node_stats;
	unsigned int		 cacheline_idx;

	struct compute_stats	 cstats;

	unsigned long		 paddr;
	unsigned long		 paddr_cnt;
	bool			 paddr_zero;
	char			*nodestr;

	/*
	 * must be at the end,
	 * because of its callchain dynamic entry
	 */
	struct hist_entry	he;
};

#define C2C_HEADER_MAX 2

struct c2c_header {
	struct {
		const char *text;
		int	    span;
	} line[C2C_HEADER_MAX];
};

struct c2c_dimension {
	struct c2c_header	 header;
	const char		*name;
	int			 width;
	struct sort_entry	*se;

	int64_t (*cmp)(struct perf_hpp_fmt *fmt,
		       struct hist_entry *left, struct hist_entry *right);
	int   (*entry)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he);
	int   (*color)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he);
};

struct c2c_fmt {
	struct perf_hpp_fmt	 fmt;
	struct c2c_dimension	*dim;
};

#define SYMBOL_WIDTH 30

#define HEADER_LOW(__h)			\
	{				\
		.line[1] = {		\
			.text = __h,	\
		},			\
	}

#define HEADER_BOTH(__h0, __h1)		\
	{				\
		.line[0] = {		\
			.text = __h0,	\
		},			\
		.line[1] = {		\
			.text = __h1,	\
		},			\
	}

void c2c_fmt_free(struct perf_hpp_fmt *fmt);
bool c2c_fmt_equal(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b);

/*
 * Build the function-view hierarchy. Returns -EOPNOTSUPP when @cl_sort lacks
 * iaddr. On success, *@hists remains valid until the next
 * c2c_function__build() or c2c_function__reset(). On failure, *@hists is
 * NULL.
 */
int c2c_function__build(struct c2c_hists *cl_hists, const char *cl_sort,
			bool symbol_full, struct hists **hists);
void c2c_function__reset(void);
/* Valid only between a successful build and c2c_function__reset(). */
struct hist_entry *c2c_function__find_cacheline(struct hist_entry *he);

/* Inputs and TUI callback supplied by the c2c command. */
struct c2c_function_view_args {
	/* Source cacheline histograms used by the common model. */
	struct c2c_hists	*cl_hists;
	/* --coalesce field list, used to require iaddr. */
	const char		*cl_sort;
	/* Do not cap long symbol names. */
	bool			 symbol_full;
	/* Open the cacheline detail view for @he. */
	int			(*browse_cacheline)(struct hist_entry *he);
};

#ifdef HAVE_SLANG_SUPPORT
int perf_c2c__browse_function_view(struct c2c_function_view_args *args);
#else
static inline int
perf_c2c__browse_function_view(struct c2c_function_view_args *args __maybe_unused)
{
	return 0;
}
#endif

#endif /* __PERF_UTIL_C2C_H */
