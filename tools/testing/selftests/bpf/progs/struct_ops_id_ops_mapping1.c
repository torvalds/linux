// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

#define bpf_kfunc_multi_st_ops_test_1(args) bpf_kfunc_multi_st_ops_test_1(args, st_ops_id)
int st_ops_id;

int test_pid;
int test_err;

#define MAP1_MAGIC 1234

SEC("struct_ops")
int BPF_PROG(test_1, struct st_ops_args *args)
{
	return MAP1_MAGIC;
}

SEC("tp_btf/sys_enter")
int BPF_PROG(sys_enter, struct pt_regs *regs, long id)
{
	struct st_ops_args args = {};
	struct task_struct *task;
	int ret;

	task = bpf_get_current_task_btf();
	if (!test_pid || task->pid != test_pid)
		return 0;

	ret = bpf_kfunc_multi_st_ops_test_1(&args);
	if (ret != MAP1_MAGIC)
		test_err++;

	return 0;
}

SEC("syscall")
int syscall_prog(void *ctx)
{
	struct st_ops_args args = {};
	int ret;

	ret = bpf_kfunc_multi_st_ops_test_1(&args);
	if (ret != MAP1_MAGIC)
		test_err++;

	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_multi_st_ops st_ops_map = {
	.test_1 = (void *)test_1,
};
