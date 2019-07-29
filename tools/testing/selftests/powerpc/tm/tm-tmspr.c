/*
 * Copyright 2015, Michael Neuling, IBM Corp.
 * Licensed under GPLv2.
 *
 * Original: Michael Neuling 3/4/2014
 * Modified: Rashmica Gupta 8/12/2015
 *
 * Check if any of the Transaction Memory SPRs get corrupted.
 * - TFIAR  - stores address of location of transaction failure
 * - TFHAR  - stores address of software failure handler (if transaction
 *   fails)
 * - TEXASR - lots of info about the transacion(s)
 *
 * (1) create more threads than cpus
 * (2) in each thread:
 * 	(a) set TFIAR and TFHAR a unique value
 * 	(b) loop for awhile, continually checking to see if
 * 	either register has been corrupted.
 *
 * (3) Loop:
 * 	(a) begin transaction
 *    	(b) abort transaction
 *	(c) check TEXASR to see if FS has been corrupted
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "utils.h"
#include "tm.h"

int	num_loops	= 10000;
int	passed = 1;

void tfiar_tfhar(void *in)
{
	int i, cpu;
	unsigned long tfhar, tfhar_rd, tfiar, tfiar_rd;
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	cpu = (unsigned long)in >> 1;
	CPU_SET(cpu, &cpuset);
	sched_setaffinity(0, sizeof(cpuset), &cpuset);

	/* TFIAR: Last bit has to be high so userspace can read register */
	tfiar = ((unsigned long)in) + 1;
	tfiar += 2;
	mtspr(SPRN_TFIAR, tfiar);

	/* TFHAR: Last two bits are reserved */
	tfhar = ((unsigned long)in);
	tfhar &= ~0x3UL;
	tfhar += 4;
	mtspr(SPRN_TFHAR, tfhar);

	for (i = 0; i < num_loops; i++)	{
		tfhar_rd = mfspr(SPRN_TFHAR);
		tfiar_rd = mfspr(SPRN_TFIAR);
		if ( (tfhar != tfhar_rd) || (tfiar != tfiar_rd) ) {
			passed = 0;
			return;
		}
	}
	return;
}

void texasr(void *in)
{
	unsigned long i;
	uint64_t result = 0;

	for (i = 0; i < num_loops; i++) {
		asm __volatile__(
			"tbegin.;"
			"beq    3f ;"
			"tabort. 0 ;"
			"tend.;"

			/* Abort handler */
			"3: ;"
			::: "memory");

                /* Check the TEXASR */
                result = mfspr(SPRN_TEXASR);
		if ((result & TEXASR_FS) == 0) {
			passed = 0;
			return;
		}
	}
	return;
}

int test_tmspr()
{
	pthread_t	*thread;
	int	   	thread_num;
	unsigned long	i;

	SKIP_IF(!have_htm());

	/* To cause some context switching */
	thread_num = 10 * sysconf(_SC_NPROCESSORS_ONLN);

	thread = malloc(thread_num * sizeof(pthread_t));
	if (thread == NULL)
		return EXIT_FAILURE;

	/* Test TFIAR and TFHAR */
	for (i = 0; i < thread_num; i += 2) {
		if (pthread_create(&thread[i], NULL, (void *)tfiar_tfhar,
				   (void *)i))
			return EXIT_FAILURE;
	}
	/* Test TEXASR */
	for (i = 1; i < thread_num; i += 2) {
		if (pthread_create(&thread[i], NULL, (void *)texasr, (void *)i))
			return EXIT_FAILURE;
	}

	for (i = 0; i < thread_num; i++) {
		if (pthread_join(thread[i], NULL) != 0)
			return EXIT_FAILURE;
	}

	free(thread);

	if (passed)
		return 0;
	else
		return 1;
}

int main(int argc, char *argv[])
{
	if (argc > 1) {
		if (strcmp(argv[1], "-h") == 0) {
			printf("Syntax:\t [<num loops>]\n");
			return 0;
		} else {
			num_loops = atoi(argv[1]);
		}
	}
	return test_harness(test_tmspr, "tm_tmspr");
}
