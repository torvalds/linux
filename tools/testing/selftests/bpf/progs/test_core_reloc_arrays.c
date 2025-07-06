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

struct core_reloc_arrays_output {
	int a2;
	int a3;
	char b123;
	int c1c;
	int d00d;
	int f01c;
};

struct core_reloc_arrays_substruct {
	int c;
	int d;
};

struct core_reloc_arrays {
	int a[5];
	char b[2][3][4];
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
	struct core_reloc_arrays_substruct f[][2];
};

#define CORE_READ(dst, src) bpf_core_read(dst, sizeof(*(dst)), src)

SEC("raw_tracepoint/sys_enter")
int test_core_arrays(void *ctx)
{
	struct core_reloc_arrays *in = (void *)&data.in;
	struct core_reloc_arrays_output *out = (void *)&data.out;
	int *a;

	if (CORE_READ(&out->a2, &in->a[2]))
		return 1;
	if (CORE_READ(&out->b123, &in->b[1][2][3]))
		return 1;
	if (CORE_READ(&out->c1c, &in->c[1].c))
		return 1;
	if (CORE_READ(&out->d00d, &in->d[0][0].d))
		return 1;
	if (CORE_READ(&out->f01c, &in->f[0][1].c))
		return 1;

	a = __builtin_preserve_access_index(({ in->a; }));
	out->a3 = a[0] + a[1] + a[2] + a[3];

	return 0;
}

