// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_kfuncs.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

#if (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64)) && \
	defined(__BPF_FEATURE_STACK_ARGUMENT)

const volatile bool has_stack_arg = true;

struct bpf_iter_testmod_seq {
	u64 :64;
	u64 :64;
};

extern int bpf_iter_testmod_seq_new(struct bpf_iter_testmod_seq *it, s64 value, int cnt) __ksym;
extern void bpf_iter_testmod_seq_destroy(struct bpf_iter_testmod_seq *it) __ksym;

struct timer_map_value {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct timer_map_value);
} kfunc_timer_map SEC(".maps");

SEC("tc")
int test_stack_arg_scalar(struct __sk_buff *skb)
{
	return bpf_kfunc_call_stack_arg(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
}

SEC("tc")
int test_stack_arg_ptr(struct __sk_buff *skb)
{
	struct prog_test_pass1 p = { .x0 = 10, .x1 = 20 };

	return bpf_kfunc_call_stack_arg_ptr(1, 2, 3, 4, 5, 6, 7, 8, 9, &p);
}

SEC("tc")
int test_stack_arg_mix(struct __sk_buff *skb)
{
	struct prog_test_pass1 p = { .x0 = 10 };
	struct prog_test_pass1 q = { .x1 = 20 };

	return bpf_kfunc_call_stack_arg_mix(1, 2, 3, 4, 5, 6, 7, &p, 8, &q);
}

/* 1+2+3+4+5+6+7+8+9+sizeof(pkt_v4) = 45+54 = 99 */
SEC("tc")
int test_stack_arg_dynptr(struct __sk_buff *skb)
{
	struct bpf_dynptr ptr;

	bpf_dynptr_from_skb(skb, 0, &ptr);
	return bpf_kfunc_call_stack_arg_dynptr(1, 2, 3, 4, 5, 6, 7, 8, 9, &ptr);
}

/* 1 + 2 + 3 + 4 + 5 + (1 + 2 + ... + 16) = 15 + 136 = 151 */
SEC("tc")
int test_stack_arg_mem(struct __sk_buff *skb)
{
	char buf[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

	return bpf_kfunc_call_stack_arg_mem(1, 2, 3, 4, 5, buf, sizeof(buf));
}

/* 1+2+3+4+5+6+7+8+9+100 = 145 */
SEC("tc")
int test_stack_arg_iter(struct __sk_buff *skb)
{
	struct bpf_iter_testmod_seq it;
	u64 ret;

	bpf_iter_testmod_seq_new(&it, 100, 10);
	ret = bpf_kfunc_call_stack_arg_iter(1, 2, 3, 4, 5, 6, 7, 8, 9, &it);
	bpf_iter_testmod_seq_destroy(&it);
	return ret;
}

const char cstr[] = "hello";

/* 1+2+3+4+5+6+7+8+9 = 45 */
SEC("tc")
int test_stack_arg_const_str(struct __sk_buff *skb)
{
	return bpf_kfunc_call_stack_arg_const_str(1, 2, 3, 4, 5, 6, 7, 8, 9,
						  cstr);
}

/* 1+2+3+4+5+6+7+8+9 = 45 */
SEC("tc")
int test_stack_arg_timer(struct __sk_buff *skb)
{
	struct timer_map_value *val;
	int key = 0;

	val = bpf_map_lookup_elem(&kfunc_timer_map, &key);
	if (!val)
		return 0;
	return bpf_kfunc_call_stack_arg_timer(1, 2, 3, 4, 5, 6, 7, 8, 9,
					      &val->timer);
}

#else

const volatile bool has_stack_arg = false;

SEC("tc")
int test_stack_arg_scalar(struct __sk_buff *skb)
{
	return 0;
}

SEC("tc")
int test_stack_arg_ptr(struct __sk_buff *skb)
{
	return 0;
}

SEC("tc")
int test_stack_arg_mix(struct __sk_buff *skb)
{
	return 0;
}

SEC("tc")
int test_stack_arg_dynptr(struct __sk_buff *skb)
{
	return 0;
}

SEC("tc")
int test_stack_arg_mem(struct __sk_buff *skb)
{
	return 0;
}

SEC("tc")
int test_stack_arg_iter(struct __sk_buff *skb)
{
	return 0;
}

SEC("tc")
int test_stack_arg_const_str(struct __sk_buff *skb)
{
	return 0;
}

SEC("tc")
int test_stack_arg_timer(struct __sk_buff *skb)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
