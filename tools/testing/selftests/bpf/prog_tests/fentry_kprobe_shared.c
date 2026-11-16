// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "fentry_kprobe_shared.skel.h"

static void trigger(struct fentry_kprobe_shared *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err;

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.fentry1), &topts);
	ASSERT_OK(err, "test_run");
}

/*
 * With a kprobe next to the trampoline, the ftrace site of the function
 * reaches the trampoline through the list of ftrace callbacks, and so does
 * the site of a trampoline that is modified to add or remove a program.
 * With the fexit program, the trampoline is entered by a jmp.
 */
void test_fentry_kprobe_shared(void)
{
	struct fentry_kprobe_shared *skel;

	skel = fentry_kprobe_shared__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;
	skel->bss->pid = getpid();

	skel->links.fentry1 = bpf_program__attach(skel->progs.fentry1);
	if (!ASSERT_OK_PTR(skel->links.fentry1, "attach_fentry1"))
		goto out;
	skel->links.kprobe1 = bpf_program__attach(skel->progs.kprobe1);
	if (!ASSERT_OK_PTR(skel->links.kprobe1, "attach_kprobe1"))
		goto out;
	trigger(skel);
	ASSERT_EQ(skel->bss->fentry1_hits, 1, "fentry1_hits");
	ASSERT_EQ(skel->bss->kprobe_hits, 1, "kprobe_hits");

	skel->links.fentry2 = bpf_program__attach(skel->progs.fentry2);
	if (!ASSERT_OK_PTR(skel->links.fentry2, "attach_fentry2"))
		goto out;
	trigger(skel);
	ASSERT_EQ(skel->bss->fentry1_hits, 2, "fentry1_hits");
	ASSERT_EQ(skel->bss->fentry2_hits, 1, "fentry2_hits");
	ASSERT_EQ(skel->bss->kprobe_hits, 2, "kprobe_hits");

	skel->links.fexit1 = bpf_program__attach(skel->progs.fexit1);
	if (!ASSERT_OK_PTR(skel->links.fexit1, "attach_fexit1"))
		goto out;
	trigger(skel);
	ASSERT_EQ(skel->bss->fentry1_hits, 3, "fentry1_hits");
	ASSERT_EQ(skel->bss->fentry2_hits, 2, "fentry2_hits");
	ASSERT_EQ(skel->bss->fexit_hits, 1, "fexit_hits");
	ASSERT_EQ(skel->bss->kprobe_hits, 3, "kprobe_hits");

	bpf_link__destroy(skel->links.kprobe1);
	skel->links.kprobe1 = NULL;
	trigger(skel);
	ASSERT_EQ(skel->bss->fentry1_hits, 4, "fentry1_hits");
	ASSERT_EQ(skel->bss->fentry2_hits, 3, "fentry2_hits");
	ASSERT_EQ(skel->bss->fexit_hits, 2, "fexit_hits");
	ASSERT_EQ(skel->bss->kprobe_hits, 3, "kprobe_hits");
out:
	fentry_kprobe_shared__destroy(skel);
}
