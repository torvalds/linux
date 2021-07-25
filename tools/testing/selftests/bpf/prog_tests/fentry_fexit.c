// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include "fentry_test.lskel.h"
#include "fexit_test.lskel.h"

void test_fentry_fexit(void)
{
	struct fentry_test *fentry_skel = NULL;
	struct fexit_test *fexit_skel = NULL;
	__u64 *fentry_res, *fexit_res;
	__u32 duration = 0, retval;
	int err, prog_fd, i;

	fentry_skel = fentry_test__open_and_load();
	if (CHECK(!fentry_skel, "fentry_skel_load", "fentry skeleton failed\n"))
		goto close_prog;
	fexit_skel = fexit_test__open_and_load();
	if (CHECK(!fexit_skel, "fexit_skel_load", "fexit skeleton failed\n"))
		goto close_prog;

	err = fentry_test__attach(fentry_skel);
	if (CHECK(err, "fentry_attach", "fentry attach failed: %d\n", err))
		goto close_prog;
	err = fexit_test__attach(fexit_skel);
	if (CHECK(err, "fexit_attach", "fexit attach failed: %d\n", err))
		goto close_prog;

	prog_fd = fexit_skel->progs.test1.prog_fd;
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "ipv6",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	fentry_res = (__u64 *)fentry_skel->bss;
	fexit_res = (__u64 *)fexit_skel->bss;
	printf("%lld\n", fentry_skel->bss->test1_result);
	for (i = 0; i < 8; i++) {
		CHECK(fentry_res[i] != 1, "result",
		      "fentry_test%d failed err %lld\n", i + 1, fentry_res[i]);
		CHECK(fexit_res[i] != 1, "result",
		      "fexit_test%d failed err %lld\n", i + 1, fexit_res[i]);
	}

close_prog:
	fentry_test__destroy(fentry_skel);
	fexit_test__destroy(fexit_skel);
}
