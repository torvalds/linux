/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#ifndef __BENCH_BPF_TIMING_H__
#define __BENCH_BPF_TIMING_H__

#include <stdbool.h>
#include <linux/types.h>
#include "bench.h"

#ifndef BENCH_NR_SAMPLES
#define BENCH_NR_SAMPLES	4096
#endif
#ifndef BENCH_NR_CPUS
#define BENCH_NR_CPUS		256
#endif

typedef void (*bpf_bench_run_fn)(void *ctx);

struct bpf_bench_timing {
	__u64 (*samples)[BENCH_NR_SAMPLES];	/* skel->bss->timing_samples */
	__u32 *idx;				/* skel->bss->timing_idx */
	volatile __u32 *timing_enabled;		/* &skel->bss->timing_enabled */
	volatile __u32 *batch_iters_bss;	/* &skel->bss->batch_iters */
	__u32 batch_iters;
	__u32 target_samples;
	__u32 nr_cpus;
	int warmup_ticks;
	bool done;
	bool machine_readable;
};

#define BENCH_TIMING_INIT(t, skel, iters) do {				\
	(t)->samples = (skel)->bss->timing_samples;			\
	(t)->idx = (skel)->bss->timing_idx;				\
	(t)->timing_enabled = &(skel)->bss->timing_enabled;		\
	(t)->batch_iters_bss = &(skel)->bss->batch_iters;		\
	(t)->batch_iters = (iters);					\
	(t)->target_samples = 200;					\
	(t)->nr_cpus = env.nr_cpus;					\
	(t)->warmup_ticks = 0;						\
	(t)->done = false;						\
	(t)->machine_readable = false;					\
} while (0)

void bpf_bench_timing_measure(struct bpf_bench_timing *t, struct bench_res *res);
void bpf_bench_timing_report(struct bpf_bench_timing *t, const char *name, const char *desc);
void bpf_bench_calibrate(struct bpf_bench_timing *t, bpf_bench_run_fn run_fn, void *ctx);

#endif /* __BENCH_BPF_TIMING_H__ */
