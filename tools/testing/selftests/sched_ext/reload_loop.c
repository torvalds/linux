/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#include <bpf/bpf.h>
#include <errno.h>
#include <pthread.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include "maximal.bpf.skel.h"
#include "scx_test.h"

enum {
	RELOAD_ITERS	= 1024,
	RELOAD_THREADS	= 2,
};

static struct maximal *skel;
static pthread_t threads[RELOAD_THREADS];
static int unexpected_errno;

bool force_exit = false;

static enum scx_test_status setup(void **ctx)
{
	skel = maximal__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(maximal__load(skel), "Failed to load skel");

	bpf_map__set_autoattach(skel->maps.maximal_ops, false);
	SCX_FAIL_IF(maximal__attach(skel), "Failed to attach skel");

	return SCX_TEST_PASS;
}

static void *do_reload_loop(void *arg)
{
	u32 i, failures = 0;

	for (i = 0; i < RELOAD_ITERS && !force_exit; i++) {
		struct bpf_link *link;

		errno = 0;
		link = bpf_map__attach_struct_ops(skel->maps.maximal_ops);
		if (!link) {
			int e = errno;

			if (e != EBUSY)
				__atomic_store_n(&unexpected_errno, e,
						 __ATOMIC_RELAXED);
			failures++;
			continue;
		}

		bpf_link__destroy(link);
	}

	return (void *)(unsigned long)failures;
}

static enum scx_test_status run(void *ctx)
{
	unsigned long failures[RELOAD_THREADS] = {};
	int err, i;
	void *ret;

	err = pthread_create(&threads[0], NULL, do_reload_loop, NULL);
	SCX_FAIL_IF(err, "Failed to create thread 0");

	err = pthread_create(&threads[1], NULL, do_reload_loop, NULL);
	if (err) {
		force_exit = true;
		pthread_join(threads[0], NULL);
		SCX_FAIL("Failed to create thread 1");
	}

	SCX_FAIL_IF(pthread_join(threads[0], &ret), "thread 0 failed");
	failures[0] = (unsigned long)ret;

	SCX_FAIL_IF(pthread_join(threads[1], &ret), "thread 1 failed");
	failures[1] = (unsigned long)ret;

	SCX_FAIL_IF(unexpected_errno, "unexpected attach errno %d",
		    unexpected_errno);

	for (i = 0; i < RELOAD_THREADS; i++)
		SCX_FAIL_IF(failures[i] == RELOAD_ITERS,
			    "thread %d: every attach failed (%lu EBUSY)",
			    i, failures[i]);

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
