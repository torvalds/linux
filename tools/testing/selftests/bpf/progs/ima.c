// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 Google LLC.
 */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

u32 monitored_pid = 0;

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 12);
} ringbuf SEC(".maps");

char _license[] SEC("license") = "GPL";

bool use_ima_file_hash;
bool enable_bprm_creds_for_exec;
bool enable_kernel_read_file;
bool test_deny;

static void ima_test_common(struct file *file)
{
	u64 ima_hash = 0;
	u64 *sample;
	int ret;
	u32 pid;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid == monitored_pid) {
		if (!use_ima_file_hash)
			ret = bpf_ima_inode_hash(file->f_inode, &ima_hash,
						 sizeof(ima_hash));
		else
			ret = bpf_ima_file_hash(file, &ima_hash,
						sizeof(ima_hash));
		if (ret < 0 || ima_hash == 0)
			return;

		sample = bpf_ringbuf_reserve(&ringbuf, sizeof(u64), 0);
		if (!sample)
			return;

		*sample = ima_hash;
		bpf_ringbuf_submit(sample, 0);
	}

	return;
}

static int ima_test_deny(void)
{
	u32 pid;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid == monitored_pid && test_deny)
		return -EPERM;

	return 0;
}

SEC("lsm.s/bprm_committed_creds")
void BPF_PROG(bprm_committed_creds, struct linux_binprm *bprm)
{
	ima_test_common(bprm->file);
}

SEC("lsm.s/bprm_creds_for_exec")
int BPF_PROG(bprm_creds_for_exec, struct linux_binprm *bprm)
{
	if (!enable_bprm_creds_for_exec)
		return 0;

	ima_test_common(bprm->file);
	return 0;
}

SEC("lsm.s/kernel_read_file")
int BPF_PROG(kernel_read_file, struct file *file, enum kernel_read_file_id id,
	     bool contents)
{
	int ret;

	if (!enable_kernel_read_file)
		return 0;

	if (!contents)
		return 0;

	if (id != READING_POLICY)
		return 0;

	ret = ima_test_deny();
	if (ret < 0)
		return ret;

	ima_test_common(file);
	return 0;
}
