/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#ifndef __BENCH_BPF_TIMING_BPF_H__
#define __BENCH_BPF_TIMING_BPF_H__

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf_may_goto.h>

#ifndef BENCH_NR_SAMPLES
#define BENCH_NR_SAMPLES	4096
#endif
#ifndef BENCH_NR_CPUS
#define BENCH_NR_CPUS		256
#endif
#define BENCH_CPU_MASK		(BENCH_NR_CPUS - 1)

__u64 timing_samples[BENCH_NR_CPUS][BENCH_NR_SAMPLES];
__u32 timing_idx[BENCH_NR_CPUS];

volatile __u32 batch_iters;
volatile __u32 timing_enabled;

static __always_inline void bench_record_sample(__u64 elapsed_ns)
{
	__u32 cpu, idx;

	if (!timing_enabled)
		return;

	cpu = bpf_get_smp_processor_id() & BENCH_CPU_MASK;
	idx = timing_idx[cpu];

	if (idx >= BENCH_NR_SAMPLES)
		return;

	timing_samples[cpu][idx] = elapsed_ns;
	timing_idx[cpu] = idx + 1;
}

/*
 * @body:  expression to time; return value (int) stored in __bench_result.
 * @reset: undo body's side-effects so each iteration starts identically.
 *         May reference __bench_result.  Use ({}) for empty reset.
 *
 * Runs batch_iters timed iterations, then one untimed iteration whose
 * return value the macro evaluates to (for validation).
 */
#define BENCH_BPF_LOOP(body, reset) ({					\
	__u64 __bench_start = bpf_ktime_get_ns();			\
	__u32 __bench_i;						\
	int __bench_result;						\
									\
	for (__bench_i = 0;						\
	     __bench_i < batch_iters && can_loop;			\
	     __bench_i++) {						\
		__bench_result = (body);				\
		reset;							\
	}								\
									\
	bench_record_sample(bpf_ktime_get_ns() - __bench_start);	\
									\
	__bench_result = (body);					\
	__bench_result;							\
})

#endif /* __BENCH_BPF_TIMING_BPF_H__ */
