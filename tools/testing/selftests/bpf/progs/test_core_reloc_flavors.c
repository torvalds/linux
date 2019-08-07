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

struct core_reloc_flavors {
	int a;
	int b;
	int c;
};

/* local flavor with reversed layout */
struct core_reloc_flavors___reversed {
	int c;
	int b;
	int a;
};

/* local flavor with nested/overlapping layout */
struct core_reloc_flavors___weird {
	struct {
		int b;
	};
	/* a and c overlap in local flavor, but this should still work
	 * correctly with target original flavor
	 */
	union {
		int a;
		int c;
	};
};

SEC("raw_tracepoint/sys_enter")
int test_core_flavors(void *ctx)
{
	struct core_reloc_flavors *in_orig = (void *)&data.in;
	struct core_reloc_flavors___reversed *in_rev = (void *)&data.in;
	struct core_reloc_flavors___weird *in_weird = (void *)&data.in;
	struct core_reloc_flavors *out = (void *)&data.out;

	/* read a using weird layout */
	if (BPF_CORE_READ(&out->a, &in_weird->a))
		return 1;
	/* read b using reversed layout */
	if (BPF_CORE_READ(&out->b, &in_rev->b))
		return 1;
	/* read c using original layout */
	if (BPF_CORE_READ(&out->c, &in_orig->c))
		return 1;

	return 0;
}

