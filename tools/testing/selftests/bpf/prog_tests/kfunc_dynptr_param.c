// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2022 Facebook
 * Copyright (C) 2022 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 */

#include <test_progs.h>
#include "test_kfunc_dynptr_param.skel.h"

static struct {
	const char *prog_name;
	int expected_runtime_err;
} kfunc_dynptr_tests[] = {
	{"dynptr_data_null", -EBADMSG},
};

static bool kfunc_not_supported;

static int libbpf_print_cb(enum libbpf_print_level level, const char *fmt,
			   va_list args)
{
	if (strcmp(fmt, "libbpf: extern (func ksym) '%s': not found in kernel or module BTFs\n"))
		return 0;

	if (strcmp(va_arg(args, char *), "bpf_verify_pkcs7_signature"))
		return 0;

	kfunc_not_supported = true;
	return 0;
}

static bool has_pkcs7_kfunc_support(void)
{
	struct test_kfunc_dynptr_param *skel;
	libbpf_print_fn_t old_print_cb;
	int err;

	skel = test_kfunc_dynptr_param__open();
	if (!ASSERT_OK_PTR(skel, "test_kfunc_dynptr_param__open"))
		return false;

	kfunc_not_supported = false;

	old_print_cb = libbpf_set_print(libbpf_print_cb);
	err = test_kfunc_dynptr_param__load(skel);
	libbpf_set_print(old_print_cb);

	if (err < 0 && kfunc_not_supported) {
		fprintf(stderr,
		  "%s:SKIP:bpf_verify_pkcs7_signature() kfunc not supported\n",
		  __func__);
		test_kfunc_dynptr_param__destroy(skel);
		return false;
	}

	test_kfunc_dynptr_param__destroy(skel);

	return true;
}

static void verify_success(const char *prog_name, int expected_runtime_err)
{
	struct test_kfunc_dynptr_param *skel;
	struct bpf_program *prog;
	struct bpf_link *link;
	__u32 next_id;
	int err;

	skel = test_kfunc_dynptr_param__open();
	if (!ASSERT_OK_PTR(skel, "test_kfunc_dynptr_param__open"))
		return;

	skel->bss->pid = getpid();

	err = test_kfunc_dynptr_param__load(skel);

	if (!ASSERT_OK(err, "test_kfunc_dynptr_param__load"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto cleanup;

	link = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach"))
		goto cleanup;

	err = bpf_prog_get_next_id(0, &next_id);

	bpf_link__destroy(link);

	if (!ASSERT_OK(err, "bpf_prog_get_next_id"))
		goto cleanup;

	ASSERT_EQ(skel->bss->err, expected_runtime_err, "err");

cleanup:
	test_kfunc_dynptr_param__destroy(skel);
}

void test_kfunc_dynptr_param(void)
{
	int i;

	if (!has_pkcs7_kfunc_support())
		return;

	for (i = 0; i < ARRAY_SIZE(kfunc_dynptr_tests); i++) {
		if (!test__start_subtest(kfunc_dynptr_tests[i].prog_name))
			continue;

		verify_success(kfunc_dynptr_tests[i].prog_name,
			kfunc_dynptr_tests[i].expected_runtime_err);
	}
	RUN_TESTS(test_kfunc_dynptr_param);
}
