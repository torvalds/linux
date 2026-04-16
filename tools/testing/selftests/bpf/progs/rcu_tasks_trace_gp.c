// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"

int done;

SEC("syscall")
int call_rcu_tasks_trace(void *ctx)
{
	return bpf_kfunc_call_test_call_rcu_tasks_trace(&done);
}

char _license[] SEC("license") = "GPL";
