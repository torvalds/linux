// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

unsigned long uprobe_trigger_body;

__u64 test1_result = 0;
SEC("uprobe//proc/self/exe:uprobe_trigger_body+1")
int BPF_UPROBE(test1)
{
	__u64 addr = bpf_get_func_ip(ctx);

	test1_result = (const void *) addr == (const void *) uprobe_trigger_body + 1;
	return 0;
}
