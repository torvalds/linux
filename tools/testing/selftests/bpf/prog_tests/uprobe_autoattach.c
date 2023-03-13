// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Oracle and/or its affiliates. */

#include <test_progs.h>
#include "test_uprobe_autoattach.skel.h"
#include "progs/bpf_misc.h"

/* uprobe attach point */
static noinline int autoattach_trigger_func(int arg1, int arg2, int arg3,
					    int arg4, int arg5, int arg6,
					    int arg7, int arg8)
{
	asm volatile ("");
	return arg1 + arg2 + arg3 + arg4 + arg5 + arg6 + arg7 + arg8 + 1;
}

void test_uprobe_autoattach(void)
{
	const char *devnull_str = "/dev/null";
	struct test_uprobe_autoattach *skel;
	int trigger_ret;
	FILE *devnull;

	skel = test_uprobe_autoattach__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	if (!ASSERT_OK(test_uprobe_autoattach__attach(skel), "skel_attach"))
		goto cleanup;

	skel->bss->test_pid = getpid();

	/* trigger & validate uprobe & uretprobe */
	trigger_ret = autoattach_trigger_func(1, 2, 3, 4, 5, 6, 7, 8);

	skel->bss->test_pid = getpid();

	/* trigger & validate shared library u[ret]probes attached by name */
	devnull = fopen(devnull_str, "r");

	ASSERT_EQ(skel->bss->uprobe_byname_parm1, 1, "check_uprobe_byname_parm1");
	ASSERT_EQ(skel->bss->uprobe_byname_ran, 1, "check_uprobe_byname_ran");
	ASSERT_EQ(skel->bss->uretprobe_byname_rc, trigger_ret, "check_uretprobe_byname_rc");
	ASSERT_EQ(skel->bss->uretprobe_byname_ret, trigger_ret, "check_uretprobe_byname_ret");
	ASSERT_EQ(skel->bss->uretprobe_byname_ran, 2, "check_uretprobe_byname_ran");
	ASSERT_EQ(skel->bss->uprobe_byname2_parm1, (__u64)(long)devnull_str,
		  "check_uprobe_byname2_parm1");
	ASSERT_EQ(skel->bss->uprobe_byname2_ran, 3, "check_uprobe_byname2_ran");
	ASSERT_EQ(skel->bss->uretprobe_byname2_rc, (__u64)(long)devnull,
		  "check_uretprobe_byname2_rc");
	ASSERT_EQ(skel->bss->uretprobe_byname2_ran, 4, "check_uretprobe_byname2_ran");

	ASSERT_EQ(skel->bss->a[0], 1, "arg1");
	ASSERT_EQ(skel->bss->a[1], 2, "arg2");
	ASSERT_EQ(skel->bss->a[2], 3, "arg3");
#if FUNC_REG_ARG_CNT > 3
	ASSERT_EQ(skel->bss->a[3], 4, "arg4");
#endif
#if FUNC_REG_ARG_CNT > 4
	ASSERT_EQ(skel->bss->a[4], 5, "arg5");
#endif
#if FUNC_REG_ARG_CNT > 5
	ASSERT_EQ(skel->bss->a[5], 6, "arg6");
#endif
#if FUNC_REG_ARG_CNT > 6
	ASSERT_EQ(skel->bss->a[6], 7, "arg7");
#endif
#if FUNC_REG_ARG_CNT > 7
	ASSERT_EQ(skel->bss->a[7], 8, "arg8");
#endif

	fclose(devnull);
cleanup:
	test_uprobe_autoattach__destroy(skel);
}
