// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/usdt.bpf.h>

char _license[] SEC("license") = "GPL";

__u64 entry_stack1[32], exit_stack1[32];
__u64 entry_stack1_recur[32], exit_stack1_recur[32];
__u64 entry_stack2[32];
__u64 entry_stack3[32];
__u64 entry_stack4[32], exit_stack4[32];
__u64 usdt_stack[32];

int entry1_len, exit1_len;
int entry1_recur_len, exit1_recur_len;
int entry2_len, exit2_len;
int entry3_len, exit3_len;
int entry4_len, exit4_len;
int usdt_len;

#define SZ sizeof(usdt_stack)

SEC("uprobe//proc/self/exe:target_1")
int BPF_UPROBE(uprobe_1)
{
	/* target_1 is recursive wit depth of 2, so we capture two separate
	 * stack traces, depending on which occurence it is
	 */
	static bool recur = false;

	if (!recur)
		entry1_len = bpf_get_stack(ctx, &entry_stack1, SZ, BPF_F_USER_STACK);
	else
		entry1_recur_len = bpf_get_stack(ctx, &entry_stack1_recur, SZ, BPF_F_USER_STACK);

	recur = true;
	return 0;
}

SEC("uretprobe//proc/self/exe:target_1")
int BPF_URETPROBE(uretprobe_1)
{
	/* see above, target_1 is recursive */
	static bool recur = false;

	/* NOTE: order of returns is reversed to order of entries */
	if (!recur)
		exit1_recur_len = bpf_get_stack(ctx, &exit_stack1_recur, SZ, BPF_F_USER_STACK);
	else
		exit1_len = bpf_get_stack(ctx, &exit_stack1, SZ, BPF_F_USER_STACK);

	recur = true;
	return 0;
}

SEC("uprobe//proc/self/exe:target_2")
int BPF_UPROBE(uprobe_2)
{
	entry2_len = bpf_get_stack(ctx, &entry_stack2, SZ, BPF_F_USER_STACK);
	return 0;
}

/* no uretprobe for target_2 */

SEC("uprobe//proc/self/exe:target_3")
int BPF_UPROBE(uprobe_3)
{
	entry3_len = bpf_get_stack(ctx, &entry_stack3, SZ, BPF_F_USER_STACK);
	return 0;
}

/* no uretprobe for target_3 */

SEC("uprobe//proc/self/exe:target_4")
int BPF_UPROBE(uprobe_4)
{
	entry4_len = bpf_get_stack(ctx, &entry_stack4, SZ, BPF_F_USER_STACK);
	return 0;
}

SEC("uretprobe//proc/self/exe:target_4")
int BPF_URETPROBE(uretprobe_4)
{
	exit4_len = bpf_get_stack(ctx, &exit_stack4, SZ, BPF_F_USER_STACK);
	return 0;
}

SEC("usdt//proc/self/exe:uretprobe_stack:target")
int BPF_USDT(usdt_probe)
{
	usdt_len = bpf_get_stack(ctx, &usdt_stack, SZ, BPF_F_USER_STACK);
	return 0;
}
