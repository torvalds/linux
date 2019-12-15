// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <test_progs.h>

struct s {
	int a;
	long long b;
} __attribute__((packed));

#include "test_skeleton.skel.h"

BPF_EMBED_OBJ(skeleton, "test_skeleton.o");

void test_skeleton(void)
{
	int duration = 0, err;
	struct test_skeleton* skel;
	struct test_skeleton__bss *bss;

	skel = test_skeleton__open_and_load(&skeleton_embed);
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;

	bss = skel->bss;
	bss->in1 = 1;
	bss->in2 = 2;
	bss->in3 = 3;
	bss->in4 = 4;
	bss->in5.a = 5;
	bss->in5.b = 6;

	err = test_skeleton__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);

	CHECK(bss->out1 != 1, "res1", "got %d != exp %d\n", bss->out1, 1);
	CHECK(bss->out2 != 2, "res2", "got %lld != exp %d\n", bss->out2, 2);
	CHECK(bss->out3 != 3, "res3", "got %d != exp %d\n", (int)bss->out3, 3);
	CHECK(bss->out4 != 4, "res4", "got %lld != exp %d\n", bss->out4, 4);
	CHECK(bss->handler_out5.a != 5, "res5", "got %d != exp %d\n",
	      bss->handler_out5.a, 5);
	CHECK(bss->handler_out5.b != 6, "res6", "got %lld != exp %d\n",
	      bss->handler_out5.b, 6);

cleanup:
	test_skeleton__destroy(skel);
}
