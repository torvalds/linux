// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

uint32_t tid = 0;
int num_unknown_tid = 0;
int num_known_tid = 0;
void *user_ptr = 0;
void *user_ptr_long = 0;
uint32_t pid = 0;

static char big_str1[5000];
static char big_str2[5005];
static char big_str3[4996];

SEC("iter/task")
int dump_task(struct bpf_iter__task *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;
	static char info[] = "    === END ===";

	if (task == (void *)0) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	if (task->pid != (pid_t)tid)
		num_unknown_tid++;
	else
		num_known_tid++;

	if (ctx->meta->seq_num == 0)
		BPF_SEQ_PRINTF(seq, "    tgid      gid\n");

	BPF_SEQ_PRINTF(seq, "%8d %8d\n", task->tgid, task->pid);
	return 0;
}

int num_expected_failure_copy_from_user_task = 0;
int num_expected_failure_copy_from_user_task_str = 0;
int num_success_copy_from_user_task = 0;
int num_success_copy_from_user_task_str = 0;

SEC("iter.s/task")
int dump_task_sleepable(struct bpf_iter__task *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;
	static const char info[] = "    === END ===";
	struct pt_regs *regs;
	char task_str1[10] = "aaaaaaaaaa";
	char task_str2[10], task_str3[10];
	char task_str4[20] = "aaaaaaaaaaaaaaaaaaaa";
	void *ptr;
	uint32_t user_data = 0;
	int ret;

	if (task == (void *)0) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Read an invalid pointer and ensure we get an error */
	ptr = NULL;
	ret = bpf_copy_from_user_task(&user_data, sizeof(uint32_t), ptr, task, 0);
	if (ret) {
		++num_expected_failure_copy_from_user_task;
	} else {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Try to read the contents of the task's instruction pointer from the
	 * remote task's address space.
	 */
	regs = (struct pt_regs *)bpf_task_pt_regs(task);
	if (regs == (void *)0) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}
	ptr = (void *)PT_REGS_IP(regs);

	ret = bpf_copy_from_user_task(&user_data, sizeof(uint32_t), ptr, task, 0);
	if (ret) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	++num_success_copy_from_user_task;

	/* Read an invalid pointer and ensure we get an error */
	ptr = NULL;
	ret = bpf_copy_from_user_task_str((char *)task_str1, sizeof(task_str1), ptr, task, 0);
	if (ret >= 0 || task_str1[9] != 'a' || task_str1[0] != '\0') {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Read an invalid pointer and ensure we get error with pad zeros flag */
	ptr = NULL;
	ret = bpf_copy_from_user_task_str((char *)task_str1, sizeof(task_str1),
					  ptr, task, BPF_F_PAD_ZEROS);
	if (ret >= 0 || task_str1[9] != '\0' || task_str1[0] != '\0') {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	++num_expected_failure_copy_from_user_task_str;

	/* Same length as the string */
	ret = bpf_copy_from_user_task_str((char *)task_str2, 10, user_ptr, task, 0);
	/* only need to do the task pid check once */
	if (bpf_strncmp(task_str2, 10, "test_data\0") != 0 || ret != 10 || task->tgid != pid) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Shorter length than the string */
	ret = bpf_copy_from_user_task_str((char *)task_str3, 2, user_ptr, task, 0);
	if (bpf_strncmp(task_str3, 2, "t\0") != 0 || ret != 2) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Longer length than the string */
	ret = bpf_copy_from_user_task_str((char *)task_str4, 20, user_ptr, task, 0);
	if (bpf_strncmp(task_str4, 10, "test_data\0") != 0 || ret != 10
	    || task_str4[sizeof(task_str4) - 1] != 'a') {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Longer length than the string with pad zeros flag */
	ret = bpf_copy_from_user_task_str((char *)task_str4, 20, user_ptr, task, BPF_F_PAD_ZEROS);
	if (bpf_strncmp(task_str4, 10, "test_data\0") != 0 || ret != 10
	    || task_str4[sizeof(task_str4) - 1] != '\0') {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Longer length than the string past a page boundary */
	ret = bpf_copy_from_user_task_str(big_str1, 5000, user_ptr, task, 0);
	if (bpf_strncmp(big_str1, 10, "test_data\0") != 0 || ret != 10) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* String that crosses a page boundary */
	ret = bpf_copy_from_user_task_str(big_str1, 5000, user_ptr_long, task, BPF_F_PAD_ZEROS);
	if (bpf_strncmp(big_str1, 4, "baba") != 0 || ret != 5000
	    || bpf_strncmp(big_str1 + 4996, 4, "bab\0") != 0) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	for (int i = 0; i < 4999; ++i) {
		if (i % 2 == 0) {
			if (big_str1[i] != 'b') {
				BPF_SEQ_PRINTF(seq, "%s\n", info);
				return 0;
			}
		} else {
			if (big_str1[i] != 'a') {
				BPF_SEQ_PRINTF(seq, "%s\n", info);
				return 0;
			}
		}
	}

	/* Longer length than the string that crosses a page boundary */
	ret = bpf_copy_from_user_task_str(big_str2, 5005, user_ptr_long, task, BPF_F_PAD_ZEROS);
	if (bpf_strncmp(big_str2, 4, "baba") != 0 || ret != 5000
	    || bpf_strncmp(big_str2 + 4996, 5, "bab\0\0") != 0) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Shorter length than the string that crosses a page boundary */
	ret = bpf_copy_from_user_task_str(big_str3, 4996, user_ptr_long, task, 0);
	if (bpf_strncmp(big_str3, 4, "baba") != 0 || ret != 4996
	    || bpf_strncmp(big_str3 + 4992, 4, "bab\0") != 0) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	++num_success_copy_from_user_task_str;

	if (ctx->meta->seq_num == 0)
		BPF_SEQ_PRINTF(seq, "    tgid      gid     data\n");

	BPF_SEQ_PRINTF(seq, "%8d %8d %8d\n", task->tgid, task->pid, user_data);
	return 0;
}
