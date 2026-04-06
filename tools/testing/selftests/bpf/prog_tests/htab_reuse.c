// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#define _GNU_SOURCE
#include <sched.h>
#include <stdbool.h>
#include <test_progs.h>
#include "htab_reuse.skel.h"

struct htab_op_ctx {
	int fd;
	int loop;
	bool stop;
};

struct htab_val {
	unsigned int lock;
	unsigned int data;
};

static void *htab_lookup_fn(void *arg)
{
	struct htab_op_ctx *ctx = arg;
	int i = 0;

	while (i++ < ctx->loop && !ctx->stop) {
		struct htab_val value;
		unsigned int key;

		/* Use BPF_F_LOCK to use spin-lock in map value. */
		key = 7;
		bpf_map_lookup_elem_flags(ctx->fd, &key, &value, BPF_F_LOCK);
	}

	return NULL;
}

static void *htab_update_fn(void *arg)
{
	struct htab_op_ctx *ctx = arg;
	int i = 0;

	while (i++ < ctx->loop && !ctx->stop) {
		struct htab_val value;
		unsigned int key;

		key = 7;
		value.lock = 0;
		value.data = key;
		bpf_map_update_elem(ctx->fd, &key, &value, BPF_F_LOCK);
		bpf_map_delete_elem(ctx->fd, &key);

		key = 24;
		value.lock = 0;
		value.data = key;
		bpf_map_update_elem(ctx->fd, &key, &value, BPF_F_LOCK);
		bpf_map_delete_elem(ctx->fd, &key);
	}

	return NULL;
}

static void test_htab_reuse_basic(void)
{
	unsigned int i, wr_nr = 1, rd_nr = 4;
	pthread_t tids[wr_nr + rd_nr];
	struct htab_reuse *skel;
	struct htab_op_ctx ctx;
	int err;

	skel = htab_reuse__open_and_load();
	if (!ASSERT_OK_PTR(skel, "htab_reuse__open_and_load"))
		return;

	ctx.fd = bpf_map__fd(skel->maps.htab);
	ctx.loop = 500;
	ctx.stop = false;

	memset(tids, 0, sizeof(tids));
	for (i = 0; i < wr_nr; i++) {
		err = pthread_create(&tids[i], NULL, htab_update_fn, &ctx);
		if (!ASSERT_OK(err, "pthread_create")) {
			ctx.stop = true;
			goto reap;
		}
	}
	for (i = 0; i < rd_nr; i++) {
		err = pthread_create(&tids[i + wr_nr], NULL, htab_lookup_fn, &ctx);
		if (!ASSERT_OK(err, "pthread_create")) {
			ctx.stop = true;
			goto reap;
		}
	}

reap:
	for (i = 0; i < wr_nr + rd_nr; i++) {
		if (!tids[i])
			continue;
		pthread_join(tids[i], NULL);
	}
	htab_reuse__destroy(skel);
}

/*
 * Writes consistency test for BPF_F_LOCK update
 *
 * The race:
 *   1. Thread A: BPF_F_LOCK|BPF_EXIST update
 *   2. Thread B: delete element then update it with BPF_ANY
 */

struct htab_val_large {
	struct bpf_spin_lock lock;
	__u32 seq;
	__u64 data[256];
};

struct consistency_ctx {
	int fd;
	int start_fd;
	int loop;
	volatile bool torn_write;
};

static void wait_for_start(int fd)
{
	char buf;

	read(fd, &buf, 1);
}

static void *locked_update_fn(void *arg)
{
	struct consistency_ctx *ctx = arg;
	struct htab_val_large value;
	unsigned int key = 1;
	int i;

	memset(&value, 0xAA, sizeof(value));
	wait_for_start(ctx->start_fd);

	for (i = 0; i < ctx->loop; i++) {
		value.seq = i;
		bpf_map_update_elem(ctx->fd, &key, &value,
				    BPF_F_LOCK | BPF_EXIST);
	}

	return NULL;
}

/* Delete + update: removes the element then re-creates it with BPF_ANY. */
static void *delete_update_fn(void *arg)
{
	struct consistency_ctx *ctx = arg;
	struct htab_val_large value;
	unsigned int key = 1;
	int i;

	memset(&value, 0xBB, sizeof(value));

	wait_for_start(ctx->start_fd);

	for (i = 0; i < ctx->loop; i++) {
		value.seq = i;
		bpf_map_delete_elem(ctx->fd, &key);
		bpf_map_update_elem(ctx->fd, &key, &value, BPF_ANY | BPF_F_LOCK);
	}

	return NULL;
}

static void *locked_lookup_fn(void *arg)
{
	struct consistency_ctx *ctx = arg;
	struct htab_val_large value;
	unsigned int key = 1;
	int i, j;

	wait_for_start(ctx->start_fd);

	for (i = 0; i < ctx->loop && !ctx->torn_write; i++) {
		if (bpf_map_lookup_elem_flags(ctx->fd, &key, &value, BPF_F_LOCK))
			continue;

		for (j = 0; j < 256; j++) {
			if (value.data[j] != value.data[0]) {
				ctx->torn_write = true;
				return NULL;
			}
		}
	}

	return NULL;
}

static void test_htab_reuse_consistency(void)
{
	int threads_total = 6, threads = 2;
	pthread_t tids[threads_total];
	struct consistency_ctx ctx;
	struct htab_val_large seed;
	struct htab_reuse *skel;
	unsigned int key = 1, i;
	int pipefd[2];
	int err;

	skel = htab_reuse__open_and_load();
	if (!ASSERT_OK_PTR(skel, "htab_reuse__open_and_load"))
		return;

	if (!ASSERT_OK(pipe(pipefd), "pipe"))
		goto out;

	ctx.fd = bpf_map__fd(skel->maps.htab_lock_consistency);
	ctx.start_fd = pipefd[0];
	ctx.loop = 100000;
	ctx.torn_write = false;

	/* Seed the element so locked updaters have something to find */
	memset(&seed, 0xBB, sizeof(seed));
	err = bpf_map_update_elem(ctx.fd, &key, &seed, BPF_ANY);
	if (!ASSERT_OK(err, "seed_element"))
		goto close_pipe;

	memset(tids, 0, sizeof(tids));
	for (i = 0; i < threads; i++) {
		err = pthread_create(&tids[i], NULL, locked_update_fn, &ctx);
		if (!ASSERT_OK(err, "pthread_create"))
			goto stop;
	}
	for (i = 0; i < threads; i++) {
		err = pthread_create(&tids[threads + i], NULL, delete_update_fn, &ctx);
		if (!ASSERT_OK(err, "pthread_create"))
			goto stop;
	}
	for (i = 0; i < threads; i++) {
		err = pthread_create(&tids[threads * 2 + i], NULL, locked_lookup_fn, &ctx);
		if (!ASSERT_OK(err, "pthread_create"))
			goto stop;
	}

	/* Release all threads simultaneously */
	close(pipefd[1]);
	pipefd[1] = -1;

stop:
	for (i = 0; i < threads_total; i++) {
		if (!tids[i])
			continue;
		pthread_join(tids[i], NULL);
	}

	ASSERT_FALSE(ctx.torn_write, "no torn writes detected");

close_pipe:
	if (pipefd[1] >= 0)
		close(pipefd[1]);
	close(pipefd[0]);
out:
	htab_reuse__destroy(skel);
}

void test_htab_reuse(void)
{
	if (test__start_subtest("basic"))
		test_htab_reuse_basic();
	if (test__start_subtest("consistency"))
		test_htab_reuse_consistency();
}
