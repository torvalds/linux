// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2020 Google LLC.
 */

#include <test_progs.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>

#include "lsm.skel.h"

char *CMD_ARGS[] = {"true", NULL};

#define GET_PAGE_ADDR(ADDR, PAGE_SIZE)					\
	(char *)(((unsigned long) (ADDR + PAGE_SIZE)) & ~(PAGE_SIZE-1))

int stack_mprotect(void)
{
	void *buf;
	long sz;
	int ret;

	sz = sysconf(_SC_PAGESIZE);
	if (sz < 0)
		return sz;

	buf = alloca(sz * 3);
	ret = mprotect(GET_PAGE_ADDR(buf, sz), sz,
		       PROT_READ | PROT_WRITE | PROT_EXEC);
	return ret;
}

int exec_cmd(int *monitored_pid)
{
	int child_pid, child_status;

	child_pid = fork();
	if (child_pid == 0) {
		*monitored_pid = getpid();
		execvp(CMD_ARGS[0], CMD_ARGS);
		return -EINVAL;
	} else if (child_pid > 0) {
		waitpid(child_pid, &child_status, 0);
		return child_status;
	}

	return -EINVAL;
}

void test_test_lsm(void)
{
	struct lsm *skel = NULL;
	int err, duration = 0;
	int buf = 1234;

	skel = lsm__open_and_load();
	if (CHECK(!skel, "skel_load", "lsm skeleton failed\n"))
		goto close_prog;

	err = lsm__attach(skel);
	if (CHECK(err, "attach", "lsm attach failed: %d\n", err))
		goto close_prog;

	err = exec_cmd(&skel->bss->monitored_pid);
	if (CHECK(err < 0, "exec_cmd", "err %d errno %d\n", err, errno))
		goto close_prog;

	CHECK(skel->bss->bprm_count != 1, "bprm_count", "bprm_count = %d\n",
	      skel->bss->bprm_count);

	skel->bss->monitored_pid = getpid();

	err = stack_mprotect();
	if (CHECK(errno != EPERM, "stack_mprotect", "want err=EPERM, got %d\n",
		  errno))
		goto close_prog;

	CHECK(skel->bss->mprotect_count != 1, "mprotect_count",
	      "mprotect_count = %d\n", skel->bss->mprotect_count);

	syscall(__NR_setdomainname, &buf, -2L);
	syscall(__NR_setdomainname, 0, -3L);
	syscall(__NR_setdomainname, ~0L, -4L);

	CHECK(skel->bss->copy_test != 3, "copy_test",
	      "copy_test = %d\n", skel->bss->copy_test);

close_prog:
	lsm__destroy(skel);
}
