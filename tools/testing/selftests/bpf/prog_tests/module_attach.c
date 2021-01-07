// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>
#include "test_module_attach.skel.h"

static int duration;

static int trigger_module_test_read(int read_sz)
{
	int fd, err;

	fd = open("/sys/kernel/bpf_testmod", O_RDONLY);
	err = -errno;
	if (CHECK(fd < 0, "testmod_file_open", "failed: %d\n", err))
		return err;

	read(fd, NULL, read_sz);
	close(fd);

	return 0;
}

void test_module_attach(void)
{
	const int READ_SZ = 456;
	struct test_module_attach* skel;
	struct test_module_attach__bss *bss;
	int err;

	skel = test_module_attach__open();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;

	err = bpf_program__set_attach_target(skel->progs.handle_fentry_manual,
					     0, "bpf_testmod_test_read");
	ASSERT_OK(err, "set_attach_target");

	err = test_module_attach__load(skel);
	if (CHECK(err, "skel_load", "failed to load skeleton\n"))
		return;

	bss = skel->bss;

	err = test_module_attach__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	/* trigger tracepoint */
	ASSERT_OK(trigger_module_test_read(READ_SZ), "trigger_read");

	ASSERT_EQ(bss->raw_tp_read_sz, READ_SZ, "raw_tp");
	ASSERT_EQ(bss->tp_btf_read_sz, READ_SZ, "tp_btf");
	ASSERT_EQ(bss->fentry_read_sz, READ_SZ, "fentry");
	ASSERT_EQ(bss->fentry_manual_read_sz, READ_SZ, "fentry_manual");
	ASSERT_EQ(bss->fexit_read_sz, READ_SZ, "fexit");
	ASSERT_EQ(bss->fexit_ret, -EIO, "fexit_tet");
	ASSERT_EQ(bss->fmod_ret_read_sz, READ_SZ, "fmod_ret");

cleanup:
	test_module_attach__destroy(skel);
}
