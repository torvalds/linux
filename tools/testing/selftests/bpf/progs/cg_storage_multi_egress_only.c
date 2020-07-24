// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright 2020 Google LLC.
 */

#include <errno.h>
#include <linux/bpf.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, __u32);
} cgroup_storage SEC(".maps");

__u32 invocations = 0;

SEC("cgroup_skb/egress")
int egress(struct __sk_buff *skb)
{
	__u32 *ptr_cg_storage = bpf_get_local_storage(&cgroup_storage, 0);

	__sync_fetch_and_add(ptr_cg_storage, 1);
	__sync_fetch_and_add(&invocations, 1);

	return 1;
}
