// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2024. Huawei Technologies Co., Ltd */
#include "vmlinux.h"
#include <linux/types.h>
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"

int target_pid = 0;
void *user_ptr = 0;
int read_ret[10];

char _license[] SEC("license") = "GPL";

/*
 * These are the kfuncs, the others are helpers
 */
int bpf_copy_from_user_str(void *dst, u32, const void *, u64) __weak __ksym;
int bpf_copy_from_user_task_str(void *dst, u32, const void *,
				struct task_struct *, u64) __weak __ksym;

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int do_probe_read(void *ctx)
{
	char buf[8];

	if ((bpf_get_current_pid_tgid() >> 32) != target_pid)
		return 0;

	read_ret[0] = bpf_probe_read_kernel(buf, sizeof(buf), user_ptr);
	read_ret[1] = bpf_probe_read_kernel_str(buf, sizeof(buf), user_ptr);
	read_ret[2] = bpf_probe_read(buf, sizeof(buf), user_ptr);
	read_ret[3] = bpf_probe_read_str(buf, sizeof(buf), user_ptr);
	read_ret[4] = bpf_probe_read_user(buf, sizeof(buf), user_ptr);
	read_ret[5] = bpf_probe_read_user_str(buf, sizeof(buf), user_ptr);

	return 0;
}

SEC("fentry.s/" SYS_PREFIX "sys_nanosleep")
int do_copy_from_user(void *ctx)
{
	char buf[8];

	if ((bpf_get_current_pid_tgid() >> 32) != target_pid)
		return 0;

	read_ret[6] = bpf_copy_from_user(buf, sizeof(buf), user_ptr);
	read_ret[7] = bpf_copy_from_user_task(buf, sizeof(buf), user_ptr,
					      bpf_get_current_task_btf(), 0);
	read_ret[8] = bpf_copy_from_user_str((char *)buf, sizeof(buf), user_ptr, 0);
	read_ret[9] = bpf_copy_from_user_task_str((char *)buf,
						  sizeof(buf),
						  user_ptr,
						  bpf_get_current_task_btf(),
						  0);

	return 0;
}
