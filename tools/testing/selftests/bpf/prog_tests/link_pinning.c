// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>
#include <sys/stat.h>

#include "test_link_pinning.skel.h"

static int duration = 0;

void test_link_pinning_subtest(struct bpf_program *prog,
			       struct test_link_pinning__bss *bss)
{
	const char *link_pin_path = "/sys/fs/bpf/pinned_link_test";
	struct stat statbuf = {};
	struct bpf_link *link;
	int err, i;

	link = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	bss->in = 1;
	usleep(1);
	CHECK(bss->out != 1, "res_check1", "exp %d, got %d\n", 1, bss->out);

	/* pin link */
	err = bpf_link__pin(link, link_pin_path);
	if (CHECK(err, "link_pin", "err: %d\n", err))
		goto cleanup;

	CHECK(strcmp(link_pin_path, bpf_link__pin_path(link)), "pin_path1",
	      "exp %s, got %s\n", link_pin_path, bpf_link__pin_path(link));

	/* check that link was pinned */
	err = stat(link_pin_path, &statbuf);
	if (CHECK(err, "stat_link", "err %d errno %d\n", err, errno))
		goto cleanup;

	bss->in = 2;
	usleep(1);
	CHECK(bss->out != 2, "res_check2", "exp %d, got %d\n", 2, bss->out);

	/* destroy link, pinned link should keep program attached */
	bpf_link__destroy(link);
	link = NULL;

	bss->in = 3;
	usleep(1);
	CHECK(bss->out != 3, "res_check3", "exp %d, got %d\n", 3, bss->out);

	/* re-open link from BPFFS */
	link = bpf_link__open(link_pin_path);
	if (!ASSERT_OK_PTR(link, "link_open"))
		goto cleanup;

	CHECK(strcmp(link_pin_path, bpf_link__pin_path(link)), "pin_path2",
	      "exp %s, got %s\n", link_pin_path, bpf_link__pin_path(link));

	/* unpin link from BPFFS, program still attached */
	err = bpf_link__unpin(link);
	if (CHECK(err, "link_unpin", "err: %d\n", err))
		goto cleanup;

	/* still active, as we have FD open now */
	bss->in = 4;
	usleep(1);
	CHECK(bss->out != 4, "res_check4", "exp %d, got %d\n", 4, bss->out);

	bpf_link__destroy(link);
	link = NULL;

	/* Validate it's finally detached.
	 * Actual detachment might get delayed a bit, so there is no reliable
	 * way to validate it immediately here, let's count up for long enough
	 * and see if eventually output stops being updated
	 */
	for (i = 5; i < 10000; i++) {
		bss->in = i;
		usleep(1);
		if (bss->out == i - 1)
			break;
	}
	CHECK(i == 10000, "link_attached", "got to iteration #%d\n", i);

cleanup:
	bpf_link__destroy(link);
}

void test_link_pinning(void)
{
	struct test_link_pinning* skel;

	skel = test_link_pinning__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;

	if (test__start_subtest("pin_raw_tp"))
		test_link_pinning_subtest(skel->progs.raw_tp_prog, skel->bss);
	if (test__start_subtest("pin_tp_btf"))
		test_link_pinning_subtest(skel->progs.tp_btf_prog, skel->bss);

	test_link_pinning__destroy(skel);
}
