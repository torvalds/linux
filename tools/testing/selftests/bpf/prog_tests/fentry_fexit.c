// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include "fentry_test.lskel.h"
#include "fexit_test.lskel.h"

void test_fentry_fexit(void)
{
	struct fentry_test_lskel *fentry_skel = NULL;
	struct fexit_test_lskel *fexit_skel = NULL;
	__u64 *fentry_res, *fexit_res;
	int err, prog_fd, i;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	fentry_skel = fentry_test_lskel__open_and_load();
	if (!ASSERT_OK_PTR(fentry_skel, "fentry_skel_load"))
		goto close_prog;
	fexit_skel = fexit_test_lskel__open_and_load();
	if (!ASSERT_OK_PTR(fexit_skel, "fexit_skel_load"))
		goto close_prog;

	err = fentry_test_lskel__attach(fentry_skel);
	if (!ASSERT_OK(err, "fentry_attach"))
		goto close_prog;
	err = fexit_test_lskel__attach(fexit_skel);
	if (!ASSERT_OK(err, "fexit_attach"))
		goto close_prog;

	prog_fd = fexit_skel->progs.test1.prog_fd;
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "ipv6 test_run");
	ASSERT_OK(topts.retval, "ipv6 test retval");

	fentry_res = (__u64 *)fentry_skel->bss;
	fexit_res = (__u64 *)fexit_skel->bss;
	printf("%lld\n", fentry_skel->bss->test1_result);
	for (i = 0; i < 8; i++) {
		ASSERT_EQ(fentry_res[i], 1, "fentry result");
		ASSERT_EQ(fexit_res[i], 1, "fexit result");
	}

close_prog:
	fentry_test_lskel__destroy(fentry_skel);
	fexit_test_lskel__destroy(fexit_skel);
}
