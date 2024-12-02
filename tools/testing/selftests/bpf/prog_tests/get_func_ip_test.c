// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "get_func_ip_test.skel.h"

static void test_function_entry(void)
{
	struct get_func_ip_test *skel = NULL;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	skel = get_func_ip_test__open();
	if (!ASSERT_OK_PTR(skel, "get_func_ip_test__open"))
		return;

	err = get_func_ip_test__load(skel);
	if (!ASSERT_OK(err, "get_func_ip_test__load"))
		goto cleanup;

	err = get_func_ip_test__attach(skel);
	if (!ASSERT_OK(err, "get_func_ip_test__attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.test1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	prog_fd = bpf_program__fd(skel->progs.test5);
	err = bpf_prog_test_run_opts(prog_fd, &topts);

	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test1_result, 1, "test1_result");
	ASSERT_EQ(skel->bss->test2_result, 1, "test2_result");
	ASSERT_EQ(skel->bss->test3_result, 1, "test3_result");
	ASSERT_EQ(skel->bss->test4_result, 1, "test4_result");
	ASSERT_EQ(skel->bss->test5_result, 1, "test5_result");

cleanup:
	get_func_ip_test__destroy(skel);
}

/* test6 is x86_64 specific because of the instruction
 * offset, disabling it for all other archs
 */
#ifdef __x86_64__
static void test_function_body(void)
{
	struct get_func_ip_test *skel = NULL;
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	LIBBPF_OPTS(bpf_kprobe_opts, kopts);
	struct bpf_link *link6 = NULL;
	int err, prog_fd;

	skel = get_func_ip_test__open();
	if (!ASSERT_OK_PTR(skel, "get_func_ip_test__open"))
		return;

	bpf_program__set_autoload(skel->progs.test6, true);

	err = get_func_ip_test__load(skel);
	if (!ASSERT_OK(err, "get_func_ip_test__load"))
		goto cleanup;

	kopts.offset = skel->kconfig->CONFIG_X86_KERNEL_IBT ? 9 : 5;

	link6 = bpf_program__attach_kprobe_opts(skel->progs.test6, "bpf_fentry_test6", &kopts);
	if (!ASSERT_OK_PTR(link6, "link6"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.test1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	ASSERT_EQ(skel->bss->test6_result, 1, "test6_result");

cleanup:
	bpf_link__destroy(link6);
	get_func_ip_test__destroy(skel);
}
#else
#define test_function_body()
#endif

void test_get_func_ip_test(void)
{
	test_function_entry();
	test_function_body();
}
