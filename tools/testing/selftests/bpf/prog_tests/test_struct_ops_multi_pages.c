// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>

#include "struct_ops_multi_pages.skel.h"

static void do_struct_ops_multi_pages(void)
{
	struct struct_ops_multi_pages *skel;
	struct bpf_link *link;

	/* The size of all trampolines of skel->maps.multi_pages should be
	 * over 1 page (at least for x86).
	 */
	skel = struct_ops_multi_pages__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_multi_pages_open_and_load"))
		return;

	link = bpf_map__attach_struct_ops(skel->maps.multi_pages);
	ASSERT_OK_PTR(link, "attach_multi_pages");

	bpf_link__destroy(link);
	struct_ops_multi_pages__destroy(skel);
}

void test_struct_ops_multi_pages(void)
{
	if (test__start_subtest("multi_pages"))
		do_struct_ops_multi_pages();
}
