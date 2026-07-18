// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>
#include <time.h>
#include "test_vmlinux.skel.h"

#define MY_TV_NSEC 1337

static void nsleep()
{
	struct timespec ts = { .tv_nsec = MY_TV_NSEC };

	(void)syscall(__NR_nanosleep, &ts, NULL);
}

static const char *hrtimer_func = "hrtimer_start_range_ns";

static int setup_hrtimer_progs(struct test_vmlinux *skel)
{
	int err;

	if (libbpf_find_vmlinux_btf_id("hrtimer_start_range_ns_user", BPF_TRACE_FENTRY) > 0)
		hrtimer_func = "hrtimer_start_range_ns_user";

	err = bpf_program__set_attach_target(skel->progs.handle__fentry, 0, hrtimer_func);
	if (err)
		return err;

	/*
	 * Bare SEC("kprobe") has no target function, so attach it manually
	 * later after selecting the hrtimer function to probe.
	 */
	bpf_program__set_autoattach(skel->progs.handle__kprobe, false);

	return 0;
}

void test_vmlinux(void)
{
	int err;
	struct test_vmlinux* skel;
	struct test_vmlinux__bss *bss;
	struct bpf_link *kprobe_link = NULL;

	skel = test_vmlinux__open();
	if (!ASSERT_OK_PTR(skel, "test_vmlinux__open"))
		return;

	err = setup_hrtimer_progs(skel);
	if (!ASSERT_OK(err, "setup_hrtimer_progs"))
		goto cleanup;

	err = test_vmlinux__load(skel);
	if (!ASSERT_OK(err, "test_vmlinux__load"))
		goto cleanup;

	bss = skel->bss;

	err = test_vmlinux__attach(skel);
	if (!ASSERT_OK(err, "test_vmlinux__attach"))
		goto cleanup;

	/* manually attach kprobe with the selected function */
	if (hrtimer_func) {
		kprobe_link = bpf_program__attach_kprobe(skel->progs.handle__kprobe,
							 false /* retprobe */, hrtimer_func);
		if (!ASSERT_OK_PTR(kprobe_link, "bpf_program__attach_kprobe"))
			goto cleanup;
	}

	/* trigger everything */
	nsleep();

	ASSERT_TRUE(bss->tp_called, "tp");
	ASSERT_TRUE(bss->raw_tp_called, "raw_tp");
	ASSERT_TRUE(bss->tp_btf_called, "tp_btf");
	ASSERT_TRUE(bss->kprobe_called, "kprobe");
	ASSERT_TRUE(bss->fentry_called, "fentry");

cleanup:
	bpf_link__destroy(kprobe_link);
	test_vmlinux__destroy(skel);
}
