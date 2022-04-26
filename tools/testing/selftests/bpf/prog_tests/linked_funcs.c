// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <test_progs.h>
#include <sys/syscall.h>
#include "linked_funcs.skel.h"

void test_linked_funcs(void)
{
	int err;
	struct linked_funcs *skel;

	skel = linked_funcs__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	/* handler1 and handler2 are marked as SEC("?raw_tp/sys_enter") and
	 * are set to not autoload by default
	 */
	bpf_program__set_autoload(skel->progs.handler1, true);
	bpf_program__set_autoload(skel->progs.handler2, true);

	skel->rodata->my_tid = syscall(SYS_gettid);
	skel->bss->syscall_id = SYS_getpgid;

	err = linked_funcs__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	err = linked_funcs__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* trigger */
	syscall(SYS_getpgid);

	ASSERT_EQ(skel->bss->output_val1, 2000 + 2000, "output_val1");
	ASSERT_EQ(skel->bss->output_ctx1, SYS_getpgid, "output_ctx1");
	ASSERT_EQ(skel->bss->output_weak1, 42, "output_weak1");

	ASSERT_EQ(skel->bss->output_val2, 2 * 1000 + 2 * (2 * 1000), "output_val2");
	ASSERT_EQ(skel->bss->output_ctx2, SYS_getpgid, "output_ctx2");
	/* output_weak2 should never be updated */
	ASSERT_EQ(skel->bss->output_weak2, 0, "output_weak2");

cleanup:
	linked_funcs__destroy(skel);
}
