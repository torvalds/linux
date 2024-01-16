// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>

struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
};

/* ___shuffled flavor is just an illusion for BPF code, it doesn't really
 * exist and user-space needs to provide data in the memory layout that
 * matches callback_head. We just defined ___shuffled flavor to make it easier
 * to work with the skeleton
 */
struct callback_head___shuffled {
	struct callback_head___shuffled *next;
	void (*func)(struct callback_head *head);
};

#include "test_core_read_macros.skel.h"

void test_core_read_macros(void)
{
	int duration = 0, err;
	struct test_core_read_macros* skel;
	struct test_core_read_macros__bss *bss;
	struct callback_head u_probe_in;
	struct callback_head___shuffled u_core_in;

	skel = test_core_read_macros__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;
	bss = skel->bss;
	bss->my_pid = getpid();

	/* next pointers have to be set from the kernel side */
	bss->k_probe_in.func = (void *)(long)0x1234;
	bss->k_core_in.func = (void *)(long)0xabcd;

	u_probe_in.next = &u_probe_in;
	u_probe_in.func = (void *)(long)0x5678;
	bss->u_probe_in = &u_probe_in;

	u_core_in.next = &u_core_in;
	u_core_in.func = (void *)(long)0xdbca;
	bss->u_core_in = &u_core_in;

	err = test_core_read_macros__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);

	ASSERT_EQ(bss->k_probe_out, 0x1234, "k_probe_out");
	ASSERT_EQ(bss->k_core_out, 0xabcd, "k_core_out");

	ASSERT_EQ(bss->u_probe_out, 0x5678, "u_probe_out");
	ASSERT_EQ(bss->u_core_out, 0xdbca, "u_core_out");

cleanup:
	test_core_read_macros__destroy(skel);
}
