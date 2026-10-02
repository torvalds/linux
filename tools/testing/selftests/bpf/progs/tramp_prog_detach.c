// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

int pid;
void *fault_addr;
__u64 ts;

static int do_sleepable(void)
{
	char dst;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	ts = bpf_ktime_get_ns();
	/* blocks for as long as user space wants when fault_addr is armed */
	bpf_copy_from_user(&dst, sizeof(dst), fault_addr);
	return 0;
}

static int do_victim(void)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	ts = bpf_ktime_get_ns();
	return 0;
}

SEC("?fentry.s/bpf_fentry_test1")
int BPF_PROG(fentry_sleepable, int a)
{
	return do_sleepable();
}

SEC("?fentry/bpf_fentry_test1")
int BPF_PROG(fentry_victim, int a)
{
	return do_victim();
}

SEC("?fexit.s/bpf_fentry_test1")
int BPF_PROG(fexit_sleepable, int a, int ret)
{
	return do_sleepable();
}

SEC("?fexit/bpf_fentry_test1")
int BPF_PROG(fexit_victim, int a, int ret)
{
	return do_victim();
}
