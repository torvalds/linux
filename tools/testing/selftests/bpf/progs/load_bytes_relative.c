// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright 2020 Google LLC.
 */

#include <errno.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} test_result SEC(".maps");

SEC("cgroup_skb/egress")
int load_bytes_relative(struct __sk_buff *skb)
{
	struct ethhdr eth;
	struct iphdr iph;

	__u32 map_key = 0;
	__u32 test_passed = 0;

	/* MAC header is not set by the time cgroup_skb/egress triggers */
	if (bpf_skb_load_bytes_relative(skb, 0, &eth, sizeof(eth),
					BPF_HDR_START_MAC) != -EFAULT)
		goto fail;

	if (bpf_skb_load_bytes_relative(skb, 0, &iph, sizeof(iph),
					BPF_HDR_START_NET))
		goto fail;

	if (bpf_skb_load_bytes_relative(skb, 0xffff, &iph, sizeof(iph),
					BPF_HDR_START_NET) != -EFAULT)
		goto fail;

	test_passed = 1;

fail:
	bpf_map_update_elem(&test_result, &map_key, &test_passed, BPF_ANY);

	return 1;
}
