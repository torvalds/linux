// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022. Huawei Technologies Co., Ltd */
#define _GNU_SOURCE
#include <sched.h>
#include <stdbool.h>
#include <test_progs.h>
#include "htab_update.skel.h"

struct htab_update_ctx {
	int fd;
	int loop;
	bool stop;
};

static void test_reenter_update(void)
{
	struct htab_update *skel;
	unsigned int key, value;
	int err;

	skel = htab_update__open();
	if (!ASSERT_OK_PTR(skel, "htab_update__open"))
		return;

	/* lookup_elem_raw() may be inlined and find_kernel_btf_id() will return -ESRCH */
	bpf_program__set_autoload(skel->progs.lookup_elem_raw, true);
	err = htab_update__load(skel);
	if (!ASSERT_TRUE(!err || err == -ESRCH, "htab_update__load") || err)
		goto out;

	skel->bss->pid = getpid();
	err = htab_update__attach(skel);
	if (!ASSERT_OK(err, "htab_update__attach"))
		goto out;

	/* Will trigger the reentrancy of bpf_map_update_elem() */
	key = 0;
	value = 0;
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.htab), &key, &value, 0);
	if (!ASSERT_OK(err, "add element"))
		goto out;

	ASSERT_EQ(skel->bss->update_err, -EBUSY, "no reentrancy");
out:
	htab_update__destroy(skel);
}

static void *htab_update_thread(void *arg)
{
	struct htab_update_ctx *ctx = arg;
	cpu_set_t cpus;
	int i;

	/* Pinned on CPU 0 */
	CPU_ZERO(&cpus);
	CPU_SET(0, &cpus);
	pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus);

	i = 0;
	while (i++ < ctx->loop && !ctx->stop) {
		unsigned int key = 0, value = 0;
		int err;

		err = bpf_map_update_elem(ctx->fd, &key, &value, 0);
		if (err) {
			ctx->stop = true;
			return (void *)(long)err;
		}
	}

	return NULL;
}

static void test_concurrent_update(void)
{
	struct htab_update_ctx ctx;
	struct htab_update *skel;
	unsigned int i, nr;
	pthread_t *tids;
	int err;

	skel = htab_update__open_and_load();
	if (!ASSERT_OK_PTR(skel, "htab_update__open_and_load"))
		return;

	ctx.fd = bpf_map__fd(skel->maps.htab);
	ctx.loop = 1000;
	ctx.stop = false;

	nr = 4;
	tids = calloc(nr, sizeof(*tids));
	if (!ASSERT_NEQ(tids, NULL, "no mem"))
		goto out;

	for (i = 0; i < nr; i++) {
		err = pthread_create(&tids[i], NULL, htab_update_thread, &ctx);
		if (!ASSERT_OK(err, "pthread_create")) {
			unsigned int j;

			ctx.stop = true;
			for (j = 0; j < i; j++)
				pthread_join(tids[j], NULL);
			goto out;
		}
	}

	for (i = 0; i < nr; i++) {
		void *thread_err = NULL;

		pthread_join(tids[i], &thread_err);
		ASSERT_EQ(thread_err, NULL, "update error");
	}

out:
	if (tids)
		free(tids);
	htab_update__destroy(skel);
}

void test_htab_update(void)
{
	if (test__start_subtest("reenter_update"))
		test_reenter_update();
	if (test__start_subtest("concurrent_update"))
		test_concurrent_update();
}
