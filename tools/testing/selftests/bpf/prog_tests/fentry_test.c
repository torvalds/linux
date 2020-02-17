// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include "test_pkt_access.skel.h"
#include "fentry_test.skel.h"

void test_fentry_test(void)
{
	struct test_pkt_access *pkt_skel = NULL;
	struct fentry_test *fentry_skel = NULL;
	int err, pkt_fd, i;
	__u32 duration = 0, retval;
	__u64 *result;

	pkt_skel = test_pkt_access__open_and_load();
	if (CHECK(!pkt_skel, "pkt_skel_load", "pkt_access skeleton failed\n"))
		return;
	fentry_skel = fentry_test__open_and_load();
	if (CHECK(!fentry_skel, "fentry_skel_load", "fentry skeleton failed\n"))
		goto cleanup;

	err = fentry_test__attach(fentry_skel);
	if (CHECK(err, "fentry_attach", "fentry attach failed: %d\n", err))
		goto cleanup;

	pkt_fd = bpf_program__fd(pkt_skel->progs.test_pkt_access);
	err = bpf_prog_test_run(pkt_fd, 1, &pkt_v6, sizeof(pkt_v6),
				NULL, NULL, &retval, &duration);
	CHECK(err || retval, "ipv6",
	      "err %d errno %d retval %d duration %d\n",
	      err, errno, retval, duration);

	result = (__u64 *)fentry_skel->bss;
	for (i = 0; i < 6; i++) {
		if (CHECK(result[i] != 1, "result",
			  "fentry_test%d failed err %lld\n", i + 1, result[i]))
			goto cleanup;
	}

cleanup:
	fentry_test__destroy(fentry_skel);
	test_pkt_access__destroy(pkt_skel);
}
