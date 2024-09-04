// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Huawei Technologies Co., Ltd */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

SEC("lsm/file_permission")
int lsm_file_permission_prog(void *ctx)
{
	return 0;
}

SEC("lsm/file_alloc_security")
int lsm_file_alloc_security_prog(void *ctx)
{
	return 0;
}

SEC("lsm/file_alloc_security")
int lsm_file_alloc_security_entry(void *ctx)
{
	bpf_tail_call_static(ctx, &jmp_table, 0);
	return 0;
}
