// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/usdt.bpf.h>

/* this file is linked together with test_usdt.c to validate that usdt.bpf.h
 * can be included in multiple .bpf.c files forming single final BPF object
 * file
 */

extern int my_pid;

int usdt_100_called;
int usdt_100_sum;

SEC("usdt//proc/self/exe:test:usdt_100")
int BPF_USDT(usdt_100, int x)
{
	if (my_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	__sync_fetch_and_add(&usdt_100_called, 1);
	__sync_fetch_and_add(&usdt_100_sum, x);

	return 0;
}

char _license[] SEC("license") = "GPL";
