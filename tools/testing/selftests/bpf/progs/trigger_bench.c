// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook
#include <linux/bpf.h>
#include <asm/unistd.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

#define CPU_MASK 255
#define MAX_CPUS (CPU_MASK + 1) /* should match MAX_BUCKETS in benchs/bench_trigger.c */

/* matches struct counter in bench.h */
struct counter {
	long value;
} __attribute__((aligned(128)));

struct counter hits[MAX_CPUS];

static __always_inline void inc_counter(void)
{
	int cpu = bpf_get_smp_processor_id();

	__sync_add_and_fetch(&hits[cpu & CPU_MASK].value, 1);
}

SEC("?uprobe")
int bench_trigger_uprobe(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("?uprobe.multi")
int bench_trigger_uprobe_multi(void *ctx)
{
	inc_counter();
	return 0;
}

const volatile int batch_iters = 0;

SEC("?raw_tp")
int trigger_count(void *ctx)
{
	int i;

	for (i = 0; i < batch_iters; i++)
		inc_counter();

	return 0;
}

SEC("?raw_tp")
int trigger_driver(void *ctx)
{
	int i;

	for (i = 0; i < batch_iters; i++)
		(void)bpf_get_numa_node_id(); /* attach point for benchmarking */

	return 0;
}

extern int bpf_modify_return_test_tp(int nonce) __ksym __weak;

SEC("?raw_tp")
int trigger_driver_kfunc(void *ctx)
{
	int i;

	for (i = 0; i < batch_iters; i++)
		(void)bpf_modify_return_test_tp(0); /* attach point for benchmarking */

	return 0;
}

SEC("?kprobe/bpf_get_numa_node_id")
int bench_trigger_kprobe(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("?kretprobe/bpf_get_numa_node_id")
int bench_trigger_kretprobe(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("?kprobe.multi/bpf_get_numa_node_id")
int bench_trigger_kprobe_multi(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("?kprobe.multi/bpf_get_numa_node_id")
int bench_kprobe_multi_empty(void *ctx)
{
	return 0;
}

SEC("?kretprobe.multi/bpf_get_numa_node_id")
int bench_trigger_kretprobe_multi(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("?kretprobe.multi/bpf_get_numa_node_id")
int bench_kretprobe_multi_empty(void *ctx)
{
	return 0;
}

SEC("?fentry/bpf_get_numa_node_id")
int bench_trigger_fentry(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("?fexit/bpf_get_numa_node_id")
int bench_trigger_fexit(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("?fmod_ret/bpf_modify_return_test_tp")
int bench_trigger_fmodret(void *ctx)
{
	inc_counter();
	return -22;
}

SEC("?tp/bpf_test_run/bpf_trigger_tp")
int bench_trigger_tp(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("?raw_tp/bpf_trigger_tp")
int bench_trigger_rawtp(void *ctx)
{
	inc_counter();
	return 0;
}
