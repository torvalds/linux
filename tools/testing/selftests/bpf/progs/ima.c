// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 Google LLC.
 */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

long ima_hash_ret = -1;
u64 ima_hash = 0;
u32 monitored_pid = 0;

char _license[] SEC("license") = "GPL";

SEC("lsm.s/bprm_committed_creds")
int BPF_PROG(ima, struct linux_binprm *bprm)
{
	u32 pid = bpf_get_current_pid_tgid() >> 32;

	if (pid == monitored_pid)
		ima_hash_ret = bpf_ima_inode_hash(bprm->file->f_inode,
						  &ima_hash, sizeof(ima_hash));

	return 0;
}
