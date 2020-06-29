// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
/* "undefine" structs in vmlinux.h, because we "override" them below */
#define bpf_iter_meta bpf_iter_meta___not_used
#define bpf_iter__ipv6_route bpf_iter__ipv6_route___not_used
#include "vmlinux.h"
#undef bpf_iter_meta
#undef bpf_iter__ipv6_route
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct bpf_iter_meta {
	struct seq_file *seq;
	__u64 session_id;
	__u64 seq_num;
} __attribute__((preserve_access_index));

struct bpf_iter__ipv6_route {
	struct bpf_iter_meta *meta;
	struct fib6_info *rt;
} __attribute__((preserve_access_index));

char _license[] SEC("license") = "GPL";

extern bool CONFIG_IPV6_SUBTREES __kconfig __weak;

#define RTF_GATEWAY		0x0002
#define IFNAMSIZ		16
#define fib_nh_gw_family	nh_common.nhc_gw_family
#define fib_nh_gw6		nh_common.nhc_gw.ipv6
#define fib_nh_dev		nh_common.nhc_dev

SEC("iter/ipv6_route")
int dump_ipv6_route(struct bpf_iter__ipv6_route *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct fib6_info *rt = ctx->rt;
	const struct net_device *dev;
	struct fib6_nh *fib6_nh;
	unsigned int flags;
	struct nexthop *nh;

	if (rt == (void *)0)
		return 0;

	fib6_nh = &rt->fib6_nh[0];
	flags = rt->fib6_flags;

	/* FIXME: nexthop_is_multipath is not handled here. */
	nh = rt->nh;
	if (rt->nh)
		fib6_nh = &nh->nh_info->fib6_nh;

	BPF_SEQ_PRINTF(seq, "%pi6 %02x ", &rt->fib6_dst.addr, rt->fib6_dst.plen);

	if (CONFIG_IPV6_SUBTREES)
		BPF_SEQ_PRINTF(seq, "%pi6 %02x ", &rt->fib6_src.addr,
			       rt->fib6_src.plen);
	else
		BPF_SEQ_PRINTF(seq, "00000000000000000000000000000000 00 ");

	if (fib6_nh->fib_nh_gw_family) {
		flags |= RTF_GATEWAY;
		BPF_SEQ_PRINTF(seq, "%pi6 ", &fib6_nh->fib_nh_gw6);
	} else {
		BPF_SEQ_PRINTF(seq, "00000000000000000000000000000000 ");
	}

	dev = fib6_nh->fib_nh_dev;
	if (dev)
		BPF_SEQ_PRINTF(seq, "%08x %08x %08x %08x %8s\n", rt->fib6_metric,
			       rt->fib6_ref.refs.counter, 0, flags, dev->name);
	else
		BPF_SEQ_PRINTF(seq, "%08x %08x %08x %08x\n", rt->fib6_metric,
			       rt->fib6_ref.refs.counter, 0, flags);

	return 0;
}
