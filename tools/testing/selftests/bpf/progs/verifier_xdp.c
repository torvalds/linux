// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/xdp.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, __u64);
	__uint(map_flags, BPF_F_RDONLY_PROG);
} map_array_ro SEC(".maps");

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

SEC("xdp")
__description("XDP, using xdp_store_bytes from RO map")
__success __retval(0)
__naked void xdp_store_bytes_from_ro_map(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;                                         \
	*(u64*)(r10 - 8) = r1;                          \
	r2 = r10;                                       \
	r2 += -8;                                       \
	r1 = %[map_array_ro] ll;                        \
	call %[bpf_map_lookup_elem];                    \
	if r0 == 0 goto l0_%=;                          \
	r1 = r6;					\
	r2 = 0;						\
	r3 = r0;					\
	r4 = 8;						\
	call %[bpf_xdp_store_bytes];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_xdp_store_bytes),
	  __imm_addr(map_array_ro)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
