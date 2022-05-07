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

struct core_reloc_existence_output {
	int a_exists;
	int a_value;
	int b_exists;
	int b_value;
	int c_exists;
	int c_value;
	int arr_exists;
	int arr_value;
	int s_exists;
	int s_value;
};

struct core_reloc_existence {
	struct {
		int x;
	} s;
	int arr[1];
	int a;
	struct {
		int b;
	};
	int c;
};

SEC("raw_tracepoint/sys_enter")
int test_core_existence(void *ctx)
{
	struct core_reloc_existence *in = (void *)&data.in;
	struct core_reloc_existence_output *out = (void *)&data.out;

	out->a_exists = bpf_core_field_exists(in->a);
	if (bpf_core_field_exists(in->a))
		out->a_value = BPF_CORE_READ(in, a);
	else
		out->a_value = 0xff000001u;

	out->b_exists = bpf_core_field_exists(in->b);
	if (bpf_core_field_exists(in->b))
		out->b_value = BPF_CORE_READ(in, b);
	else
		out->b_value = 0xff000002u;

	out->c_exists = bpf_core_field_exists(in->c);
	if (bpf_core_field_exists(in->c))
		out->c_value = BPF_CORE_READ(in, c);
	else
		out->c_value = 0xff000003u;

	out->arr_exists = bpf_core_field_exists(in->arr);
	if (bpf_core_field_exists(in->arr))
		out->arr_value = BPF_CORE_READ(in, arr[0]);
	else
		out->arr_value = 0xff000004u;

	out->s_exists = bpf_core_field_exists(in->s);
	if (bpf_core_field_exists(in->s))
		out->s_value = BPF_CORE_READ(in, s.x);
	else
		out->s_value = 0xff000005u;

	return 0;
}

