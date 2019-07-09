// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include <linux/version.h>
#include "bpf_helpers.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} info_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} status_map SEC(".maps");

SEC("send_signal_demo")
int bpf_send_signal_test(void *ctx)
{
	__u64 *info_val, *status_val;
	__u32 key = 0, pid, sig;
	int ret;

	status_val = bpf_map_lookup_elem(&status_map, &key);
	if (!status_val || *status_val != 0)
		return 0;

	info_val = bpf_map_lookup_elem(&info_map, &key);
	if (!info_val || *info_val == 0)
		return 0;

	sig = *info_val >> 32;
	pid = *info_val & 0xffffFFFF;

	if ((bpf_get_current_pid_tgid() >> 32) == pid) {
		ret = bpf_send_signal(sig);
		if (ret == 0)
			*status_val = 1;
	}

	return 0;
}
char __license[] SEC("license") = "GPL";
