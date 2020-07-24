// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright 2020 Google LLC.
 */

#include <errno.h>
#include <linux/bpf.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>

#include "progs/cg_storage_multi.h"

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, struct cgroup_value);
} cgroup_storage SEC(".maps");

__u32 invocations = 0;

SEC("cgroup_skb/egress/1")
int egress1(struct __sk_buff *skb)
{
	struct cgroup_value *ptr_cg_storage =
		bpf_get_local_storage(&cgroup_storage, 0);

	__sync_fetch_and_add(&ptr_cg_storage->egress_pkts, 1);
	__sync_fetch_and_add(&invocations, 1);

	return 1;
}

SEC("cgroup_skb/egress/2")
int egress2(struct __sk_buff *skb)
{
	struct cgroup_value *ptr_cg_storage =
		bpf_get_local_storage(&cgroup_storage, 0);

	__sync_fetch_and_add(&ptr_cg_storage->egress_pkts, 1);
	__sync_fetch_and_add(&invocations, 1);

	return 1;
}

SEC("cgroup_skb/ingress")
int ingress(struct __sk_buff *skb)
{
	struct cgroup_value *ptr_cg_storage =
		bpf_get_local_storage(&cgroup_storage, 0);

	__sync_fetch_and_add(&ptr_cg_storage->ingress_pkts, 1);
	__sync_fetch_and_add(&invocations, 1);

	return 1;
}
