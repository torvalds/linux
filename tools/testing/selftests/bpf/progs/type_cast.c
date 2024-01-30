// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_kfuncs.h"

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, long);
} enter_id SEC(".maps");

#define	IFNAMSIZ 16

int ifindex, ingress_ifindex;
char name[IFNAMSIZ];
unsigned int inum;
unsigned int meta_len, frag0_len, kskb_len, kskb2_len;

SEC("?xdp")
int md_xdp(struct xdp_md *ctx)
{
	struct xdp_buff *kctx = bpf_cast_to_kern_ctx(ctx);
	struct net_device *dev;

	dev = kctx->rxq->dev;
	ifindex = dev->ifindex;
	inum = dev->nd_net.net->ns.inum;
	__builtin_memcpy(name, dev->name, IFNAMSIZ);
	ingress_ifindex = ctx->ingress_ifindex;
	return XDP_PASS;
}

SEC("?tc")
int md_skb(struct __sk_buff *skb)
{
	struct sk_buff *kskb = bpf_cast_to_kern_ctx(skb);
	struct skb_shared_info *shared_info;
	struct sk_buff *kskb2;

	kskb_len = kskb->len;

	/* Simulate the following kernel macro:
	 *   #define skb_shinfo(SKB) ((struct skb_shared_info *)(skb_end_pointer(SKB)))
	 */
	shared_info = bpf_core_cast(kskb->head + kskb->end, struct skb_shared_info);
	meta_len = shared_info->meta_len;
	frag0_len = shared_info->frag_list->len;

	/* kskb2 should be equal to kskb */
	kskb2 = bpf_core_cast(kskb, typeof(*kskb2));
	kskb2_len = kskb2->len;
	return 0;
}

SEC("?tp_btf/sys_enter")
int BPF_PROG(untrusted_ptr, struct pt_regs *regs, long id)
{
	struct task_struct *task, *task_dup;

	task = bpf_get_current_task_btf();
	task_dup = bpf_core_cast(task, struct task_struct);
	(void)bpf_task_storage_get(&enter_id, task_dup, 0, 0);
	return 0;
}

SEC("?tracepoint/syscalls/sys_enter_nanosleep")
int kctx_u64(void *ctx)
{
	u64 *kctx = bpf_core_cast(ctx, u64);

	(void)kctx;
	return 0;
}

char _license[] SEC("license") = "GPL";
