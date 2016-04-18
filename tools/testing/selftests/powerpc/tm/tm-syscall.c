/*
 * Copyright 2015, Sam Bobroff, IBM Corp.
 * Licensed under GPLv2.
 *
 * Test the kernel's system call code to ensure that a system call
 * made from within an active HTM transaction is aborted with the
 * correct failure code.
 * Conversely, ensure that a system call made from within a
 * suspended transaction can succeed.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <asm/tm.h>
#include <sys/time.h>
#include <stdlib.h>

#include "utils.h"
#include "tm.h"

extern int getppid_tm_active(void);
extern int getppid_tm_suspended(void);

unsigned retries = 0;

#define TEST_DURATION 10 /* seconds */
#define TM_RETRIES 100

long failure_code(void)
{
	return __builtin_get_texasru() >> 24;
}

bool failure_is_persistent(void)
{
	return (failure_code() & TM_CAUSE_PERSISTENT) == TM_CAUSE_PERSISTENT;
}

bool failure_is_syscall(void)
{
	return (failure_code() & TM_CAUSE_SYSCALL) == TM_CAUSE_SYSCALL;
}

pid_t getppid_tm(bool suspend)
{
	int i;
	pid_t pid;

	for (i = 0; i < TM_RETRIES; i++) {
		if (suspend)
			pid = getppid_tm_suspended();
		else
			pid = getppid_tm_active();

		if (pid >= 0)
			return pid;

		if (failure_is_persistent()) {
			if (failure_is_syscall())
				return -1;

			printf("Unexpected persistent transaction failure.\n");
			printf("TEXASR 0x%016lx, TFIAR 0x%016lx.\n",
			       __builtin_get_texasr(), __builtin_get_tfiar());
			exit(-1);
		}

		retries++;
	}

	printf("Exceeded limit of %d temporary transaction failures.\n", TM_RETRIES);
	printf("TEXASR 0x%016lx, TFIAR 0x%016lx.\n",
	       __builtin_get_texasr(), __builtin_get_tfiar());

	exit(-1);
}

int tm_syscall(void)
{
	unsigned count = 0;
	struct timeval end, now;

	SKIP_IF(!have_htm_nosc());

	setbuf(stdout, NULL);

	printf("Testing transactional syscalls for %d seconds...\n", TEST_DURATION);

	gettimeofday(&end, NULL);
	now.tv_sec = TEST_DURATION;
	now.tv_usec = 0;
	timeradd(&end, &now, &end);

	for (count = 0; timercmp(&now, &end, <); count++) {
		/*
		 * Test a syscall within a suspended transaction and verify
		 * that it succeeds.
		 */
		FAIL_IF(getppid_tm(true) == -1); /* Should succeed. */

		/*
		 * Test a syscall within an active transaction and verify that
		 * it fails with the correct failure code.
		 */
		FAIL_IF(getppid_tm(false) != -1);  /* Should fail... */
		FAIL_IF(!failure_is_persistent()); /* ...persistently... */
		FAIL_IF(!failure_is_syscall());    /* ...with code syscall. */
		gettimeofday(&now, 0);
	}

	printf("%d active and suspended transactions behaved correctly.\n", count);
	printf("(There were %d transaction retries.)\n", retries);

	return 0;
}

int main(void)
{
	return test_harness(tm_syscall, "tm_syscall");
}
