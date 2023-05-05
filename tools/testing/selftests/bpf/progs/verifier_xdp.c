// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/xdp.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("xdp")
__description("XDP, using ifindex from netdev")
__success __retval(1)
__naked void xdp_using_ifindex_from_netdev(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r2 = *(u32*)(r1 + %[xdp_md_ingress_ifindex]);	\
	if r2 < 1 goto l0_%=;				\
	r0 = 1;						\
l0_%=:	exit;						\
"	:
	: __imm_const(xdp_md_ingress_ifindex, offsetof(struct xdp_md, ingress_ifindex))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
