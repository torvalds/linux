// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

int pid;
int fentry1_hits;
int fentry2_hits;
int fexit_hits;
int kprobe_hits;

static bool from_test(int a)
{
	return (bpf_get_current_pid_tgid() >> 32) == pid && a == 1;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(fentry1, int a)
{
	if (from_test(a))
		fentry1_hits++;
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(fentry2, int a)
{
	if (from_test(a))
		fentry2_hits++;
	return 0;
}

SEC("fexit/bpf_fentry_test1")
int BPF_PROG(fexit1, int a, int ret)
{
	if (from_test(a) && ret == 2)
		fexit_hits++;
	return 0;
}

SEC("kprobe/bpf_fentry_test1")
int BPF_KPROBE(kprobe1, int a)
{
	if (from_test(a))
		kprobe_hits++;
	return 0;
}
