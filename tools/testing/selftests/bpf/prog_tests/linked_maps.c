// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <test_progs.h>
#include <sys/syscall.h>
#include "linked_maps.skel.h"

void test_linked_maps(void)
{
	int err;
	struct linked_maps *skel;

	skel = linked_maps__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	err = linked_maps__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* trigger */
	syscall(SYS_getpgid);

	ASSERT_EQ(skel->bss->output_first1, 2000, "output_first1");
	ASSERT_EQ(skel->bss->output_second1, 2, "output_second1");
	ASSERT_EQ(skel->bss->output_weak1, 2, "output_weak1");

cleanup:
	linked_maps__destroy(skel);
}
