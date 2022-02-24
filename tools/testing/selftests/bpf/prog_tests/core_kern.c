// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "test_progs.h"
#include "core_kern.lskel.h"

void test_core_kern_lskel(void)
{
	struct core_kern_lskel *skel;

	skel = core_kern_lskel__open_and_load();
	ASSERT_OK_PTR(skel, "open_and_load");
	core_kern_lskel__destroy(skel);
}
