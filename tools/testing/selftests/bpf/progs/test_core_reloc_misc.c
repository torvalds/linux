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

struct core_reloc_misc_output {
	int a, b, c;
};

struct core_reloc_misc___a {
	int a1;
	int a2;
};

struct core_reloc_misc___b {
	int b1;
	int b2;
};

/* fixed two first members, can be extended with new fields */
struct core_reloc_misc_extensible {
	int a;
	int b;
};

#define CORE_READ(dst, src) bpf_core_read(dst, sizeof(*(dst)), src)

SEC("raw_tracepoint/sys_enter")
int test_core_misc(void *ctx)
{
	struct core_reloc_misc___a *in_a = (void *)&data.in;
	struct core_reloc_misc___b *in_b = (void *)&data.in;
	struct core_reloc_misc_extensible *in_ext = (void *)&data.in;
	struct core_reloc_misc_output *out = (void *)&data.out;

	/* record two different relocations with the same accessor string */
	if (CORE_READ(&out->a, &in_a->a1) ||		/* accessor: 0:0 */
	    CORE_READ(&out->b, &in_b->b1))		/* accessor: 0:0 */
		return 1;

	/* Validate relocations capture array-only accesses for structs with
	 * fixed header, but with potentially extendable tail. This will read
	 * first 4 bytes of 2nd element of in_ext array of potentially
	 * variably sized struct core_reloc_misc_extensible. */ 
	if (CORE_READ(&out->c, &in_ext[2]))		/* accessor: 2 */
		return 1;

	return 0;
}

