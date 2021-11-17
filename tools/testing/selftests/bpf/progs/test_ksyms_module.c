// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

extern const int bpf_testmod_ksym_percpu __ksym;

int out_mod_ksym_global = 0;
bool triggered = false;

SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	int *val;
	__u32 cpu;

	val = (int *)bpf_this_cpu_ptr(&bpf_testmod_ksym_percpu);
	out_mod_ksym_global = *val;
	triggered = true;

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
