// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "get_func_ip_test.skel.h"

void test_get_func_ip_test(void)
{
	struct get_func_ip_test *skel = NULL;
	__u32 duration = 0, retval;
	int err, prog_fd;

	skel = get_func_ip_test__open();
	if (!ASSERT_OK_PTR(skel, "get_func_ip_test__open"))
		return;

	/* test6 is x86_64 specifc because of the instruction
	 * offset, disabling it for all other archs
	 */
#ifndef __x86_64__
	bpf_program__set_autoload(skel->progs.test6, false);
	bpf_program__set_autoload(skel->progs.test7, false);
#endif

	err = get_func_ip_test__load(skel);
	if (!ASSERT_OK(err, "get_func_ip_test__load"))
		goto cleanup;

	err = get_func_ip_test__attach(skel);
	if (!ASSERT_OK(err, "get_func_ip_test__attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.test1);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(retval, 0, "test_run");

	prog_fd = bpf_program__fd(skel->progs.test5);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);

	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test1_result, 1, "test1_result");
	ASSERT_EQ(skel->bss->test2_result, 1, "test2_result");
	ASSERT_EQ(skel->bss->test3_result, 1, "test3_result");
	ASSERT_EQ(skel->bss->test4_result, 1, "test4_result");
	ASSERT_EQ(skel->bss->test5_result, 1, "test5_result");
#ifdef __x86_64__
	ASSERT_EQ(skel->bss->test6_result, 1, "test6_result");
	ASSERT_EQ(skel->bss->test7_result, 1, "test7_result");
#endif

cleanup:
	get_func_ip_test__destroy(skel);
}
