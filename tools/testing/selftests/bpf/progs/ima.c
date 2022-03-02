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

SEC("lsm.s/bprm_committed_creds")
void BPF_PROG(bprm_committed_creds, struct linux_binprm *bprm)
{
	ima_test_common(bprm->file);
}
