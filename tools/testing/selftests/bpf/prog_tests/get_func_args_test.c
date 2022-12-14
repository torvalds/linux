// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "get_func_args_test.skel.h"

void test_get_func_args_test(void)
{
	struct get_func_args_test *skel = NULL;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	skel = get_func_args_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "get_func_args_test__open_and_load"))
		return;

	err = get_func_args_test__attach(skel);
	if (!ASSERT_OK(err, "get_func_args_test__attach"))
		goto cleanup;

	/* This runs bpf_fentry_test* functions and triggers
	 * fentry/fexit programs.
	 */
	prog_fd = bpf_program__fd(skel->progs.test1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	/* This runs bpf_modify_return_test function and triggers
	 * fmod_ret_test and fexit_test programs.
	 */
	prog_fd = bpf_program__fd(skel->progs.fmod_ret_test);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 1234, "test_run");

	ASSERT_EQ(skel->bss->test1_result, 1, "test1_result");
	ASSERT_EQ(skel->bss->test2_result, 1, "test2_result");
	ASSERT_EQ(skel->bss->test3_result, 1, "test3_result");
	ASSERT_EQ(skel->bss->test4_result, 1, "test4_result");

cleanup:
	get_func_args_test__destroy(skel);
}
