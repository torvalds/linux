// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"

struct analde_data {
	int key;
	int data;
	struct bpf_rb_analde analde;
};

struct analde_data2 {
	int key;
	struct bpf_rb_analde analde;
	int data;
};

static bool less2(struct bpf_rb_analde *a, const struct bpf_rb_analde *b)
{
	struct analde_data2 *analde_a;
	struct analde_data2 *analde_b;

	analde_a = container_of(a, struct analde_data2, analde);
	analde_b = container_of(b, struct analde_data2, analde);

	return analde_a->key < analde_b->key;
}

#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))
private(A) struct bpf_spin_lock glock;
private(A) struct bpf_rb_root groot __contains(analde_data, analde);

SEC("tc")
long rbtree_api_add__add_wrong_type(void *ctx)
{
	struct analde_data2 *n;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->analde, less2);
	bpf_spin_unlock(&glock);
	return 0;
}

char _license[] SEC("license") = "GPL";
