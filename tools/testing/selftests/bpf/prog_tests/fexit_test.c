// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include "fexit_test.lskel.h"

static int fexit_test(struct fexit_test_lskel *fexit_skel)
{
	int err, prog_fd, i;
	int link_fd;
	__u64 *result;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	err = fexit_test_lskel__attach(fexit_skel);
	if (!ASSERT_OK(err, "fexit_attach"))
		return err;

	/* Check that already linked program can't be attached again. */
	link_fd = fexit_test_lskel__test1__attach(fexit_skel);
	if (!ASSERT_LT(link_fd, 0, "fexit_attach_link"))
		return -1;

	prog_fd = fexit_skel->progs.test1.prog_fd;
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	result = (__u64 *)fexit_skel->bss;
	for (i = 0; i < sizeof(*fexit_skel->bss) / sizeof(__u64); i++) {
		if (!ASSERT_EQ(result[i], 1, "fexit_result"))
			return -1;
	}

	fexit_test_lskel__detach(fexit_skel);

	/* zero results for re-attach test */
	memset(fexit_skel->bss, 0, sizeof(*fexit_skel->bss));
	return 0;
}

void test_fexit_test(void)
{
	struct fexit_test_lskel *fexit_skel = NULL;
	int err;

	fexit_skel = fexit_test_lskel__open_and_load();
	if (!ASSERT_OK_PTR(fexit_skel, "fexit_skel_load"))
		goto cleanup;

	err = fexit_test(fexit_skel);
	if (!ASSERT_OK(err, "fexit_first_attach"))
		goto cleanup;

	err = fexit_test(fexit_skel);
	ASSERT_OK(err, "fexit_second_attach");

cleanup:
	fexit_test_lskel__destroy(fexit_skel);
}
