// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "test_progs.h"
#include "xdpwall.skel.h"

void test_xdpwall(void)
{
	struct xdpwall *skel;

	skel = xdpwall__open_and_load();
	ASSERT_OK_PTR(skel, "Does LLMV have https://reviews.llvm.org/D109073?");

	xdpwall__destroy(skel);
}
