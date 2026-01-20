// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "testing_helpers.h"
#include "livepatch_trampoline.skel.h"

static int load_livepatch(void)
{
	char path[4096];

	/* CI will set KBUILD_OUTPUT */
	snprintf(path, sizeof(path), "%s/samples/livepatch/livepatch-sample.ko",
		 getenv("KBUILD_OUTPUT") ? : "../../../..");

	return load_module(path, env_verbosity > VERBOSE_NONE);
}

static void unload_livepatch(void)
{
	/* Disable the livepatch before unloading the module */
	system("echo 0 > /sys/kernel/livepatch/livepatch_sample/enabled");

	unload_module("livepatch_sample", env_verbosity > VERBOSE_NONE);
}

static void read_proc_cmdline(void)
{
	char buf[4096];
	int fd, ret;

	fd = open("/proc/cmdline", O_RDONLY);
	if (!ASSERT_OK_FD(fd, "open /proc/cmdline"))
		return;

	ret = read(fd, buf, sizeof(buf));
	if (!ASSERT_GT(ret, 0, "read /proc/cmdline"))
		goto out;

	ASSERT_OK(strncmp(buf, "this has been live patched", 26), "strncmp");

out:
	close(fd);
}

static void __test_livepatch_trampoline(bool fexit_first)
{
	struct livepatch_trampoline *skel = NULL;
	int err;

	skel = livepatch_trampoline__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		goto out;

	skel->bss->my_pid = getpid();

	if (!fexit_first) {
		/* fentry program is loaded first by default */
		err = livepatch_trampoline__attach(skel);
		if (!ASSERT_OK(err, "skel_attach"))
			goto out;
	} else {
		/* Manually load fexit program first. */
		skel->links.fexit_cmdline = bpf_program__attach(skel->progs.fexit_cmdline);
		if (!ASSERT_OK_PTR(skel->links.fexit_cmdline, "attach_fexit"))
			goto out;

		skel->links.fentry_cmdline = bpf_program__attach(skel->progs.fentry_cmdline);
		if (!ASSERT_OK_PTR(skel->links.fentry_cmdline, "attach_fentry"))
			goto out;
	}

	read_proc_cmdline();

	ASSERT_EQ(skel->bss->fentry_hit, 1, "fentry_hit");
	ASSERT_EQ(skel->bss->fexit_hit, 1, "fexit_hit");
out:
	livepatch_trampoline__destroy(skel);
}

void test_livepatch_trampoline(void)
{
	int retry_cnt = 0;

retry:
	if (load_livepatch()) {
		if (retry_cnt) {
			ASSERT_OK(1, "load_livepatch");
			goto out;
		}
		/*
		 * Something else (previous run of the same test?) loaded
		 * the KLP module. Unload the KLP module and retry.
		 */
		unload_livepatch();
		retry_cnt++;
		goto retry;
	}

	if (test__start_subtest("fentry_first"))
		__test_livepatch_trampoline(false);

	if (test__start_subtest("fexit_first"))
		__test_livepatch_trampoline(true);
out:
	unload_livepatch();
}
