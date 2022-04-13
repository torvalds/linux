// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <test_progs.h>
#include "test_task_pt_regs.skel.h"

/* uprobe attach point */
static void trigger_func(void)
{
	asm volatile ("");
}

void test_task_pt_regs(void)
{
	struct test_task_pt_regs *skel;
	struct bpf_link *uprobe_link;
	ssize_t uprobe_offset;
	bool match;

	uprobe_offset = get_uprobe_offset(&trigger_func);
	if (!ASSERT_GE(uprobe_offset, 0, "uprobe_offset"))
		return;

	skel = test_task_pt_regs__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;
	if (!ASSERT_OK_PTR(skel->bss, "check_bss"))
		goto cleanup;

	uprobe_link = bpf_program__attach_uprobe(skel->progs.handle_uprobe,
						 false /* retprobe */,
						 0 /* self pid */,
						 "/proc/self/exe",
						 uprobe_offset);
	if (!ASSERT_OK_PTR(uprobe_link, "attach_uprobe"))
		goto cleanup;
	skel->links.handle_uprobe = uprobe_link;

	/* trigger & validate uprobe */
	trigger_func();

	if (!ASSERT_EQ(skel->bss->uprobe_res, 1, "check_uprobe_res"))
		goto cleanup;

	match = !memcmp(&skel->bss->current_regs, &skel->bss->ctx_regs,
			sizeof(skel->bss->current_regs));
	ASSERT_TRUE(match, "check_regs_match");

cleanup:
	test_task_pt_regs__destroy(skel);
}
