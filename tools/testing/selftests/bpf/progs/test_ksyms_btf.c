// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Google */

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

__u64 out__runqueues_addr = -1;
__u64 out__bpf_prog_active_addr = -1;

extern const struct rq runqueues __ksym; /* struct type global var. */
extern const int bpf_prog_active __ksym; /* int type global var. */

SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	out__runqueues_addr = (__u64)&runqueues;
	out__bpf_prog_active_addr = (__u64)&bpf_prog_active;

	return 0;
}

char _license[] SEC("license") = "GPL";
