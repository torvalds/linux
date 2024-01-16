// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include "fentry_test.lskel.h"

static int fentry_test(struct fentry_test_lskel *fentry_skel)
{
	int err, prog_fd, i;
	int link_fd;
	__u64 *result;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	err = fentry_test_lskel__attach(fentry_skel);
	if (!ASSERT_OK(err, "fentry_attach"))
		return err;

	/* Check that already linked program can't be attached again. */
	link_fd = fentry_test_lskel__test1__attach(fentry_skel);
	if (!ASSERT_LT(link_fd, 0, "fentry_attach_link"))
		return -1;

	prog_fd = fentry_skel->progs.test1.prog_fd;
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	result = (__u64 *)fentry_skel->bss;
	for (i = 0; i < sizeof(*fentry_skel->bss) / sizeof(__u64); i++) {
		if (!ASSERT_EQ(result[i], 1, "fentry_result"))
			return -1;
	}

	fentry_test_lskel__detach(fentry_skel);

	/* zero results for re-attach test */
	memset(fentry_skel->bss, 0, sizeof(*fentry_skel->bss));
	return 0;
}

void test_fentry_test(void)
{
	struct fentry_test_lskel *fentry_skel = NULL;
	int err;

	fentry_skel = fentry_test_lskel__open_and_load();
	if (!ASSERT_OK_PTR(fentry_skel, "fentry_skel_load"))
		goto cleanup;

	err = fentry_test(fentry_skel);
	if (!ASSERT_OK(err, "fentry_first_attach"))
		goto cleanup;

	err = fentry_test(fentry_skel);
	ASSERT_OK(err, "fentry_second_attach");

cleanup:
	fentry_test_lskel__destroy(fentry_skel);
}
