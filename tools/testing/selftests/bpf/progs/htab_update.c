// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022. Huawei Technologies Co., Ltd */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

/* Map value type: has BTF-managed field (bpf_timer) */
struct val {
	struct bpf_timer t;
	__u64 payload;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct val);
} htab SEC(".maps");

int pid = 0;
int update_err = 0;

SEC("?fentry/bpf_obj_free_fields")
int bpf_obj_free_fields(void *ctx)
{
	__u32 key = 0;
	struct val value = { .payload = 1 };

	if ((bpf_get_current_pid_tgid() >> 32) != pid)
		return 0;

	update_err = bpf_map_update_elem(&htab, &key, &value, BPF_ANY);
	return 0;
}
