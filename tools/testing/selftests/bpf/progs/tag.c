// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define __tag1 __attribute__((btf_tag("tag1")))
#define __tag2 __attribute__((btf_tag("tag2")))

struct key_t {
	int a;
	int b __tag1 __tag2;
	int c;
} __tag1 __tag2;

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 3);
	__type(key, struct key_t);
	__type(value, __u64);
} hashmap1 SEC(".maps");

__u32 total __tag1 __tag2 = 0;

static __noinline int foo(int x __tag1 __tag2) __tag1 __tag2
{
	struct key_t key;
	__u64 val = 1;

	key.a = key.b = key.c = x;
	bpf_map_update_elem(&hashmap1, &key, &val, 0);
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(sub, int x)
{
	return foo(x);
}
