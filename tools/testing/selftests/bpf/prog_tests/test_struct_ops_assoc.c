// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "struct_ops_assoc.skel.h"
#include "struct_ops_assoc_reuse.skel.h"

static void test_st_ops_assoc(void)
{
	struct struct_ops_assoc *skel = NULL;
	int err, pid;

	skel = struct_ops_assoc__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_assoc__open"))
		goto out;

	/* cannot explicitly associate struct_ops program */
	err = bpf_program__assoc_struct_ops(skel->progs.test_1_a,
					    skel->maps.st_ops_map_a, NULL);
	ASSERT_ERR(err, "bpf_program__assoc_struct_ops(test_1_a, st_ops_map_a)");

	err = bpf_program__assoc_struct_ops(skel->progs.syscall_prog_a,
					    skel->maps.st_ops_map_a, NULL);
	ASSERT_OK(err, "bpf_program__assoc_struct_ops(syscall_prog_a, st_ops_map_a)");

	err = bpf_program__assoc_struct_ops(skel->progs.sys_enter_prog_a,
					    skel->maps.st_ops_map_a, NULL);
	ASSERT_OK(err, "bpf_program__assoc_struct_ops(sys_enter_prog_a, st_ops_map_a)");

	err = bpf_program__assoc_struct_ops(skel->progs.syscall_prog_b,
					    skel->maps.st_ops_map_b, NULL);
	ASSERT_OK(err, "bpf_program__assoc_struct_ops(syscall_prog_b, st_ops_map_b)");

	err = bpf_program__assoc_struct_ops(skel->progs.sys_enter_prog_b,
					    skel->maps.st_ops_map_b, NULL);
	ASSERT_OK(err, "bpf_program__assoc_struct_ops(sys_enter_prog_b, st_ops_map_b)");

	/* sys_enter_prog_a already associated with map_a */
	err = bpf_program__assoc_struct_ops(skel->progs.sys_enter_prog_a,
					    skel->maps.st_ops_map_b, NULL);
	ASSERT_ERR(err, "bpf_program__assoc_struct_ops(sys_enter_prog_a, st_ops_map_b)");

	err = struct_ops_assoc__attach(skel);
	if (!ASSERT_OK(err, "struct_ops_assoc__attach"))
		goto out;

	/* run tracing prog that calls .test_1 and checks return */
	pid = getpid();
	skel->bss->test_pid = pid;
	sys_gettid();
	skel->bss->test_pid = 0;

	ASSERT_EQ(skel->bss->test_err_a, 0, "skel->bss->test_err_a");
	ASSERT_EQ(skel->bss->test_err_b, 0, "skel->bss->test_err_b");

	/* run syscall_prog that calls .test_1 and checks return */
	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.syscall_prog_a), NULL);
	ASSERT_OK(err, "bpf_prog_test_run_opts");

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.syscall_prog_b), NULL);
	ASSERT_OK(err, "bpf_prog_test_run_opts");

	ASSERT_EQ(skel->bss->test_err_a, 0, "skel->bss->test_err_a");
	ASSERT_EQ(skel->bss->test_err_b, 0, "skel->bss->test_err_b");

out:
	struct_ops_assoc__destroy(skel);
}

static void test_st_ops_assoc_reuse(void)
{
	struct struct_ops_assoc_reuse *skel = NULL;
	int err;

	skel = struct_ops_assoc_reuse__open_and_load();
	if (!ASSERT_OK_PTR(skel, "struct_ops_assoc_reuse__open"))
		goto out;

	err = bpf_program__assoc_struct_ops(skel->progs.syscall_prog_a,
					    skel->maps.st_ops_map_a, NULL);
	ASSERT_OK(err, "bpf_program__assoc_struct_ops(syscall_prog_a, st_ops_map_a)");

	err = bpf_program__assoc_struct_ops(skel->progs.syscall_prog_b,
					    skel->maps.st_ops_map_b, NULL);
	ASSERT_OK(err, "bpf_program__assoc_struct_ops(syscall_prog_b, st_ops_map_b)");

	err = struct_ops_assoc_reuse__attach(skel);
	if (!ASSERT_OK(err, "struct_ops_assoc__attach"))
		goto out;

	/* run syscall_prog that calls .test_1 and checks return */
	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.syscall_prog_a), NULL);
	ASSERT_OK(err, "bpf_prog_test_run_opts");

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.syscall_prog_b), NULL);
	ASSERT_OK(err, "bpf_prog_test_run_opts");

	ASSERT_EQ(skel->bss->test_err_a, 0, "skel->bss->test_err_a");
	ASSERT_EQ(skel->bss->test_err_b, 0, "skel->bss->test_err_b");

out:
	struct_ops_assoc_reuse__destroy(skel);
}

void test_struct_ops_assoc(void)
{
	if (test__start_subtest("st_ops_assoc"))
		test_st_ops_assoc();
	if (test__start_subtest("st_ops_assoc_reuse"))
		test_st_ops_assoc_reuse();
}
