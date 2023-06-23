// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Google */

#include <test_progs.h>
#include "test_autoattach.skel.h"

void test_autoattach(void)
{
	struct test_autoattach *skel;

	skel = test_autoattach__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		goto cleanup;

	/* disable auto-attach for prog2 */
	bpf_program__set_autoattach(skel->progs.prog2, false);
	ASSERT_TRUE(bpf_program__autoattach(skel->progs.prog1), "autoattach_prog1");
	ASSERT_FALSE(bpf_program__autoattach(skel->progs.prog2), "autoattach_prog2");
	if (!ASSERT_OK(test_autoattach__attach(skel), "skel_attach"))
		goto cleanup;

	usleep(1);

	ASSERT_TRUE(skel->bss->prog1_called, "attached_prog1");
	ASSERT_FALSE(skel->bss->prog2_called, "attached_prog2");

cleanup:
	test_autoattach__destroy(skel);
}

