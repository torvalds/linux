// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 KylinSoft Corporation. */
#include <vmlinux.h>
#include <stdbool.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

#define TASK_WORK_RACE_MAX_SEQ	2048

enum task_work_race_status {
	RACE_ARM_SEQ,
	RACE_READY_SEQ,
	/* High 32 bits hold the sequence, low 32 bits the signed error. */
	RACE_RESULT,
	RACE_STATUS_MAX,
};

int trigger_tid;
int target_tid;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, RACE_STATUS_MAX);
	__type(key, int);
	__type(value, __s64);
} status SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, TASK_WORK_RACE_MAX_SEQ);
	__type(key, int);
	__type(value, __u64);
} completed SEC(".maps");

struct task_work_race_value {
	__u32 seq;
	char data[60];
	struct bpf_task_work tw;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 4);
	__type(key, int);
	__type(value, struct task_work_race_value);
} hmap SEC(".maps");

static __always_inline void set_status(int key, __s64 value)
{
	__s64 *slot;

	slot = bpf_map_lookup_elem(&status, &key);
	if (slot)
		*slot = value;
}

static int process_work(struct bpf_map *map, void *key, void *value)
{
	struct task_work_race_value *work = value;
	__u64 *done;
	int seq = work->seq;

	if (seq <= 0 || seq >= TASK_WORK_RACE_MAX_SEQ)
		return 0;
	done = bpf_map_lookup_elem(&completed, &seq);
	if (done)
		*done = 1;
	return 0;
}

SEC("tracepoint/syscalls/sys_enter_getppid")
int race_sched_work(void *ctx)
{
	struct task_work_race_value *work;
	struct task_struct *task;
	__s64 *arm, *result;
	__u32 tid = (__u32)bpf_get_current_pid_tgid();
	int key = 0, err;
	__u32 seq;

	if (tid != trigger_tid)
		return 0;

	key = RACE_ARM_SEQ;
	arm = bpf_map_lookup_elem(&status, &key);
	key = RACE_RESULT;
	result = bpf_map_lookup_elem(&status, &key);
	if (!arm || !result || *arm <= 0 ||
	    (__u32)((__u64)*result >> 32) == *arm)
		return 0;
	seq = *arm;

	task = bpf_task_from_pid(target_tid);
	if (!task) {
		err = -ESRCH;
		goto out_done;
	}

	key = 0;
	work = bpf_map_lookup_elem(&hmap, &key);
	if (!work) {
		err = -ENOENT;
		goto out_task;
	}

	work->seq = seq;
	set_status(RACE_READY_SEQ, seq);
	err = bpf_task_work_schedule_signal(task, &work->tw, &hmap,
					    process_work);
	if (err == -EBUSY) {
		bpf_task_release(task);
		return 0;
	}

out_task:
	bpf_task_release(task);
out_done:
	/* Publish the sequence and its error in one store. */
	*result = ((__u64)seq << 32) | (__u32)err;
	return 0;
}
