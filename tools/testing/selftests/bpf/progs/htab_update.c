// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022. Huawei Technologies Co., Ltd */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} htab SEC(".maps");

int pid = 0;
int update_err = 0;

SEC("?fentry/lookup_elem_raw")
int lookup_elem_raw(void *ctx)
{
	__u32 key = 0, value = 1;

	if ((bpf_get_current_pid_tgid() >> 32) != pid)
		return 0;

	update_err = bpf_map_update_elem(&htab, &key, &value, 0);
	return 0;
}
