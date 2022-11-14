// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "test_user_ringbuf.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_USER_RINGBUF);
} user_ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} kernel_ringbuf SEC(".maps");

/* inputs */
int pid, err, val;

int read = 0;

/* Counter used for end-to-end protocol test */
__u64 kern_mutated = 0;
__u64 user_mutated = 0;
__u64 expected_user_mutated = 0;

static int
is_test_process(void)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;

	return cur_pid == pid;
}

static long
record_sample(struct bpf_dynptr *dynptr, void *context)
{
	const struct sample *sample = NULL;
	struct sample stack_sample;
	int status;
	static int num_calls;

	if (num_calls++ % 2 == 0) {
		status = bpf_dynptr_read(&stack_sample, sizeof(stack_sample), dynptr, 0, 0);
		if (status) {
			bpf_printk("bpf_dynptr_read() failed: %d\n", status);
			err = 1;
			return 0;
		}
	} else {
		sample = bpf_dynptr_data(dynptr, 0, sizeof(*sample));
		if (!sample) {
			bpf_printk("Unexpectedly failed to get sample\n");
			err = 2;
			return 0;
		}
		stack_sample = *sample;
	}

	__sync_fetch_and_add(&read, 1);
	return 0;
}

static void
handle_sample_msg(const struct test_msg *msg)
{
	switch (msg->msg_op) {
	case TEST_MSG_OP_INC64:
		kern_mutated += msg->operand_64;
		break;
	case TEST_MSG_OP_INC32:
		kern_mutated += msg->operand_32;
		break;
	case TEST_MSG_OP_MUL64:
		kern_mutated *= msg->operand_64;
		break;
	case TEST_MSG_OP_MUL32:
		kern_mutated *= msg->operand_32;
		break;
	default:
		bpf_printk("Unrecognized op %d\n", msg->msg_op);
		err = 2;
	}
}

static long
read_protocol_msg(struct bpf_dynptr *dynptr, void *context)
{
	const struct test_msg *msg = NULL;

	msg = bpf_dynptr_data(dynptr, 0, sizeof(*msg));
	if (!msg) {
		err = 1;
		bpf_printk("Unexpectedly failed to get msg\n");
		return 0;
	}

	handle_sample_msg(msg);

	return 0;
}

static int publish_next_kern_msg(__u32 index, void *context)
{
	struct test_msg *msg = NULL;
	int operand_64 = TEST_OP_64;
	int operand_32 = TEST_OP_32;

	msg = bpf_ringbuf_reserve(&kernel_ringbuf, sizeof(*msg), 0);
	if (!msg) {
		err = 4;
		return 1;
	}

	switch (index % TEST_MSG_OP_NUM_OPS) {
	case TEST_MSG_OP_INC64:
		msg->operand_64 = operand_64;
		msg->msg_op = TEST_MSG_OP_INC64;
		expected_user_mutated += operand_64;
		break;
	case TEST_MSG_OP_INC32:
		msg->operand_32 = operand_32;
		msg->msg_op = TEST_MSG_OP_INC32;
		expected_user_mutated += operand_32;
		break;
	case TEST_MSG_OP_MUL64:
		msg->operand_64 = operand_64;
		msg->msg_op = TEST_MSG_OP_MUL64;
		expected_user_mutated *= operand_64;
		break;
	case TEST_MSG_OP_MUL32:
		msg->operand_32 = operand_32;
		msg->msg_op = TEST_MSG_OP_MUL32;
		expected_user_mutated *= operand_32;
		break;
	default:
		bpf_ringbuf_discard(msg, 0);
		err = 5;
		return 1;
	}

	bpf_ringbuf_submit(msg, 0);

	return 0;
}

static void
publish_kern_messages(void)
{
	if (expected_user_mutated != user_mutated) {
		bpf_printk("%lu != %lu\n", expected_user_mutated, user_mutated);
		err = 3;
		return;
	}

	bpf_loop(8, publish_next_kern_msg, NULL, 0);
}

SEC("fentry/" SYS_PREFIX "sys_prctl")
int test_user_ringbuf_protocol(void *ctx)
{
	long status = 0;
	struct sample *sample = NULL;
	struct bpf_dynptr ptr;

	if (!is_test_process())
		return 0;

	status = bpf_user_ringbuf_drain(&user_ringbuf, read_protocol_msg, NULL, 0);
	if (status < 0) {
		bpf_printk("Drain returned: %ld\n", status);
		err = 1;
		return 0;
	}

	publish_kern_messages();

	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int test_user_ringbuf(void *ctx)
{
	int status = 0;
	struct sample *sample = NULL;
	struct bpf_dynptr ptr;

	if (!is_test_process())
		return 0;

	err = bpf_user_ringbuf_drain(&user_ringbuf, record_sample, NULL, 0);

	return 0;
}

static long
do_nothing_cb(struct bpf_dynptr *dynptr, void *context)
{
	__sync_fetch_and_add(&read, 1);
	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getrlimit")
int test_user_ringbuf_epoll(void *ctx)
{
	long num_samples;

	if (!is_test_process())
		return 0;

	num_samples = bpf_user_ringbuf_drain(&user_ringbuf, do_nothing_cb, NULL, 0);
	if (num_samples <= 0)
		err = 1;

	return 0;
}
