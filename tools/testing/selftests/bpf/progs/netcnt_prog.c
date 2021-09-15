// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/version.h>

#include <bpf/bpf_helpers.h>
#include "netcnt_common.h"

#define MAX_BPS	(3 * 1024 * 1024)

#define REFRESH_TIME_NS	100000000
#define NS_PER_SEC	1000000000

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, union percpu_net_cnt);
} percpu_netcnt SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, union net_cnt);
} netcnt SEC(".maps");

SEC("cgroup/skb")
int bpf_nextcnt(struct __sk_buff *skb)
{
	union percpu_net_cnt *percpu_cnt;
	char fmt[] = "%d %llu %llu\n";
	union net_cnt *cnt;
	__u64 ts, dt;
	int ret;

	cnt = bpf_get_local_storage(&netcnt, 0);
	percpu_cnt = bpf_get_local_storage(&percpu_netcnt, 0);

	percpu_cnt->packets++;
	percpu_cnt->bytes += skb->len;

	if (percpu_cnt->packets > MAX_PERCPU_PACKETS) {
		__sync_fetch_and_add(&cnt->packets,
				     percpu_cnt->packets);
		percpu_cnt->packets = 0;

		__sync_fetch_and_add(&cnt->bytes,
				     percpu_cnt->bytes);
		percpu_cnt->bytes = 0;
	}

	ts = bpf_ktime_get_ns();
	dt = ts - percpu_cnt->prev_ts;

	dt *= MAX_BPS;
	dt /= NS_PER_SEC;

	if (cnt->bytes + percpu_cnt->bytes - percpu_cnt->prev_bytes < dt)
		ret = 1;
	else
		ret = 0;

	if (dt > REFRESH_TIME_NS) {
		percpu_cnt->prev_ts = ts;
		percpu_cnt->prev_packets = cnt->packets;
		percpu_cnt->prev_bytes = cnt->bytes;
	}

	return !!ret;
}

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = LINUX_VERSION_CODE;
