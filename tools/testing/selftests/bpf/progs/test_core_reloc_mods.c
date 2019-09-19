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

struct core_reloc_mods_output {
	int a, b, c, d, e, f, g, h;
};

typedef const int int_t;
typedef const char *char_ptr_t;
typedef const int arr_t[7];

struct core_reloc_mods_substruct {
	int x;
	int y;
};

typedef struct {
	int x;
	int y;
} core_reloc_mods_substruct_t;

struct core_reloc_mods {
	int a;
	int_t b;
	char *c;
	char_ptr_t d;
	int e[3];
	arr_t f;
	struct core_reloc_mods_substruct g;
	core_reloc_mods_substruct_t h;
};

SEC("raw_tracepoint/sys_enter")
int test_core_mods(void *ctx)
{
	struct core_reloc_mods *in = (void *)&data.in;
	struct core_reloc_mods_output *out = (void *)&data.out;

	if (BPF_CORE_READ(&out->a, &in->a) ||
	    BPF_CORE_READ(&out->b, &in->b) ||
	    BPF_CORE_READ(&out->c, &in->c) ||
	    BPF_CORE_READ(&out->d, &in->d) ||
	    BPF_CORE_READ(&out->e, &in->e[2]) ||
	    BPF_CORE_READ(&out->f, &in->f[1]) ||
	    BPF_CORE_READ(&out->g, &in->g.x) ||
	    BPF_CORE_READ(&out->h, &in->h.y))
		return 1;

	return 0;
}

