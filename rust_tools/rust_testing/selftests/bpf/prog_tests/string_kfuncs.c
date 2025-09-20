// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Red Hat, Inc.*/
#include <test_progs.h>
#include "string_kfuncs_success.skel.h"
#include "string_kfuncs_failure1.skel.h"
#include "string_kfuncs_failure2.skel.h"
#include <sys/mman.h>

static const char * const test_cases[] = {
	"strcmp",
	"strchr",
	"strchrnul",
	"strnchr",
	"strrchr",
	"strlen",
	"strnlen",
	"strspn_str",
	"strspn_accept",
	"strcspn_str",
	"strcspn_reject",
	"strstr",
	"strnstr",
};

void run_too_long_tests(void)
{
	struct string_kfuncs_failure2 *skel;
	struct bpf_program *prog;
	char test_name[256];
	int err, i;

	skel = string_kfuncs_failure2__open_and_load();
	if (!ASSERT_OK_PTR(skel, "string_kfuncs_failure2__open_and_load"))
		return;

	memset(skel->bss->long_str, 'a', sizeof(skel->bss->long_str));

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		sprintf(test_name, "test_%s_too_long", test_cases[i]);
		if (!test__start_subtest(test_name))
			continue;

		prog = bpf_object__find_program_by_name(skel->obj, test_name);
		if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
			goto cleanup;

		LIBBPF_OPTS(bpf_test_run_opts, topts);
		err = bpf_prog_test_run_opts(bpf_program__fd(prog), &topts);
		if (!ASSERT_OK(err, "bpf_prog_test_run"))
			goto cleanup;

		ASSERT_EQ(topts.retval, -E2BIG, "reading too long string fails with -E2BIG");
	}

cleanup:
	string_kfuncs_failure2__destroy(skel);
}

void test_string_kfuncs(void)
{
	RUN_TESTS(string_kfuncs_success);
	RUN_TESTS(string_kfuncs_failure1);

	run_too_long_tests();
}
