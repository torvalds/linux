// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

SEC("freplace/btf_unreliable_kprobe")
/* context type is what BPF verifier expects for kprobe context, but target
 * program has `stuct whatever *ctx` argument, so freplace operation will be
 * rejected with the following message:
 *
 * arg0 replace_btf_unreliable_kprobe(struct pt_regs *) doesn't match btf_unreliable_kprobe(struct whatever *)
 */
int replace_btf_unreliable_kprobe(bpf_user_pt_regs_t *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
