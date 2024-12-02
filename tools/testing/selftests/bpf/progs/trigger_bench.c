// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/bpf.h>
#include <asm/unistd.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

long hits = 0;

SEC("tp/syscalls/sys_enter_getpgid")
int bench_trigger_tp(void *ctx)
{
	__sync_add_and_fetch(&hits, 1);
	return 0;
}

SEC("raw_tp/sys_enter")
int BPF_PROG(bench_trigger_raw_tp, struct pt_regs *regs, long id)
{
	if (id == __NR_getpgid)
		__sync_add_and_fetch(&hits, 1);
	return 0;
}

SEC("kprobe/" SYS_PREFIX "sys_getpgid")
int bench_trigger_kprobe(void *ctx)
{
	__sync_add_and_fetch(&hits, 1);
	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int bench_trigger_fentry(void *ctx)
{
	__sync_add_and_fetch(&hits, 1);
	return 0;
}

SEC("fentry.s/" SYS_PREFIX "sys_getpgid")
int bench_trigger_fentry_sleep(void *ctx)
{
	__sync_add_and_fetch(&hits, 1);
	return 0;
}

SEC("fmod_ret/" SYS_PREFIX "sys_getpgid")
int bench_trigger_fmodret(void *ctx)
{
	__sync_add_and_fetch(&hits, 1);
	return -22;
}

SEC("uprobe")
int bench_trigger_uprobe(void *ctx)
{
	__sync_add_and_fetch(&hits, 1);
	return 0;
}
