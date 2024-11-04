// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "raw_tp_null.skel.h"

void test_raw_tp_null(void)
{
	struct raw_tp_null *skel;

	skel = raw_tp_null__open_and_load();
	if (!ASSERT_OK_PTR(skel, "raw_tp_null__open_and_load"))
		return;

	skel->bss->tid = sys_gettid();

	if (!ASSERT_OK(raw_tp_null__attach(skel), "raw_tp_null__attach"))
		goto end;

	ASSERT_OK(trigger_module_test_read(2), "trigger testmod read");
	ASSERT_EQ(skel->bss->i, 3, "invocations");

end:
	raw_tp_null__destroy(skel);
}
