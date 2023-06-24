// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Oracle and/or its affiliates. */

#include <test_progs.h>
#include "test_uprobe_autoattach.skel.h"

/* uprobe attach point */
static noinline int autoattach_trigger_func(int arg)
{
	asm volatile ("");
	return arg + 1;
}

void test_uprobe_autoattach(void)
{
	struct test_uprobe_autoattach *skel;
	int trigger_val = 100, trigger_ret;
	size_t malloc_sz = 1;
	char *mem;

	skel = test_uprobe_autoattach__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	if (!ASSERT_OK(test_uprobe_autoattach__attach(skel), "skel_attach"))
		goto cleanup;

	skel->bss->test_pid = getpid();

	/* trigger & validate uprobe & uretprobe */
	trigger_ret = autoattach_trigger_func(trigger_val);

	skel->bss->test_pid = getpid();

	/* trigger & validate shared library u[ret]probes attached by name */
	mem = malloc(malloc_sz);

	ASSERT_EQ(skel->bss->uprobe_byname_parm1, trigger_val, "check_uprobe_byname_parm1");
	ASSERT_EQ(skel->bss->uprobe_byname_ran, 1, "check_uprobe_byname_ran");
	ASSERT_EQ(skel->bss->uretprobe_byname_rc, trigger_ret, "check_uretprobe_byname_rc");
	ASSERT_EQ(skel->bss->uretprobe_byname_ran, 2, "check_uretprobe_byname_ran");
	ASSERT_EQ(skel->bss->uprobe_byname2_parm1, malloc_sz, "check_uprobe_byname2_parm1");
	ASSERT_EQ(skel->bss->uprobe_byname2_ran, 3, "check_uprobe_byname2_ran");
	ASSERT_EQ(skel->bss->uretprobe_byname2_rc, mem, "check_uretprobe_byname2_rc");
	ASSERT_EQ(skel->bss->uretprobe_byname2_ran, 4, "check_uretprobe_byname2_ran");

	free(mem);
cleanup:
	test_uprobe_autoattach__destroy(skel);
}
