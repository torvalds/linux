// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} sk_stg_map SEC(".maps");

SEC("fentry/bpf_sk_storage_free")
int BPF_PROG(trace_bpf_sk_storage_free, struct sock *sk)
{
	int *value;

	value = bpf_sk_storage_get(&sk_stg_map, sk, 0,
				   BPF_SK_STORAGE_GET_F_CREATE);

	if (value)
		*value = 1;

	return 0;
}

char _license[] SEC("license") = "GPL";
