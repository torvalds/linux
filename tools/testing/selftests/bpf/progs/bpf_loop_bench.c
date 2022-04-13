// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

u32 nr_loops;
long hits;

static int empty_callback(__u32 index, void *data)
{
	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int benchmark(void *ctx)
{
	for (int i = 0; i < 1000; i++) {
		bpf_loop(nr_loops, empty_callback, NULL, 0);

		__sync_add_and_fetch(&hits, nr_loops);
	}
	return 0;
}
