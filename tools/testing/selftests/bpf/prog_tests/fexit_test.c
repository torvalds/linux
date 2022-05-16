// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include "fexit_test.skel.h"

void test_fexit_test(void)
{
	struct fexit_test *fexit_skel = NULL;
	int err, prog_fd, i;
	__u32 duration = 0, retval;
	__u64 *result;

	fexit_skel = fexit_test__open_and_load();
	if (CHECK(!fexit_skel, "fexit_skel_load", "fexit skeleton failed\n"))
		goto cleanup;

	err = fexit_test__attach(fexit_skel);
	if (CHECK(err, "fexit_attach", "fexit attach failed: %d\n", err))
		goto cleanup;

	prog_fd = bpf_program__fd(fexit_skel->progs.test1);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0,
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "test_run",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	result = (__u64 *)fexit_skel->bss;
	for (i = 0; i < 6; i++) {
		if (CHECK(result[i] != 1, "result",
			  "fexit_test%d failed err %lld\n", i + 1, result[i]))
			goto cleanup;
	}

cleanup:
	fexit_test__destroy(fexit_skel);
}
