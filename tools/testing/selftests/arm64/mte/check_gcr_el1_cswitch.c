// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "kselftest.h"
#include "mte_common_util.h"

#define PR_SET_TAGGED_ADDR_CTRL 55
#define PR_GET_TAGGED_ADDR_CTRL 56
# define PR_TAGGED_ADDR_ENABLE  (1UL << 0)
# define PR_MTE_TCF_SHIFT	1
# define PR_MTE_TCF_NONE	(0UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_SYNC	(1UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_ASYNC	(2UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_MASK	(3UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TAG_SHIFT	3
# define PR_MTE_TAG_MASK	(0xffffUL << PR_MTE_TAG_SHIFT)

#include "mte_def.h"

#define NUM_ITERATIONS		1024
#define MAX_THREADS		5
#define THREAD_ITERATIONS	1000

void *execute_thread(void *x)
{
	pid_t pid = *((pid_t *)x);
	pid_t tid = gettid();
	uint64_t prctl_tag_mask;
	uint64_t prctl_set;
	uint64_t prctl_get;
	uint64_t prctl_tcf;

	srand(time(NULL) ^ (pid << 16) ^ (tid << 16));

	prctl_tag_mask = rand() & 0xffff;

	if (prctl_tag_mask % 2)
		prctl_tcf = PR_MTE_TCF_SYNC;
	else
		prctl_tcf = PR_MTE_TCF_ASYNC;

	prctl_set = PR_TAGGED_ADDR_ENABLE | prctl_tcf | (prctl_tag_mask << PR_MTE_TAG_SHIFT);

	for (int j = 0; j < THREAD_ITERATIONS; j++) {
		if (prctl(PR_SET_TAGGED_ADDR_CTRL, prctl_set, 0, 0, 0)) {
			perror("prctl() failed");
			goto fail;
		}

		prctl_get = prctl(PR_GET_TAGGED_ADDR_CTRL, 0, 0, 0, 0);

		if (prctl_set != prctl_get) {
			ksft_print_msg("Error: prctl_set: 0x%lx != prctl_get: 0x%lx\n",
						prctl_set, prctl_get);
			goto fail;
		}
	}

	return (void *)KSFT_PASS;

fail:
	return (void *)KSFT_FAIL;
}

int execute_test(pid_t pid)
{
	pthread_t thread_id[MAX_THREADS];
	int thread_data[MAX_THREADS];

	for (int i = 0; i < MAX_THREADS; i++)
		pthread_create(&thread_id[i], NULL,
			       execute_thread, (void *)&pid);

	for (int i = 0; i < MAX_THREADS; i++)
		pthread_join(thread_id[i], (void *)&thread_data[i]);

	for (int i = 0; i < MAX_THREADS; i++)
		if (thread_data[i] == KSFT_FAIL)
			return KSFT_FAIL;

	return KSFT_PASS;
}

int mte_gcr_fork_test(void)
{
	pid_t pid;
	int results[NUM_ITERATIONS];
	pid_t cpid;
	int res;

	for (int i = 0; i < NUM_ITERATIONS; i++) {
		pid = fork();

		if (pid < 0)
			return KSFT_FAIL;

		if (pid == 0) {
			cpid = getpid();

			res = execute_test(cpid);

			exit(res);
		}
	}

	for (int i = 0; i < NUM_ITERATIONS; i++) {
		wait(&res);

		if (WIFEXITED(res))
			results[i] = WEXITSTATUS(res);
		else
			--i;
	}

	for (int i = 0; i < NUM_ITERATIONS; i++)
		if (results[i] == KSFT_FAIL)
			return KSFT_FAIL;

	return KSFT_PASS;
}

int main(int argc, char *argv[])
{
	int err;

	err = mte_default_setup();
	if (err)
		return err;

	ksft_set_plan(1);

	evaluate_test(mte_gcr_fork_test(),
		"Verify that GCR_EL1 is set correctly on context switch\n");

	mte_restore_setup();
	ksft_print_cnts();

	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
