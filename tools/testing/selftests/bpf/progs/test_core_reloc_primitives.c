// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

struct {
	char in[256];
	char out[256];
} data = {};

enum core_reloc_primitives_enum {
	A = 0,
	B = 1,
};

struct core_reloc_primitives {
	char a;
	int b;
	enum core_reloc_primitives_enum c;
	void *d;
	int (*f)(const char *);
};

#define CORE_READ(dst, src) bpf_core_read(dst, sizeof(*(dst)), src)

SEC("raw_tracepoint/sys_enter")
int test_core_primitives(void *ctx)
{
	struct core_reloc_primitives *in = (void *)&data.in;
	struct core_reloc_primitives *out = (void *)&data.out;

	if (CORE_READ(&out->a, &in->a) ||
	    CORE_READ(&out->b, &in->b) ||
	    CORE_READ(&out->c, &in->c) ||
	    CORE_READ(&out->d, &in->d) ||
	    CORE_READ(&out->f, &in->f))
		return 1;

	return 0;
}

