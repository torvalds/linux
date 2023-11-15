// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "bpf_experimental.h"

struct bin_data {
	char data[256];
	struct bpf_spin_lock lock;
};

struct map_value {
	struct bin_data __kptr * data;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 2048);
} array SEC(".maps");

char _license[] SEC("license") = "GPL";

bool nomem_err = false;

static int del_array(unsigned int i, int *from)
{
	struct map_value *value;
	struct bin_data *old;

	value = bpf_map_lookup_elem(&array, from);
	if (!value)
		return 1;

	old = bpf_kptr_xchg(&value->data, NULL);
	if (old)
		bpf_obj_drop(old);

	(*from)++;
	return 0;
}

static int add_array(unsigned int i, int *from)
{
	struct bin_data *old, *new;
	struct map_value *value;

	value = bpf_map_lookup_elem(&array, from);
	if (!value)
		return 1;

	new = bpf_obj_new(typeof(*new));
	if (!new) {
		nomem_err = true;
		return 1;
	}

	old = bpf_kptr_xchg(&value->data, new);
	if (old)
		bpf_obj_drop(old);

	(*from)++;
	return 0;
}

static void del_then_add_array(int from)
{
	int i;

	i = from;
	bpf_loop(512, del_array, &i, 0);

	i = from;
	bpf_loop(512, add_array, &i, 0);
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG2(test0, int, a)
{
	del_then_add_array(0);
	return 0;
}

SEC("fentry/bpf_fentry_test2")
int BPF_PROG2(test1, int, a, u64, b)
{
	del_then_add_array(512);
	return 0;
}

SEC("fentry/bpf_fentry_test3")
int BPF_PROG2(test2, char, a, int, b, u64, c)
{
	del_then_add_array(1024);
	return 0;
}

SEC("fentry/bpf_fentry_test4")
int BPF_PROG2(test3, void *, a, char, b, int, c, u64, d)
{
	del_then_add_array(1536);
	return 0;
}
