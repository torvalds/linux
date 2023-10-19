// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <test_progs.h>
#include <sys/syscall.h>
#include "linked_vars.skel.h"

void test_linked_vars(void)
{
	int err;
	struct linked_vars *skel;

	skel = linked_vars__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->bss->input_bss1 = 1000;
	skel->bss->input_bss2 = 2000;
	skel->bss->input_bss_weak = 3000;

	err = linked_vars__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	err = linked_vars__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* trigger */
	syscall(SYS_getpgid);

	ASSERT_EQ(skel->bss->output_bss1, 1000 + 2000 + 3000, "output_bss1");
	ASSERT_EQ(skel->bss->output_bss2, 1000 + 2000 + 3000, "output_bss2");
	/* 10 comes from "winner" input_data_weak in first obj file */
	ASSERT_EQ(skel->bss->output_data1, 1 + 2 + 10, "output_bss1");
	ASSERT_EQ(skel->bss->output_data2, 1 + 2 + 10, "output_bss2");
	/* 100 comes from "winner" input_rodata_weak in first obj file */
	ASSERT_EQ(skel->bss->output_rodata1, 11 + 22 + 100, "output_weak1");
	ASSERT_EQ(skel->bss->output_rodata2, 11 + 22 + 100, "output_weak2");

cleanup:
	linked_vars__destroy(skel);
}
