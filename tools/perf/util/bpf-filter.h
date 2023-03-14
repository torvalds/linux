/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_UTIL_BPF_FILTER_H
#define PERF_UTIL_BPF_FILTER_H

#include <linux/list.h>

enum perf_bpf_filter_op {
	PBF_OP_EQ,
	PBF_OP_NEQ,
	PBF_OP_GT,
	PBF_OP_GE,
	PBF_OP_LT,
	PBF_OP_LE,
	PBF_OP_AND,
};

struct perf_bpf_filter_expr {
	struct list_head list;
	enum perf_bpf_filter_op op;
	unsigned long sample_flags;
	unsigned long val;
};

#ifdef HAVE_BPF_SKEL
struct perf_bpf_filter_expr *perf_bpf_filter_expr__new(unsigned long sample_flags,
						       enum perf_bpf_filter_op op,
						       unsigned long val);
int perf_bpf_filter__parse(struct list_head *expr_head, const char *str);
#else /* !HAVE_BPF_SKEL */
static inline int perf_bpf_filter__parse(struct list_head *expr_head __maybe_unused,
					 const char *str __maybe_unused)
{
	return -ENOSYS;
}
#endif /* HAVE_BPF_SKEL*/
#endif /* PERF_UTIL_BPF_FILTER_H */
