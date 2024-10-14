/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#include <bpf/bpf.h>
#include <pthread.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include "maximal.bpf.skel.h"
#include "scx_test.h"

static struct maximal *skel;
static pthread_t threads[2];

bool force_exit = false;

static enum scx_test_status setup(void **ctx)
{
	skel = maximal__open_and_load();
	if (!skel) {
		SCX_ERR("Failed to open and load skel");
		return SCX_TEST_FAIL;
	}

	return SCX_TEST_PASS;
}

static void *do_reload_loop(void *arg)
{
	u32 i;

	for (i = 0; i < 1024 && !force_exit; i++) {
		struct bpf_link *link;

		link = bpf_map__attach_struct_ops(skel->maps.maximal_ops);
		if (link)
			bpf_link__destroy(link);
	}

	return NULL;
}

static enum scx_test_status run(void *ctx)
{
	int err;
	void *ret;

	err = pthread_create(&threads[0], NULL, do_reload_loop, NULL);
	SCX_FAIL_IF(err, "Failed to create thread 0");

	err = pthread_create(&threads[1], NULL, do_reload_loop, NULL);
	SCX_FAIL_IF(err, "Failed to create thread 1");

	SCX_FAIL_IF(pthread_join(threads[0], &ret), "thread 0 failed");
	SCX_FAIL_IF(pthread_join(threads[1], &ret), "thread 1 failed");

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	force_exit = true;
	maximal__destroy(skel);
}

struct scx_test reload_loop = {
	.name = "reload_loop",
	.description = "Stress test loading and unloading schedulers repeatedly in a tight loop",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&reload_loop)
