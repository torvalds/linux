// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include "test_subprogs.skel.h"
#include "test_subprogs_unused.skel.h"

struct toggler_ctx {
	int fd;
	bool stop;
};

static void *toggle_jit_harden(void *arg)
{
	struct toggler_ctx *ctx = arg;
	char two = '2';
	char zero = '0';

	while (!ctx->stop) {
		lseek(ctx->fd, SEEK_SET, 0);
		write(ctx->fd, &two, sizeof(two));
		lseek(ctx->fd, SEEK_SET, 0);
		write(ctx->fd, &zero, sizeof(zero));
	}

	return NULL;
}

static void test_subprogs_with_jit_harden_toggling(void)
{
	struct toggler_ctx ctx;
	pthread_t toggler;
	int err;
	unsigned int i, loop = 10;

	ctx.fd = open("/proc/sys/net/core/bpf_jit_harden", O_RDWR);
	if (!ASSERT_GE(ctx.fd, 0, "open bpf_jit_harden"))
		return;

	ctx.stop = false;
	err = pthread_create(&toggler, NULL, toggle_jit_harden, &ctx);
	if (!ASSERT_OK(err, "new toggler"))
		goto out;

	/* Make toggler thread to run */
	usleep(1);

	for (i = 0; i < loop; i++) {
		struct test_subprogs *skel = test_subprogs__open_and_load();

		if (!ASSERT_OK_PTR(skel, "skel open"))
			break;
		test_subprogs__destroy(skel);
	}

	ctx.stop = true;
	pthread_join(toggler, NULL);
out:
	close(ctx.fd);
}

static void test_subprogs_alone(void)
{
	struct test_subprogs *skel;
	struct test_subprogs_unused *skel2;
	int err;

	skel = test_subprogs__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	err = test_subprogs__attach(skel);
	if (!ASSERT_OK(err, "skel attach"))
		goto cleanup;

	usleep(1);

	ASSERT_EQ(skel->bss->res1, 12, "res1");
	ASSERT_EQ(skel->bss->res2, 17, "res2");
	ASSERT_EQ(skel->bss->res3, 19, "res3");
	ASSERT_EQ(skel->bss->res4, 36, "res4");

	skel2 = test_subprogs_unused__open_and_load();
	ASSERT_OK_PTR(skel2, "unused_progs_skel");
	test_subprogs_unused__destroy(skel2);

cleanup:
	test_subprogs__destroy(skel);
}

void test_subprogs(void)
{
	if (test__start_subtest("subprogs_alone"))
		test_subprogs_alone();
	if (test__start_subtest("subprogs_and_jit_harden"))
		test_subprogs_with_jit_harden_toggling();
}
