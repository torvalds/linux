/* SPDX-License-Identifier: GPL-2.0 */
#ifndef UTIL_TRACE_H
#define UTIL_TRACE_H

#include <stdio.h>  /* for FILE */

enum trace_summary_mode {
	SUMMARY__NONE = 0,
	SUMMARY__BY_TOTAL,
	SUMMARY__BY_THREAD,
	SUMMARY__BY_CGROUP,
};

#ifdef HAVE_BPF_SKEL

int trace_prepare_bpf_summary(enum trace_summary_mode mode);
void trace_start_bpf_summary(void);
void trace_end_bpf_summary(void);
int trace_print_bpf_summary(FILE *fp, int max_summary);
void trace_cleanup_bpf_summary(void);

#else /* !HAVE_BPF_SKEL */

static inline int trace_prepare_bpf_summary(enum trace_summary_mode mode __maybe_unused)
{
	return -1;
}
static inline void trace_start_bpf_summary(void) {}
static inline void trace_end_bpf_summary(void) {}
static inline int trace_print_bpf_summary(FILE *fp __maybe_unused, int max_summary __maybe_unused)
{
	return 0;
}
static inline void trace_cleanup_bpf_summary(void) {}

#endif /* HAVE_BPF_SKEL */

#endif /* UTIL_TRACE_H */
