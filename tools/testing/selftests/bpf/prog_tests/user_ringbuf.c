// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#define _GNU_SOURCE
#include <linux/compiler.h>
#include <linux/ring_buffer.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <test_progs.h>
#include <uapi/linux/bpf.h>
#include <unistd.h>

#include "user_ringbuf_fail.skel.h"
#include "user_ringbuf_success.skel.h"

#include "../progs/test_user_ringbuf.h"

static const long c_sample_size = sizeof(struct sample) + BPF_RINGBUF_HDR_SZ;
static const long c_ringbuf_size = 1 << 12; /* 1 small page */
static const long c_max_entries = c_ringbuf_size / c_sample_size;

static void drain_current_samples(void)
{
	syscall(__NR_getpgid);
}

static int write_samples(struct user_ring_buffer *ringbuf, uint32_t num_samples)
{
	int i, err = 0;

	/* Write some number of samples to the ring buffer. */
	for (i = 0; i < num_samples; i++) {
		struct sample *entry;
		int read;

		entry = user_ring_buffer__reserve(ringbuf, sizeof(*entry));
		if (!entry) {
			err = -errno;
			goto done;
		}

		entry->pid = getpid();
		entry->seq = i;
		entry->value = i * i;

		read = snprintf(entry->comm, sizeof(entry->comm), "%u", i);
		if (read <= 0) {
			/* Assert on the error path to avoid spamming logs with
			 * mostly success messages.
			 */
			ASSERT_GT(read, 0, "snprintf_comm");
			err = read;
			user_ring_buffer__discard(ringbuf, entry);
			goto done;
		}

		user_ring_buffer__submit(ringbuf, entry);
	}

done:
	drain_current_samples();

	return err;
}

static struct user_ringbuf_success *open_load_ringbuf_skel(void)
{
	struct user_ringbuf_success *skel;
	int err;

	skel = user_ringbuf_success__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return NULL;

	err = bpf_map__set_max_entries(skel->maps.user_ringbuf, c_ringbuf_size);
	if (!ASSERT_OK(err, "set_max_entries"))
		goto cleanup;

	err = bpf_map__set_max_entries(skel->maps.kernel_ringbuf, c_ringbuf_size);
	if (!ASSERT_OK(err, "set_max_entries"))
		goto cleanup;

	err = user_ringbuf_success__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	return skel;

cleanup:
	user_ringbuf_success__destroy(skel);
	return NULL;
}

static void test_user_ringbuf_mappings(void)
{
	int err, rb_fd;
	int page_size = getpagesize();
	void *mmap_ptr;
	struct user_ringbuf_success *skel;

	skel = open_load_ringbuf_skel();
	if (!skel)
		return;

	rb_fd = bpf_map__fd(skel->maps.user_ringbuf);
	/* cons_pos can be mapped R/O, can't add +X with mprotect. */
	mmap_ptr = mmap(NULL, page_size, PROT_READ, MAP_SHARED, rb_fd, 0);
	ASSERT_OK_PTR(mmap_ptr, "ro_cons_pos");
	ASSERT_ERR(mprotect(mmap_ptr, page_size, PROT_WRITE), "write_cons_pos_protect");
	ASSERT_ERR(mprotect(mmap_ptr, page_size, PROT_EXEC), "exec_cons_pos_protect");
	ASSERT_ERR_PTR(mremap(mmap_ptr, 0, 4 * page_size, MREMAP_MAYMOVE), "wr_prod_pos");
	err = -errno;
	ASSERT_ERR(err, "wr_prod_pos_err");
	ASSERT_OK(munmap(mmap_ptr, page_size), "unmap_ro_cons");

	/* prod_pos can be mapped RW, can't add +X with mprotect. */
	mmap_ptr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED,
			rb_fd, page_size);
	ASSERT_OK_PTR(mmap_ptr, "rw_prod_pos");
	ASSERT_ERR(mprotect(mmap_ptr, page_size, PROT_EXEC), "exec_prod_pos_protect");
	err = -errno;
	ASSERT_ERR(err, "wr_prod_pos_err");
	ASSERT_OK(munmap(mmap_ptr, page_size), "unmap_rw_prod");

	/* data pages can be mapped RW, can't add +X with mprotect. */
	mmap_ptr = mmap(NULL, page_size, PROT_WRITE, MAP_SHARED, rb_fd,
			2 * page_size);
	ASSERT_OK_PTR(mmap_ptr, "rw_data");
	ASSERT_ERR(mprotect(mmap_ptr, page_size, PROT_EXEC), "exec_data_protect");
	err = -errno;
	ASSERT_ERR(err, "exec_data_err");
	ASSERT_OK(munmap(mmap_ptr, page_size), "unmap_rw_data");

	user_ringbuf_success__destroy(skel);
}

static int load_skel_create_ringbufs(struct user_ringbuf_success **skel_out,
				     struct ring_buffer **kern_ringbuf_out,
				     ring_buffer_sample_fn callback,
				     struct user_ring_buffer **user_ringbuf_out)
{
	struct user_ringbuf_success *skel;
	struct ring_buffer *kern_ringbuf = NULL;
	struct user_ring_buffer *user_ringbuf = NULL;
	int err = -ENOMEM, rb_fd;

	skel = open_load_ringbuf_skel();
	if (!skel)
		return err;

	/* only trigger BPF program for current process */
	skel->bss->pid = getpid();

	if (kern_ringbuf_out) {
		rb_fd = bpf_map__fd(skel->maps.kernel_ringbuf);
		kern_ringbuf = ring_buffer__new(rb_fd, callback, skel, NULL);
		if (!ASSERT_OK_PTR(kern_ringbuf, "kern_ringbuf_create"))
			goto cleanup;

		*kern_ringbuf_out = kern_ringbuf;
	}

	if (user_ringbuf_out) {
		rb_fd = bpf_map__fd(skel->maps.user_ringbuf);
		user_ringbuf = user_ring_buffer__new(rb_fd, NULL);
		if (!ASSERT_OK_PTR(user_ringbuf, "user_ringbuf_create"))
			goto cleanup;

		*user_ringbuf_out = user_ringbuf;
		ASSERT_EQ(skel->bss->read, 0, "no_reads_after_load");
	}

	err = user_ringbuf_success__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	*skel_out = skel;
	return 0;

cleanup:
	if (kern_ringbuf_out)
		*kern_ringbuf_out = NULL;
	if (user_ringbuf_out)
		*user_ringbuf_out = NULL;
	ring_buffer__free(kern_ringbuf);
	user_ring_buffer__free(user_ringbuf);
	user_ringbuf_success__destroy(skel);
	return err;
}

static int load_skel_create_user_ringbuf(struct user_ringbuf_success **skel_out,
					 struct user_ring_buffer **ringbuf_out)
{
	return load_skel_create_ringbufs(skel_out, NULL, NULL, ringbuf_out);
}

static void manually_write_test_invalid_sample(struct user_ringbuf_success *skel,
					       __u32 size, __u64 producer_pos, int err)
{
	void *data_ptr;
	__u64 *producer_pos_ptr;
	int rb_fd, page_size = getpagesize();

	rb_fd = bpf_map__fd(skel->maps.user_ringbuf);

	ASSERT_EQ(skel->bss->read, 0, "num_samples_before_bad_sample");

	/* Map the producer_pos as RW. */
	producer_pos_ptr = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, rb_fd, page_size);
	ASSERT_OK_PTR(producer_pos_ptr, "producer_pos_ptr");

	/* Map the data pages as RW. */
	data_ptr = mmap(NULL, page_size, PROT_WRITE, MAP_SHARED, rb_fd, 2 * page_size);
	ASSERT_OK_PTR(data_ptr, "rw_data");

	memset(data_ptr, 0, BPF_RINGBUF_HDR_SZ);
	*(__u32 *)data_ptr = size;

	/* Synchronizes with smp_load_acquire() in __bpf_user_ringbuf_peek() in the kernel. */
	smp_store_release(producer_pos_ptr, producer_pos + BPF_RINGBUF_HDR_SZ);

	drain_current_samples();
	ASSERT_EQ(skel->bss->read, 0, "num_samples_after_bad_sample");
	ASSERT_EQ(skel->bss->err, err, "err_after_bad_sample");

	ASSERT_OK(munmap(producer_pos_ptr, page_size), "unmap_producer_pos");
	ASSERT_OK(munmap(data_ptr, page_size), "unmap_data_ptr");
}

static void test_user_ringbuf_post_misaligned(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	int err;
	__u32 size = (1 << 5) + 7;

	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (!ASSERT_OK(err, "misaligned_skel"))
		return;

	manually_write_test_invalid_sample(skel, size, size, -EINVAL);
	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

static void test_user_ringbuf_post_producer_wrong_offset(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	int err;
	__u32 size = (1 << 5);

	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (!ASSERT_OK(err, "wrong_offset_skel"))
		return;

	manually_write_test_invalid_sample(skel, size, size - 8, -EINVAL);
	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

static void test_user_ringbuf_post_larger_than_ringbuf_sz(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	int err;
	__u32 size = c_ringbuf_size;

	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (!ASSERT_OK(err, "huge_sample_skel"))
		return;

	manually_write_test_invalid_sample(skel, size, size, -E2BIG);
	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

static void test_user_ringbuf_basic(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	int err;

	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (!ASSERT_OK(err, "ringbuf_basic_skel"))
		return;

	ASSERT_EQ(skel->bss->read, 0, "num_samples_read_before");

	err = write_samples(ringbuf, 2);
	if (!ASSERT_OK(err, "write_samples"))
		goto cleanup;

	ASSERT_EQ(skel->bss->read, 2, "num_samples_read_after");

cleanup:
	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

static void test_user_ringbuf_sample_full_ring_buffer(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	int err;
	void *sample;

	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (!ASSERT_OK(err, "ringbuf_full_sample_skel"))
		return;

	sample = user_ring_buffer__reserve(ringbuf, c_ringbuf_size - BPF_RINGBUF_HDR_SZ);
	if (!ASSERT_OK_PTR(sample, "full_sample"))
		goto cleanup;

	user_ring_buffer__submit(ringbuf, sample);
	ASSERT_EQ(skel->bss->read, 0, "num_samples_read_before");
	drain_current_samples();
	ASSERT_EQ(skel->bss->read, 1, "num_samples_read_after");

cleanup:
	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

static void test_user_ringbuf_post_alignment_autoadjust(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	struct sample *sample;
	int err;

	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (!ASSERT_OK(err, "ringbuf_align_autoadjust_skel"))
		return;

	/* libbpf should automatically round any sample up to an 8-byte alignment. */
	sample = user_ring_buffer__reserve(ringbuf, sizeof(*sample) + 1);
	ASSERT_OK_PTR(sample, "reserve_autoaligned");
	user_ring_buffer__submit(ringbuf, sample);

	ASSERT_EQ(skel->bss->read, 0, "num_samples_read_before");
	drain_current_samples();
	ASSERT_EQ(skel->bss->read, 1, "num_samples_read_after");

	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

static void test_user_ringbuf_overfill(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	int err;

	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (err)
		return;

	err = write_samples(ringbuf, c_max_entries * 5);
	ASSERT_ERR(err, "write_samples");
	ASSERT_EQ(skel->bss->read, c_max_entries, "max_entries");

	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

static void test_user_ringbuf_discards_properly_ignored(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	int err, num_discarded = 0;
	__u64 *token;

	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (err)
		return;

	ASSERT_EQ(skel->bss->read, 0, "num_samples_read_before");

	while (1) {
		/* Write samples until the buffer is full. */
		token = user_ring_buffer__reserve(ringbuf, sizeof(*token));
		if (!token)
			break;

		user_ring_buffer__discard(ringbuf, token);
		num_discarded++;
	}

	if (!ASSERT_GE(num_discarded, 0, "num_discarded"))
		goto cleanup;

	/* Should not read any samples, as they are all discarded. */
	ASSERT_EQ(skel->bss->read, 0, "num_pre_kick");
	drain_current_samples();
	ASSERT_EQ(skel->bss->read, 0, "num_post_kick");

	/* Now that the ring buffer has been drained, we should be able to
	 * reserve another token.
	 */
	token = user_ring_buffer__reserve(ringbuf, sizeof(*token));

	if (!ASSERT_OK_PTR(token, "new_token"))
		goto cleanup;

	user_ring_buffer__discard(ringbuf, token);
cleanup:
	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

static void test_user_ringbuf_loop(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	uint32_t total_samples = 8192;
	uint32_t remaining_samples = total_samples;
	int err;

	BUILD_BUG_ON(total_samples <= c_max_entries);
	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (err)
		return;

	do  {
		uint32_t curr_samples;

		curr_samples = remaining_samples > c_max_entries
			? c_max_entries : remaining_samples;
		err = write_samples(ringbuf, curr_samples);
		if (err != 0) {
			/* Assert inside of if statement to avoid flooding logs
			 * on the success path.
			 */
			ASSERT_OK(err, "write_samples");
			goto cleanup;
		}

		remaining_samples -= curr_samples;
		ASSERT_EQ(skel->bss->read, total_samples - remaining_samples,
			  "current_batched_entries");
	} while (remaining_samples > 0);
	ASSERT_EQ(skel->bss->read, total_samples, "total_batched_entries");

cleanup:
	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

static int send_test_message(struct user_ring_buffer *ringbuf,
			     enum test_msg_op op, s64 operand_64,
			     s32 operand_32)
{
	struct test_msg *msg;

	msg = user_ring_buffer__reserve(ringbuf, sizeof(*msg));
	if (!msg) {
		/* Assert on the error path to avoid spamming logs with mostly
		 * success messages.
		 */
		ASSERT_OK_PTR(msg, "reserve_msg");
		return -ENOMEM;
	}

	msg->msg_op = op;

	switch (op) {
	case TEST_MSG_OP_INC64:
	case TEST_MSG_OP_MUL64:
		msg->operand_64 = operand_64;
		break;
	case TEST_MSG_OP_INC32:
	case TEST_MSG_OP_MUL32:
		msg->operand_32 = operand_32;
		break;
	default:
		PRINT_FAIL("Invalid operand %d\n", op);
		user_ring_buffer__discard(ringbuf, msg);
		return -EINVAL;
	}

	user_ring_buffer__submit(ringbuf, msg);

	return 0;
}

static void kick_kernel_read_messages(void)
{
	syscall(__NR_prctl);
}

static int handle_kernel_msg(void *ctx, void *data, size_t len)
{
	struct user_ringbuf_success *skel = ctx;
	struct test_msg *msg = data;

	switch (msg->msg_op) {
	case TEST_MSG_OP_INC64:
		skel->bss->user_mutated += msg->operand_64;
		return 0;
	case TEST_MSG_OP_INC32:
		skel->bss->user_mutated += msg->operand_32;
		return 0;
	case TEST_MSG_OP_MUL64:
		skel->bss->user_mutated *= msg->operand_64;
		return 0;
	case TEST_MSG_OP_MUL32:
		skel->bss->user_mutated *= msg->operand_32;
		return 0;
	default:
		fprintf(stderr, "Invalid operand %d\n", msg->msg_op);
		return -EINVAL;
	}
}

static void drain_kernel_messages_buffer(struct ring_buffer *kern_ringbuf,
					 struct user_ringbuf_success *skel)
{
	int cnt;

	cnt = ring_buffer__consume(kern_ringbuf);
	ASSERT_EQ(cnt, 8, "consume_kern_ringbuf");
	ASSERT_OK(skel->bss->err, "consume_kern_ringbuf_err");
}

static void test_user_ringbuf_msg_protocol(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *user_ringbuf;
	struct ring_buffer *kern_ringbuf;
	int err, i;
	__u64 expected_kern = 0;

	err = load_skel_create_ringbufs(&skel, &kern_ringbuf, handle_kernel_msg, &user_ringbuf);
	if (!ASSERT_OK(err, "create_ringbufs"))
		return;

	for (i = 0; i < 64; i++) {
		enum test_msg_op op = i % TEST_MSG_OP_NUM_OPS;
		__u64 operand_64 = TEST_OP_64;
		__u32 operand_32 = TEST_OP_32;

		err = send_test_message(user_ringbuf, op, operand_64, operand_32);
		if (err) {
			/* Only assert on a failure to avoid spamming success logs. */
			ASSERT_OK(err, "send_test_message");
			goto cleanup;
		}

		switch (op) {
		case TEST_MSG_OP_INC64:
			expected_kern += operand_64;
			break;
		case TEST_MSG_OP_INC32:
			expected_kern += operand_32;
			break;
		case TEST_MSG_OP_MUL64:
			expected_kern *= operand_64;
			break;
		case TEST_MSG_OP_MUL32:
			expected_kern *= operand_32;
			break;
		default:
			PRINT_FAIL("Unexpected op %d\n", op);
			goto cleanup;
		}

		if (i % 8 == 0) {
			kick_kernel_read_messages();
			ASSERT_EQ(skel->bss->kern_mutated, expected_kern, "expected_kern");
			ASSERT_EQ(skel->bss->err, 0, "bpf_prog_err");
			drain_kernel_messages_buffer(kern_ringbuf, skel);
		}
	}

cleanup:
	ring_buffer__free(kern_ringbuf);
	user_ring_buffer__free(user_ringbuf);
	user_ringbuf_success__destroy(skel);
}

static void *kick_kernel_cb(void *arg)
{
	/* Kick the kernel, causing it to drain the ring buffer and then wake
	 * up the test thread waiting on epoll.
	 */
	syscall(__NR_prlimit64);

	return NULL;
}

static int spawn_kick_thread_for_poll(void)
{
	pthread_t thread;

	return pthread_create(&thread, NULL, kick_kernel_cb, NULL);
}

static void test_user_ringbuf_blocking_reserve(void)
{
	struct user_ringbuf_success *skel;
	struct user_ring_buffer *ringbuf;
	int err, num_written = 0;
	__u64 *token;

	err = load_skel_create_user_ringbuf(&skel, &ringbuf);
	if (err)
		return;

	ASSERT_EQ(skel->bss->read, 0, "num_samples_read_before");

	while (1) {
		/* Write samples until the buffer is full. */
		token = user_ring_buffer__reserve(ringbuf, sizeof(*token));
		if (!token)
			break;

		*token = 0xdeadbeef;

		user_ring_buffer__submit(ringbuf, token);
		num_written++;
	}

	if (!ASSERT_GE(num_written, 0, "num_written"))
		goto cleanup;

	/* Should not have read any samples until the kernel is kicked. */
	ASSERT_EQ(skel->bss->read, 0, "num_pre_kick");

	/* We correctly time out after 1 second, without a sample. */
	token = user_ring_buffer__reserve_blocking(ringbuf, sizeof(*token), 1000);
	if (!ASSERT_EQ(token, NULL, "pre_kick_timeout_token"))
		goto cleanup;

	err = spawn_kick_thread_for_poll();
	if (!ASSERT_EQ(err, 0, "deferred_kick_thread\n"))
		goto cleanup;

	/* After spawning another thread that asychronously kicks the kernel to
	 * drain the messages, we're able to block and successfully get a
	 * sample once we receive an event notification.
	 */
	token = user_ring_buffer__reserve_blocking(ringbuf, sizeof(*token), 10000);

	if (!ASSERT_OK_PTR(token, "block_token"))
		goto cleanup;

	ASSERT_GT(skel->bss->read, 0, "num_post_kill");
	ASSERT_LE(skel->bss->read, num_written, "num_post_kill");
	ASSERT_EQ(skel->bss->err, 0, "err_post_poll");
	user_ring_buffer__discard(ringbuf, token);

cleanup:
	user_ring_buffer__free(ringbuf);
	user_ringbuf_success__destroy(skel);
}

#define SUCCESS_TEST(_func) { _func, #_func }

static struct {
	void (*test_callback)(void);
	const char *test_name;
} success_tests[] = {
	SUCCESS_TEST(test_user_ringbuf_mappings),
	SUCCESS_TEST(test_user_ringbuf_post_misaligned),
	SUCCESS_TEST(test_user_ringbuf_post_producer_wrong_offset),
	SUCCESS_TEST(test_user_ringbuf_post_larger_than_ringbuf_sz),
	SUCCESS_TEST(test_user_ringbuf_basic),
	SUCCESS_TEST(test_user_ringbuf_sample_full_ring_buffer),
	SUCCESS_TEST(test_user_ringbuf_post_alignment_autoadjust),
	SUCCESS_TEST(test_user_ringbuf_overfill),
	SUCCESS_TEST(test_user_ringbuf_discards_properly_ignored),
	SUCCESS_TEST(test_user_ringbuf_loop),
	SUCCESS_TEST(test_user_ringbuf_msg_protocol),
	SUCCESS_TEST(test_user_ringbuf_blocking_reserve),
};

void test_user_ringbuf(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(success_tests); i++) {
		if (!test__start_subtest(success_tests[i].test_name))
			continue;

		success_tests[i].test_callback();
	}

	RUN_TESTS(user_ringbuf_fail);
}
