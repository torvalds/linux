// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Google LLC. */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} sk_stg_map SEC(".maps");

SEC("iter/bpf_sk_storage_map")
int delete_bpf_sk_storage_map(struct bpf_iter__bpf_sk_storage_map *ctx)
{
	if (ctx->sk)
		bpf_sk_storage_delete(&sk_stg_map, ctx->sk);

	return 0;
}
