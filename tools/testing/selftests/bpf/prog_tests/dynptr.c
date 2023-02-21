// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Facebook */

#include <test_progs.h>
#include "dynptr_fail.skel.h"
#include "dynptr_success.skel.h"

static struct {
	const char *prog_name;
	const char *expected_err_msg;
} dynptr_tests[] = {
	/* success cases */
	{"test_read_write", NULL},
	{"test_data_slice", NULL},
	{"test_ringbuf", NULL},
};

static void verify_success(const char *prog_name)
{
	struct dynptr_success *skel;
	struct bpf_program *prog;
	struct bpf_link *link;

	skel = dynptr_success__open();
	if (!ASSERT_OK_PTR(skel, "dynptr_success__open"))
		return;

	skel->bss->pid = getpid();

	dynptr_success__load(skel);
	if (!ASSERT_OK_PTR(skel, "dynptr_success__load"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto cleanup;

	link = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach"))
		goto cleanup;

	usleep(1);

	ASSERT_EQ(skel->bss->err, 0, "err");

	bpf_link__destroy(link);

cleanup:
	dynptr_success__destroy(skel);
}

void test_dynptr(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dynptr_tests); i++) {
		if (!test__start_subtest(dynptr_tests[i].prog_name))
			continue;

		verify_success(dynptr_tests[i].prog_name);
	}

	RUN_TESTS(dynptr_fail);
}
