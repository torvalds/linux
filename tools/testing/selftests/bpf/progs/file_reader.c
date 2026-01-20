// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <string.h>
#include <stdbool.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "errno.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} arrmap SEC(".maps");

struct elem {
	struct file *file;
	struct bpf_task_work tw;
};

char user_buf[256000];
char tmp_buf[256000];

int pid = 0;
int err, run_success = 0;

static int validate_file_read(struct file *file);
static int task_work_callback(struct bpf_map *map, void *key, void *value);

SEC("lsm/file_open")
int on_open_expect_fault(void *c)
{
	struct bpf_dynptr dynptr;
	struct file *file;
	int local_err = 1;
	__u32 user_buf_sz = sizeof(user_buf);

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	file = bpf_get_task_exe_file(bpf_get_current_task_btf());
	if (!file)
		return 0;

	if (bpf_dynptr_from_file(file, 0, &dynptr))
		goto out;

	local_err = bpf_dynptr_read(tmp_buf, user_buf_sz, &dynptr, user_buf_sz, 0);
	if (local_err == -EFAULT) { /* Expect page fault */
		local_err = 0;
		run_success = 1;
	}
out:
	bpf_dynptr_file_discard(&dynptr);
	if (local_err)
		err = local_err;
	bpf_put_file(file);
	return 0;
}

SEC("lsm/file_open")
int on_open_validate_file_read(void *c)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct elem *work;
	int key = 0;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	work = bpf_map_lookup_elem(&arrmap, &key);
	if (!work) {
		err = 1;
		return 0;
	}
	bpf_task_work_schedule_signal(task, &work->tw, &arrmap, task_work_callback);
	return 0;
}

/* Called in a sleepable context, read 256K bytes, cross check with user space read data */
static int task_work_callback(struct bpf_map *map, void *key, void *value)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct file *file = bpf_get_task_exe_file(task);

	if (!file)
		return 0;

	err = validate_file_read(file);
	if (!err)
		run_success = 1;
	bpf_put_file(file);
	return 0;
}

static int verify_dynptr_read(struct bpf_dynptr *ptr, u32 off, char *user_buf, u32 len)
{
	int i;

	if (bpf_dynptr_read(tmp_buf, len, ptr, off, 0))
		return 1;

	/* Verify file contents read from BPF is the same as the one read from userspace */
	bpf_for(i, 0, len)
	{
		if (tmp_buf[i] != user_buf[i])
			return 1;
	}
	return 0;
}

static int validate_file_read(struct file *file)
{
	struct bpf_dynptr dynptr;
	int loc_err = 1, off;
	__u32 user_buf_sz = sizeof(user_buf);

	if (bpf_dynptr_from_file(file, 0, &dynptr))
		goto cleanup;

	loc_err = verify_dynptr_read(&dynptr, 0, user_buf, user_buf_sz);
	off = 1;
	loc_err = loc_err ?: verify_dynptr_read(&dynptr, off, user_buf + off, user_buf_sz - off);
	off = user_buf_sz - 1;
	loc_err = loc_err ?: verify_dynptr_read(&dynptr, off, user_buf + off, user_buf_sz - off);
	/* Read file with random offset and length */
	off = 4097;
	loc_err = loc_err ?: verify_dynptr_read(&dynptr, off, user_buf + off, 100);

	/* Adjust dynptr, verify read */
	loc_err = loc_err ?: bpf_dynptr_adjust(&dynptr, off, off + 1);
	loc_err = loc_err ?: verify_dynptr_read(&dynptr, 0, user_buf + off, 1);
	/* Can't read more than 1 byte */
	loc_err = loc_err ?: verify_dynptr_read(&dynptr, 0, user_buf + off, 2) == 0;
	/* Can't read with far offset */
	loc_err = loc_err ?: verify_dynptr_read(&dynptr, 1, user_buf + off, 1) == 0;

cleanup:
	bpf_dynptr_file_discard(&dynptr);
	return loc_err;
}
