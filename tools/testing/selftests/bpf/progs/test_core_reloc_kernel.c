// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

struct {
	char in[256];
	char out[256];
	uint64_t my_pid_tgid;
} data = {};

struct core_reloc_kernel_output {
	int valid[10];
	/* we have test_progs[-flavor], so cut flavor part */
	char comm[sizeof("test_progs")];
	int comm_len;
};

struct task_struct {
	int pid;
	int tgid;
	char comm[16];
	struct task_struct *group_leader;
};

#define CORE_READ(dst, src) bpf_core_read(dst, sizeof(*(dst)), src)

SEC("raw_tracepoint/sys_enter")
int test_core_kernel(void *ctx)
{
	struct task_struct *task = (void *)bpf_get_current_task();
	struct core_reloc_kernel_output *out = (void *)&data.out;
	uint64_t pid_tgid = bpf_get_current_pid_tgid();
	uint32_t real_tgid = (uint32_t)pid_tgid;
	int pid, tgid;

	if (data.my_pid_tgid != pid_tgid)
		return 0;

	if (CORE_READ(&pid, &task->pid) ||
	    CORE_READ(&tgid, &task->tgid))
		return 1;

	/* validate pid + tgid matches */
	out->valid[0] = (((uint64_t)pid << 32) | tgid) == pid_tgid;

	/* test variadic BPF_CORE_READ macros */
	out->valid[1] = BPF_CORE_READ(task,
				      tgid) == real_tgid;
	out->valid[2] = BPF_CORE_READ(task,
				      group_leader,
				      tgid) == real_tgid;
	out->valid[3] = BPF_CORE_READ(task,
				      group_leader, group_leader,
				      tgid) == real_tgid;
	out->valid[4] = BPF_CORE_READ(task,
				      group_leader, group_leader, group_leader,
				      tgid) == real_tgid;
	out->valid[5] = BPF_CORE_READ(task,
				      group_leader, group_leader, group_leader,
				      group_leader,
				      tgid) == real_tgid;
	out->valid[6] = BPF_CORE_READ(task,
				      group_leader, group_leader, group_leader,
				      group_leader, group_leader,
				      tgid) == real_tgid;
	out->valid[7] = BPF_CORE_READ(task,
				      group_leader, group_leader, group_leader,
				      group_leader, group_leader, group_leader,
				      tgid) == real_tgid;
	out->valid[8] = BPF_CORE_READ(task,
				      group_leader, group_leader, group_leader,
				      group_leader, group_leader, group_leader,
				      group_leader,
				      tgid) == real_tgid;
	out->valid[9] = BPF_CORE_READ(task,
				      group_leader, group_leader, group_leader,
				      group_leader, group_leader, group_leader,
				      group_leader, group_leader,
				      tgid) == real_tgid;

	/* test BPF_CORE_READ_STR_INTO() returns correct code and contents */
	out->comm_len = BPF_CORE_READ_STR_INTO(
		&out->comm, task,
		group_leader, group_leader, group_leader, group_leader,
		group_leader, group_leader, group_leader, group_leader,
		comm);

	return 0;
}

