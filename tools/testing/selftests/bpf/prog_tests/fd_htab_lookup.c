// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025. Huawei Technologies Co., Ltd */
#define _GNU_SOURCE
#include <stdbool.h>
#include <test_progs.h>
#include "fd_htab_lookup.skel.h"

struct htab_op_ctx {
	int fd;
	int loop;
	unsigned int entries;
	bool stop;
};

#define ERR_TO_RETVAL(where, err) ((void *)(long)(((where) << 12) | (-err)))

static void *htab_lookup_fn(void *arg)
{
	struct htab_op_ctx *ctx = arg;
	int i = 0;

	while (i++ < ctx->loop && !ctx->stop) {
		unsigned int j;

		for (j = 0; j < ctx->entries; j++) {
			unsigned int key = j, zero = 0, value;
			int inner_fd, err;

			err = bpf_map_lookup_elem(ctx->fd, &key, &value);
			if (err) {
				ctx->stop = true;
				return ERR_TO_RETVAL(1, err);
			}

			inner_fd = bpf_map_get_fd_by_id(value);
			if (inner_fd < 0) {
				/* The old map has been freed */
				if (inner_fd == -ENOENT)
					continue;
				ctx->stop = true;
				return ERR_TO_RETVAL(2, inner_fd);
			}

			err = bpf_map_lookup_elem(inner_fd, &zero, &value);
			if (err) {
				close(inner_fd);
				ctx->stop = true;
				return ERR_TO_RETVAL(3, err);
			}
			close(inner_fd);

			if (value != key) {
				ctx->stop = true;
				return ERR_TO_RETVAL(4, -EINVAL);
			}
		}
	}

	return NULL;
}

static void *htab_update_fn(void *arg)
{
	struct htab_op_ctx *ctx = arg;
	int i = 0;

	while (i++ < ctx->loop && !ctx->stop) {
		unsigned int j;

		for (j = 0; j < ctx->entries; j++) {
			unsigned int key = j, zero = 0;
			int inner_fd, err;

			inner_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, NULL, 4, 4, 1, NULL);
			if (inner_fd < 0) {
				ctx->stop = true;
				return ERR_TO_RETVAL(1, inner_fd);
			}

			err = bpf_map_update_elem(inner_fd, &zero, &key, 0);
			if (err) {
				close(inner_fd);
				ctx->stop = true;
				return ERR_TO_RETVAL(2, err);
			}

			err = bpf_map_update_elem(ctx->fd, &key, &inner_fd, BPF_EXIST);
			if (err) {
				close(inner_fd);
				ctx->stop = true;
				return ERR_TO_RETVAL(3, err);
			}
			close(inner_fd);
		}
	}

	return NULL;
}

static int setup_htab(int fd, unsigned int entries)
{
	unsigned int i;

	for (i = 0; i < entries; i++) {
		unsigned int key = i, zero = 0;
		int inner_fd, err;

		inner_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, NULL, 4, 4, 1, NULL);
		if (!ASSERT_OK_FD(inner_fd, "new array"))
			return -1;

		err = bpf_map_update_elem(inner_fd, &zero, &key, 0);
		if (!ASSERT_OK(err, "init array")) {
			close(inner_fd);
			return -1;
		}

		err = bpf_map_update_elem(fd, &key, &inner_fd, 0);
		if (!ASSERT_OK(err, "init outer")) {
			close(inner_fd);
			return -1;
		}
		close(inner_fd);
	}

	return 0;
}

static int get_int_from_env(const char *name, int dft)
{
	const char *value;

	value = getenv(name);
	if (!value)
		return dft;

	return atoi(value);
}

void test_fd_htab_lookup(void)
{
	unsigned int i, wr_nr = 8, rd_nr = 16;
	pthread_t tids[wr_nr + rd_nr];
	struct fd_htab_lookup *skel;
	struct htab_op_ctx ctx;
	int err;

	skel = fd_htab_lookup__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fd_htab_lookup__open_and_load"))
		return;

	ctx.fd = bpf_map__fd(skel->maps.outer_map);
	ctx.loop = get_int_from_env("FD_HTAB_LOOP_NR", 5);
	ctx.stop = false;
	ctx.entries = 8;

	err = setup_htab(ctx.fd, ctx.entries);
	if (err)
		goto destroy;

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
		void *ret = NULL;
		char desc[32];

		if (!tids[i])
			continue;

		snprintf(desc, sizeof(desc), "thread %u", i + 1);
		err = pthread_join(tids[i], &ret);
		ASSERT_OK(err, desc);
		ASSERT_EQ(ret, NULL, desc);
	}
destroy:
	fd_htab_lookup__destroy(skel);
}
