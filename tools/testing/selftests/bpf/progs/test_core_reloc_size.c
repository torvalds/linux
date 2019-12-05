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

struct core_reloc_size_output {
	int int_sz;
	int struct_sz;
	int union_sz;
	int arr_sz;
	int arr_elem_sz;
	int ptr_sz;
	int enum_sz;
};

struct core_reloc_size {
	int int_field;
	struct { int x; } struct_field;
	union { int x; } union_field;
	int arr_field[4];
	void *ptr_field;
	enum { VALUE = 123 } enum_field;
};

SEC("raw_tracepoint/sys_enter")
int test_core_size(void *ctx)
{
	struct core_reloc_size *in = (void *)&data.in;
	struct core_reloc_size_output *out = (void *)&data.out;

	out->int_sz = bpf_core_field_size(in->int_field);
	out->struct_sz = bpf_core_field_size(in->struct_field);
	out->union_sz = bpf_core_field_size(in->union_field);
	out->arr_sz = bpf_core_field_size(in->arr_field);
	out->arr_elem_sz = bpf_core_field_size(in->arr_field[0]);
	out->ptr_sz = bpf_core_field_size(in->ptr_field);
	out->enum_sz = bpf_core_field_size(in->enum_field);

	return 0;
}

