// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "uptr_test_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct value_lock_type);
} datamap SEC(".maps");

/* load test only. not used */
SEC("syscall")
int not_used(void *ctx)
{
	struct value_lock_type *ptr;
	struct task_struct *task;
	struct user_data *udata;

	task = bpf_get_current_task_btf();
	ptr = bpf_task_storage_get(&datamap, task, 0, 0);
	if (!ptr)
		return 0;

	bpf_spin_lock(&ptr->lock);

	udata = ptr->udata;
	if (!udata) {
		bpf_spin_unlock(&ptr->lock);
		return 0;
	}
	udata->result = MAGIC_VALUE + udata->a + udata->b;

	bpf_spin_unlock(&ptr->lock);

	return 0;
}

char _license[] SEC("license") = "GPL";
