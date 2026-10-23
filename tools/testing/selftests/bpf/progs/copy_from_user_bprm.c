// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <errno.h>

char _license[] SEC("license") = "GPL";

int monitored_pid;
int bprm_argc;
int bprm_envc;
int data_len;
int invalid_flags_ret;
int copy_ret;
int arg0_ret;
int arg1_ret;
int env0_ret;
int env1_ret;
char data[64] = {};
char arg0[32] = {};
char arg1[32] = {};
char env0[32] = {};
char env1[32] = {};

SEC("lsm.s/bprm_check_security")
int BPF_PROG(check_exec_args, struct linux_binprm *bprm)
{
	u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct mm_struct *mm;
	u64 offset = 0;

	if (pid != monitored_pid)
		return 0;

	mm = bprm->mm;
	if (!mm)
		return 0;

	bprm_argc = bprm->argc;
	bprm_envc = bprm->envc;

	/* this is the total size of args and envs starting from bprm->p */
	data_len = bprm->exec - bprm->p;

	invalid_flags_ret = bpf_copy_from_user_mm(data, sizeof(data), (void *)bprm->p, mm, ~0ULL);

	copy_ret = bpf_copy_from_user_mm(data, sizeof(data), (void *)bprm->p, mm, 0);
	if (copy_ret)
		return 0;

	/* arg0 is at bprm->p */
	arg0_ret = bpf_copy_from_user_mm_str(arg0, sizeof(arg0), (void *)(bprm->p + offset), mm, BPF_F_PAD_ZEROS);
	offset += arg0_ret;

	/* arg1 is at bprm->p + arg0_ret */
	arg1_ret = bpf_copy_from_user_mm_str(arg1, sizeof(arg1), (void *)(bprm->p + offset), mm, BPF_F_PAD_ZEROS);
	offset += arg1_ret;

	/* env0 is at bprm->p + arg0_ret + arg1_ret */
	env0_ret = bpf_copy_from_user_mm_str(env0, sizeof(env0), (void *)(bprm->p + offset), mm, BPF_F_PAD_ZEROS);
	offset += env0_ret;

	/* env1 is at bprm->p + arg0_ret + arg1_ret + env0_ret */
	env1_ret = bpf_copy_from_user_mm_str(env1, sizeof(env1), (void *)(bprm->p + offset), mm, BPF_F_PAD_ZEROS);

	return -EPERM;
}
