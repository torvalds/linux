// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

#define ENOENT 2
#define EEXIST 17

char _license[] SEC("license") = "GPL";

int err;

struct elem {
	char arr[128];
	int val;
};

struct special_elem {
	struct task_struct __kptr *task;
	int val;
};

struct {
	__uint(type, BPF_MAP_TYPE_RHASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 128);
	__type(key, int);
	__type(value, struct elem);
} rhmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RHASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct special_elem);
} special_fields SEC(".maps");

extern struct task_struct *bpf_task_acquire(struct task_struct *p) __ksym;
extern void bpf_task_release(struct task_struct *p) __ksym;

SEC("syscall")
int test_rhash_lookup_update(void *ctx)
{
	int key = 5;
	struct elem empty = {.val = 3, .arr = {0}};
	struct elem *e;

	err = 1;
	e = bpf_map_lookup_elem(&rhmap, &key);
	if (e)
		return 1;

	err = bpf_map_update_elem(&rhmap, &key, &empty, BPF_NOEXIST);
	if (err)
		return 1;

	e = bpf_map_lookup_elem(&rhmap, &key);
	if (!e || e->val != empty.val) {
		err = 2;
		return 2;
	}

	err = 0;
	return 0;
}

SEC("syscall")
int test_rhash_update_delete(void *ctx)
{
	int key = 6;
	struct elem empty = {.val = 4, .arr = {0}};
	struct elem *e;

	err = 1;
	e = bpf_map_lookup_elem(&rhmap, &key);
	if (e)
		return 1;

	err = bpf_map_update_elem(&rhmap, &key, &empty, BPF_NOEXIST);
	if (err)
		return 2;

	err = bpf_map_delete_elem(&rhmap, &key);
	if (err)
		return 3;

	e = bpf_map_lookup_elem(&rhmap, &key);
	if (e) {
		err = 4;
		return 4;
	}

	err = 0;
	return 0;
}

SEC("syscall")
int test_rhash_update_elements(void *ctx)
{
	int key = 0;
	struct elem empty = {.val = 4, .arr = {0}};
	struct elem *e;
	int i;

	err = 1;

	for (i = 0; i < 128; ++i) {
		key = i;
		e = bpf_map_lookup_elem(&rhmap, &key);
		if (e)
			return 1;

		empty.val = key;
		err = bpf_map_update_elem(&rhmap, &key, &empty, BPF_NOEXIST);
		if (err)
			return 2;

		e = bpf_map_lookup_elem(&rhmap, &key);
		if (!e || e->val != key) {
			err = 4;
			return 4;
		}
	}

	for (i = 0; i < 128; ++i) {
		key = i;
		err = bpf_map_delete_elem(&rhmap, &key);
		if (err)
			return 3;

		e = bpf_map_lookup_elem(&rhmap, &key);
		if (e) {
			err = 5;
			return 5;
		}
	}

	err = 0;
	return 0;
}

SEC("syscall")
int test_rhash_update_exist(void *ctx)
{
	int key = 10;
	struct elem val1 = {.val = 100, .arr = {0}};
	struct elem val2 = {.val = 200, .arr = {0}};
	struct elem *e;
	int ret;

	err = 1;

	/* BPF_EXIST on non-existent key should fail with -ENOENT */
	ret = bpf_map_update_elem(&rhmap, &key, &val1, BPF_EXIST);
	if (ret != -ENOENT)
		return 1;

	/* Insert element first */
	ret = bpf_map_update_elem(&rhmap, &key, &val1, BPF_NOEXIST);
	if (ret)
		return 2;

	/* Verify initial value */
	e = bpf_map_lookup_elem(&rhmap, &key);
	if (!e || e->val != 100)
		return 3;

	/* BPF_EXIST on existing key should succeed and update value */
	ret = bpf_map_update_elem(&rhmap, &key, &val2, BPF_EXIST);
	if (ret)
		return 4;

	/* Verify value was updated */
	e = bpf_map_lookup_elem(&rhmap, &key);
	if (!e || e->val != 200)
		return 5;

	/* Cleanup */
	bpf_map_delete_elem(&rhmap, &key);
	err = 0;
	return 0;
}

SEC("syscall")
int test_rhash_update_any(void *ctx)
{
	int key = 11;
	struct elem val1 = {.val = 111, .arr = {0}};
	struct elem val2 = {.val = 222, .arr = {0}};
	struct elem *e;
	int ret;

	err = 1;

	/* BPF_ANY on non-existent key should insert */
	ret = bpf_map_update_elem(&rhmap, &key, &val1, BPF_ANY);
	if (ret)
		return 1;

	e = bpf_map_lookup_elem(&rhmap, &key);
	if (!e || e->val != 111)
		return 2;

	/* BPF_ANY on existing key should update */
	ret = bpf_map_update_elem(&rhmap, &key, &val2, BPF_ANY);
	if (ret)
		return 3;

	e = bpf_map_lookup_elem(&rhmap, &key);
	if (!e || e->val != 222)
		return 4;

	/* Cleanup */
	bpf_map_delete_elem(&rhmap, &key);
	err = 0;
	return 0;
}

SEC("syscall")
int test_rhash_noexist_duplicate(void *ctx)
{
	int key = 12;
	struct elem val = {.val = 600, .arr = {0}};
	int ret;

	err = 1;

	/* Insert element */
	ret = bpf_map_update_elem(&rhmap, &key, &val, BPF_NOEXIST);
	if (ret)
		return 1;

	/* Try to insert again with BPF_NOEXIST - should fail with -EEXIST */
	ret = bpf_map_update_elem(&rhmap, &key, &val, BPF_NOEXIST);
	if (ret != -EEXIST)
		return 2;

	/* Cleanup */
	bpf_map_delete_elem(&rhmap, &key);
	err = 0;
	return 0;
}

SEC("syscall")
int test_rhash_delete_nonexistent(void *ctx)
{
	int key = 99999;
	int ret;

	err = 1;

	/* Delete non-existent key should return -ENOENT */
	ret = bpf_map_delete_elem(&rhmap, &key);
	if (ret != -ENOENT)
		return 1;

	err = 0;
	return 0;
}

SEC("syscall")
int test_rhash_kptr_update(void *ctx)
{
	struct special_elem val1 = { .val = 1 };
	struct special_elem val2 = { .val = 2 };
	struct task_struct *task, *old;
	struct special_elem *elem;
	int key = 0;

	err = 1;
	if (bpf_map_update_elem(&special_fields, &key, &val1, BPF_NOEXIST))
		return 1;

	err = 2;
	elem = bpf_map_lookup_elem(&special_fields, &key);
	if (!elem)
		return 2;

	err = 3;
	task = bpf_task_acquire(bpf_get_current_task_btf());
	if (!task)
		return 3;

	err = 4;
	old = bpf_kptr_xchg(&elem->task, task);
	if (old) {
		bpf_task_release(old);
		return 4;
	}

	err = 5;
	if (bpf_map_update_elem(&special_fields, &key, &val2, BPF_EXIST))
		return 5;

	err = 6;
	elem = bpf_map_lookup_elem(&special_fields, &key);
	if (!elem || elem->val != 2)
		return 6;

	err = 7;
	old = bpf_kptr_xchg(&elem->task, NULL);
	if (!old)
		return 7;
	bpf_task_release(old);

	err = 8;
	if (bpf_map_delete_elem(&special_fields, &key))
		return 8;

	err = 0;
	return 0;
}

SEC("syscall")
int test_rhash_kptr_delete(void *ctx)
{
	struct special_elem val = {};
	struct task_struct *task, *old;
	struct special_elem *elem;
	int key = 0;

	err = 1;
	if (bpf_map_update_elem(&special_fields, &key, &val, BPF_NOEXIST))
		return 1;

	err = 2;
	elem = bpf_map_lookup_elem(&special_fields, &key);
	if (!elem)
		return 2;

	err = 3;
	task = bpf_task_acquire(bpf_get_current_task_btf());
	if (!task)
		return 3;

	err = 4;
	old = bpf_kptr_xchg(&elem->task, task);
	if (old) {
		bpf_task_release(old);
		return 4;
	}

	err = 5;
	if (bpf_map_delete_elem(&special_fields, &key))
		return 5;

	err = 6;
	old = bpf_kptr_xchg(&elem->task, NULL);
	if (!old)
		return 6;
	bpf_task_release(old);

	err = 0;
	return 0;
}
