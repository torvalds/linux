// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include "tag.skel.h"

void test_btf_tag(void)
{
	struct tag *skel;

	skel = tag__open_and_load();
	if (!ASSERT_OK_PTR(skel, "btf_tag"))
		return;
	tag__destroy(skel);
}
