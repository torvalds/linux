// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/bpf.h>
#include <linux/pkt_cls.h>

#include <string.h>

#include <bpf/bpf_helpers.h>

#define NUM_CGROUP_LEVELS	4

struct bpf_map_def SEC("maps") cgroup_ids = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(__u64),
	.max_entries = NUM_CGROUP_LEVELS,
};

static __always_inline void log_nth_level(struct __sk_buff *skb, __u32 level)
{
	__u64 id;

	/* [1] &level passed to external function that may change it, it's
	 *     incompatible with loop unroll.
	 */
	id = bpf_skb_ancestor_cgroup_id(skb, level);
	bpf_map_update_elem(&cgroup_ids, &level, &id, 0);
}

SEC("cgroup_id_logger")
int log_cgroup_id(struct __sk_buff *skb)
{
	/* Loop unroll can't be used here due to [1]. Unrolling manually.
	 * Number of calls should be in sync with NUM_CGROUP_LEVELS.
	 */
	log_nth_level(skb, 0);
	log_nth_level(skb, 1);
	log_nth_level(skb, 2);
	log_nth_level(skb, 3);

	return TC_ACT_OK;
}

int _version SEC("version") = 1;

char _license[] SEC("license") = "GPL";
