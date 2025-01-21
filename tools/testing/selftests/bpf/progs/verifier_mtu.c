// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("tc/ingress")
__description("uninit/mtu: write rejected")
__success
__caps_unpriv(CAP_BPF|CAP_NET_ADMIN)
__failure_unpriv __msg_unpriv("invalid indirect read from stack")
int tc_uninit_mtu(struct __sk_buff *ctx)
{
	__u32 mtu;

	bpf_check_mtu(ctx, 0, &mtu, 0, 0);
	return TCX_PASS;
}

char LICENSE[] SEC("license") = "GPL";
