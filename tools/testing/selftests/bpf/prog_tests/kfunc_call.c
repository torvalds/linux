// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include <network_helpers.h>
#include "kfunc_call_fail.skel.h"
#include "kfunc_call_test.skel.h"
#include "kfunc_call_test.lskel.h"
#include "kfunc_call_test_subprog.skel.h"
#include "kfunc_call_test_subprog.lskel.h"
#include "kfunc_call_destructive.skel.h"

#include "cap_helpers.h"

static size_t log_buf_sz = 1048576; /* 1 MB */
static char obj_log_buf[1048576];

enum kfunc_test_type {
	tc_test = 0,
	syscall_test,
	syscall_null_ctx_test,
};

struct kfunc_test_params {
	const char *prog_name;
	unsigned long lskel_prog_desc_offset;
	int retval;
	enum kfunc_test_type test_type;
	const char *expected_err_msg;
};

#define __BPF_TEST_SUCCESS(name, __retval, type) \
	{ \
	  .prog_name = #name, \
	  .lskel_prog_desc_offset = offsetof(struct kfunc_call_test_lskel, progs.name), \
	  .retval = __retval, \
	  .test_type = type, \
	  .expected_err_msg = NULL, \
	}

#define __BPF_TEST_FAIL(name, __retval, type, error_msg) \
	{ \
	  .prog_name = #name, \
	  .lskel_prog_desc_offset = 0 /* unused when test is failing */, \
	  .retval = __retval, \
	  .test_type = type, \
	  .expected_err_msg = error_msg, \
	}

#define TC_TEST(name, retval) __BPF_TEST_SUCCESS(name, retval, tc_test)
#define SYSCALL_TEST(name, retval) __BPF_TEST_SUCCESS(name, retval, syscall_test)
#define SYSCALL_NULL_CTX_TEST(name, retval) __BPF_TEST_SUCCESS(name, retval, syscall_null_ctx_test)

#define TC_FAIL(name, retval, error_msg) __BPF_TEST_FAIL(name, retval, tc_test, error_msg)
#define SYSCALL_NULL_CTX_FAIL(name, retval, error_msg) \
	__BPF_TEST_FAIL(name, retval, syscall_null_ctx_test, error_msg)

static struct kfunc_test_params kfunc_tests[] = {
	/* failure cases:
	 * if retval is 0 -> the program will fail to load and the error message is an error
	 * if retval is not 0 -> the program can be loaded but running it will gives the
	 *                       provided return value. The error message is thus the one
	 *                       from a successful load
	 */
	SYSCALL_NULL_CTX_FAIL(kfunc_syscall_test_fail, -EINVAL, "processed 4 insns"),
	SYSCALL_NULL_CTX_FAIL(kfunc_syscall_test_null_fail, -EINVAL, "processed 4 insns"),
	TC_FAIL(kfunc_call_test_get_mem_fail_rdonly, 0, "R0 cannot write into rdonly_mem"),
	TC_FAIL(kfunc_call_test_get_mem_fail_use_after_free, 0, "invalid mem access 'scalar'"),
	TC_FAIL(kfunc_call_test_get_mem_fail_oob, 0, "min value is outside of the allowed memory range"),
	TC_FAIL(kfunc_call_test_get_mem_fail_not_const, 0, "is not a const"),
	TC_FAIL(kfunc_call_test_mem_acquire_fail, 0, "acquire kernel function does not return PTR_TO_BTF_ID"),

	/* success cases */
	TC_TEST(kfunc_call_test1, 12),
	TC_TEST(kfunc_call_test2, 3),
	TC_TEST(kfunc_call_test4, -1234),
	TC_TEST(kfunc_call_test_ref_btf_id, 0),
	TC_TEST(kfunc_call_test_get_mem, 42),
	SYSCALL_TEST(kfunc_syscall_test, 0),
	SYSCALL_NULL_CTX_TEST(kfunc_syscall_test_null, 0),
	TC_TEST(kfunc_call_test_static_unused_arg, 0),
};

struct syscall_test_args {
	__u8 data[16];
	size_t size;
};

static void verify_success(struct kfunc_test_params *param)
{
	struct kfunc_call_test_lskel *lskel = NULL;
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct bpf_prog_desc *lskel_prog;
	struct kfunc_call_test *skel;
	struct bpf_program *prog;
	int prog_fd, err;
	struct syscall_test_args args = {
		.size = 10,
	};

	switch (param->test_type) {
	case syscall_test:
		topts.ctx_in = &args;
		topts.ctx_size_in = sizeof(args);
		/* fallthrough */
	case syscall_null_ctx_test:
		break;
	case tc_test:
		topts.data_in = &pkt_v4;
		topts.data_size_in = sizeof(pkt_v4);
		topts.repeat = 1;
		break;
	}

	/* first test with normal libbpf */
	skel = kfunc_call_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, param->prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto cleanup;

	prog_fd = bpf_program__fd(prog);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, param->prog_name))
		goto cleanup;

	if (!ASSERT_EQ(topts.retval, param->retval, "retval"))
		goto cleanup;

	/* second test with light skeletons */
	lskel = kfunc_call_test_lskel__open_and_load();
	if (!ASSERT_OK_PTR(lskel, "lskel"))
		goto cleanup;

	lskel_prog = (struct bpf_prog_desc *)((char *)lskel + param->lskel_prog_desc_offset);

	prog_fd = lskel_prog->prog_fd;
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	if (!ASSERT_OK(err, param->prog_name))
		goto cleanup;

	ASSERT_EQ(topts.retval, param->retval, "retval");

cleanup:
	kfunc_call_test__destroy(skel);
	if (lskel)
		kfunc_call_test_lskel__destroy(lskel);
}

static void verify_fail(struct kfunc_test_params *param)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct bpf_program *prog;
	struct kfunc_call_fail *skel;
	int prog_fd, err;
	struct syscall_test_args args = {
		.size = 10,
	};

	opts.kernel_log_buf = obj_log_buf;
	opts.kernel_log_size = log_buf_sz;
	opts.kernel_log_level = 1;

	switch (param->test_type) {
	case syscall_test:
		topts.ctx_in = &args;
		topts.ctx_size_in = sizeof(args);
		/* fallthrough */
	case syscall_null_ctx_test:
		break;
	case tc_test:
		topts.data_in = &pkt_v4;
		topts.data_size_in = sizeof(pkt_v4);
		break;
		topts.repeat = 1;
	}

	skel = kfunc_call_fail__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "kfunc_call_fail__open_opts"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(skel->obj, param->prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto cleanup;

	bpf_program__set_autoload(prog, true);

	err = kfunc_call_fail__load(skel);
	if (!param->retval) {
		/* the verifier is supposed to complain and refuses to load */
		if (!ASSERT_ERR(err, "unexpected load success"))
			goto out_err;

	} else {
		/* the program is loaded but must dynamically fail */
		if (!ASSERT_OK(err, "unexpected load error"))
			goto out_err;

		prog_fd = bpf_program__fd(prog);
		err = bpf_prog_test_run_opts(prog_fd, &topts);
		if (!ASSERT_EQ(err, param->retval, param->prog_name))
			goto out_err;
	}

out_err:
	if (!ASSERT_OK_PTR(strstr(obj_log_buf, param->expected_err_msg), "expected_err_msg")) {
		fprintf(stderr, "Expected err_msg: %s\n", param->expected_err_msg);
		fprintf(stderr, "Verifier output: %s\n", obj_log_buf);
	}

cleanup:
	kfunc_call_fail__destroy(skel);
}

static void test_main(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kfunc_tests); i++) {
		if (!test__start_subtest(kfunc_tests[i].prog_name))
			continue;

		if (!kfunc_tests[i].expected_err_msg)
			verify_success(&kfunc_tests[i]);
		else
			verify_fail(&kfunc_tests[i]);
	}
}

static void test_subprog(void)
{
	struct kfunc_call_test_subprog *skel;
	int prog_fd, err;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	skel = kfunc_call_test_subprog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;

	prog_fd = bpf_program__fd(skel->progs.kfunc_call_test1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "bpf_prog_test_run(test1)");
	ASSERT_EQ(topts.retval, 10, "test1-retval");
	ASSERT_NEQ(skel->data->active_res, -1, "active_res");
	ASSERT_EQ(skel->data->sk_state_res, BPF_TCP_CLOSE, "sk_state_res");

	kfunc_call_test_subprog__destroy(skel);
}

static void test_subprog_lskel(void)
{
	struct kfunc_call_test_subprog_lskel *skel;
	int prog_fd, err;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	skel = kfunc_call_test_subprog_lskel__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;

	prog_fd = skel->progs.kfunc_call_test1.prog_fd;
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "bpf_prog_test_run(test1)");
	ASSERT_EQ(topts.retval, 10, "test1-retval");
	ASSERT_NEQ(skel->data->active_res, -1, "active_res");
	ASSERT_EQ(skel->data->sk_state_res, BPF_TCP_CLOSE, "sk_state_res");

	kfunc_call_test_subprog_lskel__destroy(skel);
}

static int test_destructive_open_and_load(void)
{
	struct kfunc_call_destructive *skel;
	int err;

	skel = kfunc_call_destructive__open();
	if (!ASSERT_OK_PTR(skel, "prog_open"))
		return -1;

	err = kfunc_call_destructive__load(skel);

	kfunc_call_destructive__destroy(skel);

	return err;
}

static void test_destructive(void)
{
	__u64 save_caps = 0;

	ASSERT_OK(test_destructive_open_and_load(), "successful_load");

	if (!ASSERT_OK(cap_disable_effective(1ULL << CAP_SYS_BOOT, &save_caps), "drop_caps"))
		return;

	ASSERT_EQ(test_destructive_open_and_load(), -13, "no_caps_failure");

	cap_enable_effective(save_caps, NULL);
}

void test_kfunc_call(void)
{
	test_main();

	if (test__start_subtest("subprog"))
		test_subprog();

	if (test__start_subtest("subprog_lskel"))
		test_subprog_lskel();

	if (test__start_subtest("destructive"))
		test_destructive();
}
