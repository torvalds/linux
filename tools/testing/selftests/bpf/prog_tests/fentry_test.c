// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include "fentry_test.skel.h"

static int fentry_test(struct fentry_test *fentry_skel)
{
	int err, prog_fd, i;
	__u32 duration = 0, retval;
	struct bpf_link *link;
	__u64 *result;

	err = fentry_test__attach(fentry_skel);
	if (!ASSERT_OK(err, "fentry_attach"))
		return err;

	/* Check that already linked program can't be attached again. */
	link = bpf_program__attach(fentry_skel->progs.test1);
	if (!ASSERT_ERR_PTR(link, "fentry_attach_link"))
		return -1;

	prog_fd = bpf_program__fd(fentry_skel->progs.test1);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(retval, 0, "test_run");

	result = (__u64 *)fentry_skel->bss;
	for (i = 0; i < sizeof(*fentry_skel->bss) / sizeof(__u64); i++) {
		if (!ASSERT_EQ(result[i], 1, "fentry_result"))
			return -1;
	}

	fentry_test__detach(fentry_skel);

	/* zero results for re-attach test */
	memset(fentry_skel->bss, 0, sizeof(*fentry_skel->bss));
	return 0;
}

void test_fentry_test(void)
{
	struct fentry_test *fentry_skel = NULL;
	int err;

	fentry_skel = fentry_test__open_and_load();
	if (!ASSERT_OK_PTR(fentry_skel, "fentry_skel_load"))
		goto cleanup;

	err = fentry_test(fentry_skel);
	if (!ASSERT_OK(err, "fentry_first_attach"))
		goto cleanup;

	err = fentry_test(fentry_skel);
	ASSERT_OK(err, "fentry_second_attach");

cleanup:
	fentry_test__destroy(fentry_skel);
}
