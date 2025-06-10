// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Microsoft */
#include <test_progs.h>
#include "kfunc_call_test.skel.h"
#include "kfunc_call_test.lskel.h"
#include "test_kernel_flag.skel.h"

void test_kernel_flag(void)
{
	struct test_kernel_flag *lsm_skel;
	struct kfunc_call_test *skel = NULL;
	struct kfunc_call_test_lskel *lskel = NULL;
	int ret;

	lsm_skel = test_kernel_flag__open_and_load();
	if (!ASSERT_OK_PTR(lsm_skel, "lsm_skel"))
		return;

	lsm_skel->bss->monitored_tid = gettid();

	ret = test_kernel_flag__attach(lsm_skel);
	if (!ASSERT_OK(ret, "test_kernel_flag__attach"))
		goto close_prog;

	/* Test with skel. This should pass the gatekeeper */
	skel = kfunc_call_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		goto close_prog;

	/* Test with lskel. This should fail due to blocking kernel-based bpf() invocations */
	lskel = kfunc_call_test_lskel__open_and_load();
	if (!ASSERT_ERR_PTR(lskel, "lskel"))
		goto close_prog;

close_prog:
	if (skel)
		kfunc_call_test__destroy(skel);
	if (lskel)
		kfunc_call_test_lskel__destroy(lskel);

	lsm_skel->bss->monitored_tid = 0;
	test_kernel_flag__destroy(lsm_skel);
}
