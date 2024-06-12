// SPDX-License-Identifier: GPL-2.0
#define BPF_NO_KFUNC_PROTOTYPES
#include "vmlinux.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>

struct bpf_xfrm_info___local {
	u32 if_id;
	int link;
} __attribute__((preserve_access_index));

__u32 req_if_id;
__u32 resp_if_id;

int bpf_skb_set_xfrm_info(struct __sk_buff *skb_ctx,
			  const struct bpf_xfrm_info___local *from) __ksym;
int bpf_skb_get_xfrm_info(struct __sk_buff *skb_ctx,
			  struct bpf_xfrm_info___local *to) __ksym;

SEC("tc")
int set_xfrm_info(struct __sk_buff *skb)
{
	struct bpf_xfrm_info___local info = { .if_id = req_if_id };

	return bpf_skb_set_xfrm_info(skb, &info) ? TC_ACT_SHOT : TC_ACT_UNSPEC;
}

SEC("tc")
int get_xfrm_info(struct __sk_buff *skb)
{
	struct bpf_xfrm_info___local info = {};

	if (bpf_skb_get_xfrm_info(skb, &info) < 0)
		return TC_ACT_SHOT;

	resp_if_id = info.if_id;

	return TC_ACT_UNSPEC;
}

char _license[] SEC("license") = "GPL";
