// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "test_progs.h"
#include "xdpwall.skel.h"

void test_xdpwall(void)
{
	struct xdpwall *skel;

	skel = xdpwall__open_and_load();
	ASSERT_OK_PTR(skel, "Does LLVM have https://github.com/llvm/llvm-project/commit/ea72b0319d7b0f0c2fcf41d121afa5d031b319d5?");

	xdpwall__destroy(skel);
}
