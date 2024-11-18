// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2022 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* From include/linux/mm.h. */
#define FMODE_WRITE	0x2

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} data_input SEC(".maps");

char _license[] SEC("license") = "GPL";

SEC("lsm/bpf_map")
int BPF_PROG(check_access, struct bpf_map *map, fmode_t fmode)
{
	if (map != (struct bpf_map *)&data_input)
		return 0;

	if (fmode & FMODE_WRITE)
		return -EACCES;
	barrier();

	return 0;
}
