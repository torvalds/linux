// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#if __has_attribute(btf_decl_tag)
#define __tag1 __attribute__((btf_decl_tag("tag1")))
#define __tag2 __attribute__((btf_decl_tag("tag2")))
volatile const bool skip_tests __tag1 __tag2 = false;
#else
#define __tag1
#define __tag2
volatile const bool skip_tests = true;
#endif

struct key_t {
	int a;
	int b __tag1 __tag2;
	int c;
} __tag1 __tag2;

typedef struct {
	int a;
	int b;
} value_t __tag1 __tag2;

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 3);
	__type(key, struct key_t);
	__type(value, value_t);
} hashmap1 SEC(".maps");


static __noinline int foo(int x __tag1 __tag2) __tag1 __tag2
{
	struct key_t key;
	value_t val = {};

	key.a = key.b = key.c = x;
	bpf_map_update_elem(&hashmap1, &key, &val, 0);
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(sub, int x)
{
	return foo(x);
}
