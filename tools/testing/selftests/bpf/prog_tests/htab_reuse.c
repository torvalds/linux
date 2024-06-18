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

void test_htab_reuse(void)
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
