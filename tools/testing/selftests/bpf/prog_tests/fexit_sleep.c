// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#define _GNU_SOURCE
#include <sched.h>
#include <test_progs.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include "fexit_sleep.lskel.h"

static int do_sleep(void *skel)
{
	struct fexit_sleep *fexit_skel = skel;
	struct timespec ts1 = { .tv_nsec = 1 };
	struct timespec ts2 = { .tv_sec = 10 };

	fexit_skel->bss->pid = getpid();
	(void)syscall(__NR_nanosleep, &ts1, NULL);
	(void)syscall(__NR_nanosleep, &ts2, NULL);
	return 0;
}

#define STACK_SIZE (1024 * 1024)
static char child_stack[STACK_SIZE];

void test_fexit_sleep(void)
{
	struct fexit_sleep *fexit_skel = NULL;
	int wstatus, duration = 0;
	pid_t cpid;
	int err, fexit_cnt;

	fexit_skel = fexit_sleep__open_and_load();
	if (CHECK(!fexit_skel, "fexit_skel_load", "fexit skeleton failed\n"))
		goto cleanup;

	err = fexit_sleep__attach(fexit_skel);
	if (CHECK(err, "fexit_attach", "fexit attach failed: %d\n", err))
		goto cleanup;

	cpid = clone(do_sleep, child_stack + STACK_SIZE, CLONE_FILES | SIGCHLD, fexit_skel);
	if (CHECK(cpid == -1, "clone", "%s\n", strerror(errno)))
		goto cleanup;

	/* wait until first sys_nanosleep ends and second sys_nanosleep starts */
	while (READ_ONCE(fexit_skel->bss->fentry_cnt) != 2);
	fexit_cnt = READ_ONCE(fexit_skel->bss->fexit_cnt);
	if (CHECK(fexit_cnt != 1, "fexit_cnt", "%d", fexit_cnt))
		goto cleanup;

	/* close progs and detach them. That will trigger two nop5->jmp5 rewrites
	 * in the trampolines to skip nanosleep_fexit prog.
	 * The nanosleep_fentry prog will get detached first.
	 * The nanosleep_fexit prog will get detached second.
	 * Detaching will trigger freeing of both progs JITed images.
	 * There will be two dying bpf_tramp_image-s, but only the initial
	 * bpf_tramp_image (with both _fentry and _fexit progs will be stuck
	 * waiting for percpu_ref_kill to confirm). The other one
	 * will be freed quickly.
	 */
	close(fexit_skel->progs.nanosleep_fentry.prog_fd);
	close(fexit_skel->progs.nanosleep_fexit.prog_fd);
	fexit_sleep__detach(fexit_skel);

	/* kill the thread to unwind sys_nanosleep stack through the trampoline */
	kill(cpid, 9);

	if (CHECK(waitpid(cpid, &wstatus, 0) == -1, "waitpid", "%s\n", strerror(errno)))
		goto cleanup;
	if (CHECK(WEXITSTATUS(wstatus) != 0, "exitstatus", "failed"))
		goto cleanup;

	/* The bypassed nanosleep_fexit prog shouldn't have executed.
	 * Unlike progs the maps were not freed and directly accessible.
	 */
	fexit_cnt = READ_ONCE(fexit_skel->bss->fexit_cnt);
	if (CHECK(fexit_cnt != 1, "fexit_cnt", "%d", fexit_cnt))
		goto cleanup;

cleanup:
	fexit_sleep__destroy(fexit_skel);
}
