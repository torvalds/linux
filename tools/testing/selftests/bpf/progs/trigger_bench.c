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

SEC("tp/syscalls/sys_enter_getpgid")
int bench_trigger_tp(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("raw_tp/sys_enter")
int BPF_PROG(bench_trigger_raw_tp, struct pt_regs *regs, long id)
{
	if (id == __NR_getpgid)
		inc_counter();
	return 0;
}

SEC("kprobe/" SYS_PREFIX "sys_getpgid")
int bench_trigger_kprobe(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("kretprobe/" SYS_PREFIX "sys_getpgid")
int bench_trigger_kretprobe(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("kprobe.multi/" SYS_PREFIX "sys_getpgid")
int bench_trigger_kprobe_multi(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("kretprobe.multi/" SYS_PREFIX "sys_getpgid")
int bench_trigger_kretprobe_multi(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int bench_trigger_fentry(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("fexit/" SYS_PREFIX "sys_getpgid")
int bench_trigger_fexit(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("fentry.s/" SYS_PREFIX "sys_getpgid")
int bench_trigger_fentry_sleep(void *ctx)
{
	inc_counter();
	return 0;
}

SEC("fmod_ret/" SYS_PREFIX "sys_getpgid")
int bench_trigger_fmodret(void *ctx)
{
	inc_counter();
	return -22;
}

SEC("uprobe")
int bench_trigger_uprobe(void *ctx)
{
	inc_counter();
	return 0;
}
