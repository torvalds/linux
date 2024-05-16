// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 *
 * This test attempts to see if the FPU registers are correctly reported in a
 * signal context. Each worker just spins checking its FPU registers, at some
 * point a signal will interrupt it and C code will check the signal context
 * ensuring it is also the same.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <pthread.h>

#include "utils.h"
#include "fpu.h"

/* Number of times each thread should receive the signal */
#define ITERATIONS 10
/*
 * Factor by which to multiply number of online CPUs for total number of
 * worker threads
 */
#define THREAD_FACTOR 8

__thread double darray[32];

bool bad_context;
int threads_starting;
int running;

extern long preempt_fpu(double *darray, int *threads_starting, int *running);

void signal_fpu_sig(int sig, siginfo_t *info, void *context)
{
	int i;
	ucontext_t *uc = context;
	mcontext_t *mc = &uc->uc_mcontext;

	// Don't check f30/f31, they're used as scratches in check_all_fprs()
	for (i = 0; i < 30; i++) {
		if (mc->fp_regs[i] != darray[i]) {
			bad_context = true;
			break;
		}
	}
}

void *signal_fpu_c(void *p)
{
	long rc;
	struct sigaction act;
	act.sa_sigaction = signal_fpu_sig;
	act.sa_flags = SA_SIGINFO;
	rc = sigaction(SIGUSR1, &act, NULL);
	if (rc)
		return p;

	srand(pthread_self());
	randomise_darray(darray, ARRAY_SIZE(darray));
	rc = preempt_fpu(darray, &threads_starting, &running);

	return (void *) rc;
}

int test_signal_fpu(void)
{
	int i, j, rc, threads;
	void *rc_p;
	pthread_t *tids;

	threads = sysconf(_SC_NPROCESSORS_ONLN) * THREAD_FACTOR;
	tids = malloc(threads * sizeof(pthread_t));
	FAIL_IF(!tids);

	running = true;
	threads_starting = threads;
	for (i = 0; i < threads; i++) {
		rc = pthread_create(&tids[i], NULL, signal_fpu_c, NULL);
		FAIL_IF(rc);
	}

	setbuf(stdout, NULL);
	printf("\tWaiting for all workers to start...");
	while (threads_starting)
		asm volatile("": : :"memory");
	printf("done\n");

	printf("\tSending signals to all threads %d times...", ITERATIONS);
	for (i = 0; i < ITERATIONS; i++) {
		for (j = 0; j < threads; j++) {
			pthread_kill(tids[j], SIGUSR1);
		}
		sleep(1);
	}
	printf("done\n");

	printf("\tStopping workers...");
	running = 0;
	for (i = 0; i < threads; i++) {
		pthread_join(tids[i], &rc_p);

		/*
		 * Harness will say the fail was here, look at why signal_fpu
		 * returned
		 */
		if ((long) rc_p || bad_context)
			printf("oops\n");
		if (bad_context)
			fprintf(stderr, "\t!! bad_context is true\n");
		FAIL_IF((long) rc_p || bad_context);
	}
	printf("done\n");

	free(tids);
	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(test_signal_fpu, "fpu_signal");
}
