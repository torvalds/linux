// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "arena_strsearch.skel.h"

static void test_arena_str(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct arena_strsearch *skel;
	int ret;

	skel = arena_strsearch__open_and_load();
	if (!ASSERT_OK_PTR(skel, "arena_strsearch__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.arena_strsearch), &opts);
	ASSERT_OK(ret, "ret_add");
	ASSERT_OK(opts.retval, "retval");
	if (skel->bss->skip) {
		printf("%s:SKIP:compiler doesn't support arena_cast\n", __func__);
		test__skip();
	}
	arena_strsearch__destroy(skel);
}

void test_arena_strsearch(void)
{
	if (test__start_subtest("arena_strsearch"))
		test_arena_str();
}
