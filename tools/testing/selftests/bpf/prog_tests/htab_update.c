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
	void *value = NULL;
	unsigned int key, value_size;
	int err;

	skel = htab_update__open();
	if (!ASSERT_OK_PTR(skel, "htab_update__open"))
		return;

	bpf_program__set_autoload(skel->progs.bpf_obj_free_fields, true);
	err = htab_update__load(skel);
	if (!ASSERT_TRUE(!err, "htab_update__load") || err)
		goto out;

	skel->bss->pid = getpid();
	err = htab_update__attach(skel);
	if (!ASSERT_OK(err, "htab_update__attach"))
		goto out;

	value_size = bpf_map__value_size(skel->maps.htab);

	value = calloc(1, value_size);
	if (!ASSERT_OK_PTR(value, "calloc value"))
		goto out;
	/*
	 * First update: plain insert. This should NOT trigger the re-entrancy
	 * path, because there is no old element to free yet.
	 */
	key = 0;
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.htab), &key, value, BPF_ANY);
	if (!ASSERT_OK(err, "first update (insert)"))
		goto out;

	/*
	 * Second update: replace existing element with same key and trigger
	 * the reentrancy of bpf_map_update_elem().
	 * check_and_free_fields() calls bpf_obj_free_fields() on the old
	 * value, which is where fentry program runs and performs a nested
	 * bpf_map_update_elem(), triggering -EDEADLK.
	 */
	memset(value, 0, value_size);
	err = bpf_map_update_elem(bpf_map__fd(skel->maps.htab), &key, value, BPF_ANY);
	if (!ASSERT_OK(err, "second update (replace)"))
		goto out;

	ASSERT_EQ(skel->bss->update_err, -EDEADLK, "no reentrancy");
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
