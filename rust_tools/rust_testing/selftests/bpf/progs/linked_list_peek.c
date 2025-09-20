// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

struct node_data {
	struct bpf_list_node l;
	int key;
};

#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))
private(A) struct bpf_spin_lock glock;
private(A) struct bpf_list_head ghead __contains(node_data, l);

#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define NR_NODES 16

int zero = 0;

SEC("syscall")
__retval(0)
long list_peek(void *ctx)
{
	struct bpf_list_node *l_n;
	struct node_data *n;
	int i, err = 0;

	bpf_spin_lock(&glock);
	l_n = bpf_list_front(&ghead);
	bpf_spin_unlock(&glock);
	if (l_n)
		return __LINE__;

	bpf_spin_lock(&glock);
	l_n = bpf_list_back(&ghead);
	bpf_spin_unlock(&glock);
	if (l_n)
		return __LINE__;

	for (i = zero; i < NR_NODES && can_loop; i++) {
		n = bpf_obj_new(typeof(*n));
		if (!n)
			return __LINE__;
		n->key = i;
		bpf_spin_lock(&glock);
		bpf_list_push_back(&ghead, &n->l);
		bpf_spin_unlock(&glock);
	}

	bpf_spin_lock(&glock);

	l_n = bpf_list_front(&ghead);
	if (!l_n) {
		err = __LINE__;
		goto done;
	}

	n = list_entry(l_n, struct node_data, l);
	if (n->key != 0) {
		err = __LINE__;
		goto done;
	}

	l_n = bpf_list_back(&ghead);
	if (!l_n) {
		err = __LINE__;
		goto done;
	}

	n = list_entry(l_n, struct node_data, l);
	if (n->key != NR_NODES - 1) {
		err = __LINE__;
		goto done;
	}

done:
	bpf_spin_unlock(&glock);
	return err;
}

#define TEST_FB(op, dolock)					\
SEC("syscall")							\
__failure __msg(MSG)						\
long test_##op##_spinlock_##dolock(void *ctx)			\
{								\
	struct bpf_list_node *l_n;				\
	__u64 jiffies = 0;					\
								\
	if (dolock)						\
		bpf_spin_lock(&glock);				\
	l_n = bpf_list_##op(&ghead);				\
	if (l_n)						\
		jiffies = bpf_jiffies64();			\
	if (dolock)						\
		bpf_spin_unlock(&glock);			\
								\
	return !!jiffies;					\
}

#define MSG "call bpf_list_{{(front|back).+}}; R0{{(_w)?}}=ptr_or_null_node_data(id={{[0-9]+}},non_own_ref"
TEST_FB(front, true)
TEST_FB(back, true)
#undef MSG

#define MSG "bpf_spin_lock at off=0 must be held for bpf_list_head"
TEST_FB(front, false)
TEST_FB(back, false)
#undef MSG

char _license[] SEC("license") = "GPL";
