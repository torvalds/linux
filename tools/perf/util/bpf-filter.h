/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_UTIL_BPF_FILTER_H
#define PERF_UTIL_BPF_FILTER_H

#include <linux/list.h>

#include "bpf_skel/sample-filter.h"

struct perf_bpf_filter_expr {
	struct list_head list;
	struct list_head groups;
	enum perf_bpf_filter_op op;
	int part;
	enum perf_bpf_filter_term term;
	unsigned long val;
};

struct evsel;

#ifdef HAVE_BPF_SKEL
struct perf_bpf_filter_expr *perf_bpf_filter_expr__new(enum perf_bpf_filter_term term,
						       int part,
						       enum perf_bpf_filter_op op,
						       unsigned long val);
int perf_bpf_filter__parse(struct list_head *expr_head, const char *str);
int perf_bpf_filter__prepare(struct evsel *evsel);
int perf_bpf_filter__destroy(struct evsel *evsel);
u64 perf_bpf_filter__lost_count(struct evsel *evsel);

#else /* !HAVE_BPF_SKEL */

static inline int perf_bpf_filter__parse(struct list_head *expr_head __maybe_unused,
					 const char *str __maybe_unused)
{
	return -EOPNOTSUPP;
}
static inline int perf_bpf_filter__prepare(struct evsel *evsel __maybe_unused)
{
	return -EOPNOTSUPP;
}
static inline int perf_bpf_filter__destroy(struct evsel *evsel __maybe_unused)
{
	return -EOPNOTSUPP;
}
static inline u64 perf_bpf_filter__lost_count(struct evsel *evsel __maybe_unused)
{
	return 0;
}
#endif /* HAVE_BPF_SKEL*/
#endif /* PERF_UTIL_BPF_FILTER_H */
