// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Hengqi Chen */

#include <test_progs.h>
#include "test_uprobe.skel.h"

static FILE *urand_spawn(int *pid)
{
	FILE *f;

	/* urandom_read's stdout is wired into f */
	f = popen("./urandom_read 1 report-pid", "r");
	if (!f)
		return NULL;

	if (fscanf(f, "%d", pid) != 1) {
		pclose(f);
		errno = EINVAL;
		return NULL;
	}

	return f;
}

static int urand_trigger(FILE **urand_pipe)
{
	int exit_code;

	/* pclose() waits for child process to exit and returns their exit code */
	exit_code = pclose(*urand_pipe);
	*urand_pipe = NULL;

	return exit_code;
}

void test_uprobe(void)
{
	LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts);
	struct test_uprobe *skel;
	FILE *urand_pipe = NULL;
	int urand_pid = 0, err;

	skel = test_uprobe__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	urand_pipe = urand_spawn(&urand_pid);
	if (!ASSERT_OK_PTR(urand_pipe, "urand_spawn"))
		goto cleanup;

	skel->bss->my_pid = urand_pid;

	/* Manual attach uprobe to urandlib_api
	 * There are two `urandlib_api` symbols in .dynsym section:
	 *   - urandlib_api@LIBURANDOM_READ_1.0.0
	 *   - urandlib_api@@LIBURANDOM_READ_2.0.0
	 * Both are global bind and would cause a conflict if user
	 * specify the symbol name without a version suffix
	 */
	uprobe_opts.func_name = "urandlib_api";
	skel->links.test4 = bpf_program__attach_uprobe_opts(skel->progs.test4,
							    urand_pid,
							    "./liburandom_read.so",
							    0 /* offset */,
							    &uprobe_opts);
	if (!ASSERT_ERR_PTR(skel->links.test4, "urandlib_api_attach_conflict"))
		goto cleanup;

	uprobe_opts.func_name = "urandlib_api@LIBURANDOM_READ_1.0.0";
	skel->links.test4 = bpf_program__attach_uprobe_opts(skel->progs.test4,
							    urand_pid,
							    "./liburandom_read.so",
							    0 /* offset */,
							    &uprobe_opts);
	if (!ASSERT_OK_PTR(skel->links.test4, "urandlib_api_attach_ok"))
		goto cleanup;

	/* Auto attach 3 u[ret]probes to urandlib_api_sameoffset */
	err = test_uprobe__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* trigger urandom_read */
	ASSERT_OK(urand_trigger(&urand_pipe), "urand_exit_code");

	ASSERT_EQ(skel->bss->test1_result, 1, "urandlib_api_sameoffset");
	ASSERT_EQ(skel->bss->test2_result, 1, "urandlib_api_sameoffset@v1");
	ASSERT_EQ(skel->bss->test3_result, 3, "urandlib_api_sameoffset@@v2");
	ASSERT_EQ(skel->bss->test4_result, 1, "urandlib_api");

cleanup:
	if (urand_pipe)
		pclose(urand_pipe);
	test_uprobe__destroy(skel);
}
