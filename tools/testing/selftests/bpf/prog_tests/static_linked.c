// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <test_progs.h>
#include "test_static_linked.skel.h"

void test_static_linked(void)
{
	int err;
	struct test_static_linked* skel;

	skel = test_static_linked__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->rodata->rovar1 = 1;
	skel->rodata->rovar2 = 4;

	err = test_static_linked__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	err = test_static_linked__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* trigger */
	usleep(1);

	ASSERT_EQ(skel->data->var1, 1 * 2 + 2 + 3, "var1");
	ASSERT_EQ(skel->data->var2, 4 * 3 + 5 + 6, "var2");

cleanup:
	test_static_linked__destroy(skel);
}
