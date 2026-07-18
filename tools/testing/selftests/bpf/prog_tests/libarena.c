// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <unistd.h>

#include <libarena/common.h>
#include <libarena/asan.h>
#include <libarena/buddy.h>
#include <libarena/userspace.h>

#include "libarena/libarena.skel.h"

static void run_libarena_test(struct libarena *skel, struct bpf_program *prog,
		const char *name)
{
	int ret;

	if (!strstr(name, "test_buddy")) {
		ret = libarena_run_prog(bpf_program__fd(skel->progs.arena_buddy_reset));
		if (!ASSERT_OK(ret, "arena_buddy_reset"))
			return;
	}

	ret = libarena_run_prog(bpf_program__fd(prog));

	ASSERT_OK(ret, name);

}

static void *run_libarena_parallel_prog(void *arg)
{
	struct bpf_program *prog = arg;

	return (void *)(long)libarena_run_prog(bpf_program__fd(prog));
}

/* Max suffix is ceil((lg 2^32) / (lg 10)) + sizeof("__") = 10 + 2 = 12. */
#define MAX_PARTEST_SUFFIX (12)
#define MAX_PARTEST_NAME (1024)
#define MAX_PARTEST_PREFIX (MAX_PARTEST_NAME - MAX_PARTEST_SUFFIX)

static int run_libarena_parallel_fini(struct libarena *skel, const char *name,
				      size_t prefixlen)
{
	char tdname[MAX_PARTEST_NAME];
	struct bpf_program *fini_prog;
	int ret;

	ret = snprintf(tdname, sizeof(tdname), "%.*s__fini", (int)prefixlen, name);
	if (!ASSERT_LT(ret, sizeof(tdname), "partest fini name"))
		return -ENAMETOOLONG;

	fini_prog = bpf_object__find_program_by_name(skel->obj, tdname);
	if (!ASSERT_TRUE(fini_prog, "partest fini prog"))
		return -ENOENT;

	ret = libarena_run_prog(bpf_program__fd(fini_prog));
	ASSERT_OK(ret, tdname);

	return ret;
}

static int run_libarena_parallel_test_workers(struct libarena *skel,
		const char *name, size_t prefixlen)
{
	pthread_t *threads = NULL, *tmp_threads;
	char tdname[MAX_PARTEST_NAME];
	struct bpf_program *tdprog;
	uint32_t nthreads;
	void *thread_ret;
	int ret, err = 0;
	int i;

	for (nthreads = 0; nthreads < UINT_MAX; nthreads++) {
		ret = snprintf(tdname, sizeof(tdname), "%.*s__%u", (int)prefixlen,
			       name, nthreads);
		if (!ASSERT_LT(ret, sizeof(tdname), "test worker name")) {
			err = -ENAMETOOLONG;
			break;
		}

		/* 
		 * We enumerate the worker threads for a given test with __0, __1,
		 * and so on. The suffixes always start from 0 and are contiguous,
		 * so if we don't find a program with the requested name we have
		 * discovered all available worker programs.
		 */
		tdprog = bpf_object__find_program_by_name(skel->obj, tdname);
		if (!tdprog)
			break;

		/* Bump the alloc array to accommodate the new thread. */
		tmp_threads = realloc(threads, (nthreads + 1) * sizeof(*threads));
		if (!ASSERT_TRUE(tmp_threads, "realloc")) {
			err = -ENOMEM;
			break;
		}
		threads = tmp_threads;

		ret = pthread_create(&threads[nthreads], NULL,
				     run_libarena_parallel_prog,
				     tdprog);
		if (!ASSERT_OK(ret, "pthread_create")) {
			err = ret;
			break;
		}
	}


	for (i = 0; i < nthreads; i++) {
		ret = pthread_join(threads[i], &thread_ret);
		if (!ASSERT_OK(ret, "pthread_join")) {
			err = err ?: ret;
			continue;
		}

		err = err ?: (long)thread_ret;
	}

	free(threads);

	return err;
}

static bool libarena_parallel_test_enabled(struct libarena *skel,
					   const char *prefix,
					   size_t prefixlen)
{
	struct bpf_program *prog;
	char progname[MAX_PARTEST_NAME];
	int ret;

	ret = snprintf(progname, sizeof(progname), "%.*s__enabled", (int)prefixlen,
		       prefix);
	if (!ASSERT_LT(ret, sizeof(progname), "partest enabled name"))
		return false;

	prog = bpf_object__find_program_by_name(skel->obj, progname);
	if (!prog)
		return true;

	ret = libarena_run_prog(bpf_program__fd(prog));
	if (ret == -EOPNOTSUPP)
		return false;
	if (!ASSERT_OK(ret, progname))
		return false;
	return true;
}

static void run_libarena_parallel_test(struct libarena *skel, struct bpf_program *prog,
		const char *name)
{
	char testname[MAX_PARTEST_NAME];
	size_t prefixlen;
	const char *pos;
	int ret;

	/*
	 * We annotate the initialization prog with __init. If the current prog does
	 * not match, it is one of the parallel threads instead and is ignored.
	 *
	 * We assume the test writer knows what they are doing and do not add __init
	 * randomly in the middle of a test name.
	 */
	pos = strstr(name, "__init");
	if (!pos)
		return;

	prefixlen = pos - name;
	if (!ASSERT_LT(prefixlen, MAX_PARTEST_PREFIX, "partest prefix too long"))
		return;

	/* The name of the test without the __init suffix. Looks nicer in the test log. */
	ret = snprintf(testname, sizeof(testname), "%.*s", (int)prefixlen, name);
	if (!ASSERT_LT(ret, sizeof(testname), "partest test name"))
		return;

	if (!test__start_subtest(testname))
		return;

	if (!libarena_parallel_test_enabled(skel, testname, prefixlen)) {
		test__skip();
		return;
	}

	ret = libarena_run_prog(bpf_program__fd(skel->progs.arena_buddy_reset));
	if (!ASSERT_OK(ret, "arena_buddy_reset"))
		return;

	ret = libarena_run_prog(bpf_program__fd(prog));
	if (!ASSERT_OK(ret, testname))
		return;

	ret = run_libarena_parallel_test_workers(skel, name, prefixlen);

	ASSERT_OK(ret, testname);

	run_libarena_parallel_fini(skel, name, prefixlen);
}

void test_libarena(void)
{
	struct arena_alloc_reserve_args args;
	struct libarena *skel;
	struct bpf_program *prog;
	int ret;

	skel = libarena__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	ret = libarena__attach(skel);
	if (!ASSERT_OK(ret, "attach"))
		goto out;

	args.nr_pages = ARENA_RESERVE_PAGES_DFL;

	ret = libarena_run_prog_args(bpf_program__fd(skel->progs.arena_alloc_reserve),
			&args, sizeof(args));
	if (!ASSERT_OK(ret, "arena_alloc_reserve"))
		goto out;

	bpf_object__for_each_program(prog, skel->obj) {
		const char *name = bpf_program__name(prog);

		/*
		 * Handle parallel test progs separately. For those
		 * progs it's not a matter of test/skip, because each
		 * parallel test prog includes an initialization prog
		 * and a set of progs to be run in parallel. For the
		 * latter we do not record them as skipped or run,
		 * because we run them all at once when we come across
		 * the initialization prog. For more details on how we
		 * discover the progs see the comment on
		 * run_libarena_parallel_test.
		 */
		if (libarena_is_parallel_test_prog(name)) {
			run_libarena_parallel_test(skel, prog, name);
			continue;
		}

		if (!libarena_is_test_prog(name))
			continue;

		if (!test__start_subtest(name))
			continue;

		run_libarena_test(skel, prog, name);
	}

out:
	libarena__destroy(skel);
}
