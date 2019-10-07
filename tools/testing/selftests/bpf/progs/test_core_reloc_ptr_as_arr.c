// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include "bpf_helpers.h"

char _license[] SEC("license") = "GPL";

static volatile struct data {
	char in[256];
	char out[256];
} data;

struct core_reloc_ptr_as_arr {
	int a;
};

SEC("raw_tracepoint/sys_enter")
int test_core_ptr_as_arr(void *ctx)
{
	struct core_reloc_ptr_as_arr *in = (void *)&data.in;
	struct core_reloc_ptr_as_arr *out = (void *)&data.out;

	if (BPF_CORE_READ(&out->a, &in[2].a))
		return 1;

	return 0;
}

