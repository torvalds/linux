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

struct core_reloc_size_output {
	int int_sz;
	int int_off;
	int struct_sz;
	int struct_off;
	int union_sz;
	int union_off;
	int arr_sz;
	int arr_off;
	int arr_elem_sz;
	int arr_elem_off;
	int ptr_sz;
	int ptr_off;
	int enum_sz;
	int enum_off;
	int float_sz;
	int float_off;
};

struct core_reloc_size {
	int int_field;
	struct { int x; } struct_field;
	union { int x; } union_field;
	int arr_field[4];
	void *ptr_field;
	enum { VALUE = 123 } enum_field;
	float float_field;
};

SEC("raw_tracepoint/sys_enter")
int test_core_size(void *ctx)
{
	struct core_reloc_size *in = (void *)&data.in;
	struct core_reloc_size_output *out = (void *)&data.out;

	out->int_sz = bpf_core_field_size(in->int_field);
	out->int_off = bpf_core_field_offset(in->int_field);

	out->struct_sz = bpf_core_field_size(in->struct_field);
	out->struct_off = bpf_core_field_offset(in->struct_field);

	out->union_sz = bpf_core_field_size(in->union_field);
	out->union_off = bpf_core_field_offset(in->union_field);

	out->arr_sz = bpf_core_field_size(in->arr_field);
	out->arr_off = bpf_core_field_offset(in->arr_field);

	out->arr_elem_sz = bpf_core_field_size(struct core_reloc_size, arr_field[1]);
	out->arr_elem_off = bpf_core_field_offset(struct core_reloc_size, arr_field[1]);

	out->ptr_sz = bpf_core_field_size(struct core_reloc_size, ptr_field);
	out->ptr_off = bpf_core_field_offset(struct core_reloc_size, ptr_field);

	out->enum_sz = bpf_core_field_size(struct core_reloc_size, enum_field);
	out->enum_off = bpf_core_field_offset(struct core_reloc_size, enum_field);

	out->float_sz = bpf_core_field_size(struct core_reloc_size, float_field);
	out->float_off = bpf_core_field_offset(struct core_reloc_size, float_field);

	return 0;
}

