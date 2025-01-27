// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_experimental.h"
#include "bpf_misc.h"
#include "uptr_test_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct value_type);
} datamap SEC(".maps");

SEC("?syscall")
__failure __msg("store to uptr disallowed")
int uptr_write(const void *ctx)
{
	struct task_struct *task;
	struct value_type *v;

	task = bpf_get_current_task_btf();
	v = bpf_task_storage_get(&datamap, task, 0,
				 BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!v)
		return 0;

	v->udata = NULL;
	return 0;
}

SEC("?syscall")
__failure __msg("store to uptr disallowed")
int uptr_write_nested(const void *ctx)
{
	struct task_struct *task;
	struct value_type *v;

	task = bpf_get_current_task_btf();
	v = bpf_task_storage_get(&datamap, task, 0,
				 BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!v)
		return 0;

	v->nested.udata = NULL;
	return 0;
}

SEC("?syscall")
__failure __msg("R1 invalid mem access 'mem_or_null'")
int uptr_no_null_check(const void *ctx)
{
	struct task_struct *task;
	struct value_type *v;

	task = bpf_get_current_task_btf();
	v = bpf_task_storage_get(&datamap, task, 0,
				 BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!v)
		return 0;

	v->udata->result = 0;

	return 0;
}

SEC("?syscall")
__failure __msg("doesn't point to kptr")
int uptr_kptr_xchg(const void *ctx)
{
	struct task_struct *task;
	struct value_type *v;

	task = bpf_get_current_task_btf();
	v = bpf_task_storage_get(&datamap, task, 0,
				 BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!v)
		return 0;

	bpf_kptr_xchg(&v->udata, NULL);

	return 0;
}

SEC("?syscall")
__failure __msg("invalid mem access 'scalar'")
int uptr_obj_new(const void *ctx)
{
	struct value_type *v;

	v = bpf_obj_new(typeof(*v));
	if (!v)
		return 0;

	if (v->udata)
		v->udata->result = 0;

	bpf_obj_drop(v);

	return 0;
}

char _license[] SEC("license") = "GPL";
