// SPDX-License-Identifier: GPL-2.0-or-later

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#define NF_ACCEPT 1

SEC("netfilter")
int nf_link_attach_test(struct bpf_nf_ctx *ctx)
{
	return NF_ACCEPT;
}

char _license[] SEC("license") = "GPL";
