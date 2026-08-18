// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>

#ifdef HAS_BPF_ARENA_ASAN
#include <unistd.h>

#include <libarena/common.h>
#include <libarena/asan.h>
#include <libarena/buddy.h>
#include <libarena/userspace.h>

#include "libarena/libarena_asan.skel.h"

static void run_libarena_asan_test(struct libarena_asan *skel,
		struct bpf_program *prog, const char *name)
{
	int ret;

	if (!strstr(name, "test_buddy")) {
		ret = libarena_run_prog(bpf_program__fd(skel->progs.arena_buddy_reset));
		if (!ASSERT_OK(ret, "arena_buddy_reset"))
			return;
	}

	ret = libarena_run_prog(bpf_program__fd(prog));
	ASSERT_OK(ret, name);

	verify_test_stderr(skel->obj, prog);
}

static void run_test(void)
{
	struct arena_alloc_reserve_args args;
	struct libarena_asan *skel;
	struct bpf_program *prog;
	int ret;

	skel = libarena_asan__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	ret = libarena_asan__attach(skel);
	if (!ASSERT_OK(ret, "attach"))
		goto out;

	args.nr_pages = ARENA_RESERVE_PAGES_DFL;

	ret = libarena_run_prog_args(bpf_program__fd(skel->progs.arena_alloc_reserve),
			&args, sizeof(args));
	if (!ASSERT_OK(ret, "arena_alloc_reserve"))
		goto out;

	ret = libarena_asan_init(
		bpf_program__fd(skel->progs.arena_get_info),
		bpf_program__fd(skel->progs.asan_init),
		(1ULL << 32) / sysconf(_SC_PAGESIZE));
	if (!ASSERT_OK(ret, "libarena_asan_init"))
		goto out;

	bpf_object__for_each_program(prog, skel->obj) {
		const char *name = bpf_program__name(prog);

		if (!libarena_is_asan_test_prog(name))
			continue;

		if (!test__start_subtest(name))
			continue;

		run_libarena_asan_test(skel, prog, name);
	}

out:
	libarena_asan__destroy(skel);
}

#endif /* HAS_BPF_ARENA_ASAN */

/*
 * Run the test depending on whether LLVM can compile arena ASAN
 * programs.
 */
void test_libarena_asan(void)
{
#ifdef HAS_BPF_ARENA_ASAN
	run_test();
#else
	test__skip();
#endif

	return;
}

