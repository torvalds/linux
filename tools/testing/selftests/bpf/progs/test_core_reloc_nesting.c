// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include "bpf_helpers.h"
#include "bpf_core_read.h"

char _license[] SEC("license") = "GPL";

struct {
	char in[256];
	char out[256];
} data = {};

struct core_reloc_nesting_substruct {
	int a;
};

union core_reloc_nesting_subunion {
	int b;
};

/* int a.a.a and b.b.b accesses */
struct core_reloc_nesting {
	union {
		struct core_reloc_nesting_substruct a;
	} a;
	struct {
		union core_reloc_nesting_subunion b;
	} b;
};

#define CORE_READ(dst, src) bpf_core_read(dst, sizeof(*(dst)), src)

SEC("raw_tracepoint/sys_enter")
int test_core_nesting(void *ctx)
{
	struct core_reloc_nesting *in = (void *)&data.in;
	struct core_reloc_nesting *out = (void *)&data.out;

	if (CORE_READ(&out->a.a.a, &in->a.a.a))
		return 1;
	if (CORE_READ(&out->b.b.b, &in->b.b.b))
		return 1;

	return 0;
}

