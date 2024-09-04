// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, __u64);
} cgroup_storage SEC(".maps");

SEC("cgroup_skb/egress")
int bpf_prog(struct __sk_buff *skb)
{
	__u64 *counter;

	counter = bpf_get_local_storage(&cgroup_storage, 0);
	__sync_fetch_and_add(counter, 1);

	/* Drop one out of every two packets */
	return (*counter & 1);
}

char _license[] SEC("license") = "GPL";
