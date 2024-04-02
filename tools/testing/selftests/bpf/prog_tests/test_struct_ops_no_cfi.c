// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <testing_helpers.h>

static void load_bpf_test_no_cfi(void)
{
	int fd;
	int err;

	fd = open("bpf_test_no_cfi.ko", O_RDONLY);
	if (!ASSERT_GE(fd, 0, "open"))
		return;

	/* The module will try to register a struct_ops type without
	 * cfi_stubs and with cfi_stubs.
	 *
	 * The one without cfi_stub should fail. The module will be loaded
	 * successfully only if the result of the registration is as
	 * expected, or it fails.
	 */
	err = finit_module(fd, "", 0);
	close(fd);
	if (!ASSERT_OK(err, "finit_module"))
		return;

	err = delete_module("bpf_test_no_cfi", 0);
	ASSERT_OK(err, "delete_module");
}

void test_struct_ops_no_cfi(void)
{
	if (test__start_subtest("load_bpf_test_no_cfi"))
		load_bpf_test_no_cfi();
}
