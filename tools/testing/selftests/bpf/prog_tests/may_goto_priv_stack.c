// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "may_goto_priv_stack.skel.h"

static void check_regs(__u64 *regs)
{
	int i;

	for (i = 0; i < 6; i++)
		ASSERT_EQ(regs[i], 0, "reg");
	/* count is refreshed at least once when may_goto is timed */
	ASSERT_GT(regs[6], 0xffff, "iterations");
}

void test_may_goto_priv_stack(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct may_goto_priv_stack *skel;
	int err;

	skel = may_goto_priv_stack__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	err = may_goto_priv_stack__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto out;

	memset(skel->bss->regs_64, 0xff, sizeof(skel->bss->regs_64));
	memset(skel->bss->regs_128, 0xff, sizeof(skel->bss->regs_128));

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.priv_stack_64), &topts);
	if (!ASSERT_OK(err, "test_run"))
		goto out;

	check_regs(skel->bss->regs_64);
	check_regs(skel->bss->regs_128);
out:
	may_goto_priv_stack__destroy(skel);
}
