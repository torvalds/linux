// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <linux/types.h>
#include <bpf/bpf_helpers.h>
#include <linux/bpf.h>
#include <stdint.h>

#define TWFW_MAX_TIERS (64)
/*
 * load is successful
 * #define TWFW_MAX_TIERS (64u)$
 */

struct twfw_tier_value {
	unsigned long mask[1];
};

struct rule {
	uint8_t seqnum;
};

struct rules_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, struct rule);
	__uint(max_entries, 1);
};

struct tiers_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, struct twfw_tier_value);
	__uint(max_entries, 1);
};

struct rules_map rules SEC(".maps");
struct tiers_map tiers SEC(".maps");

SEC("cgroup_skb/ingress")
int twfw_verifier(struct __sk_buff* skb)
{
	const uint32_t key = 0;
	const struct twfw_tier_value* tier = bpf_map_lookup_elem(&tiers, &key);
	if (!tier)
		return 1;

	struct rule* rule = bpf_map_lookup_elem(&rules, &key);
	if (!rule)
		return 1;

	if (rule && rule->seqnum < TWFW_MAX_TIERS) {
		/* rule->seqnum / 64 should always be 0 */
		unsigned long mask = tier->mask[rule->seqnum / 64];
		if (mask)
			return 0;
	}
	return 1;
}
