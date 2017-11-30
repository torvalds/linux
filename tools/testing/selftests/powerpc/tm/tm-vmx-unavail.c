/*
 * Copyright 2017, Michael Neuling, IBM Corp.
 * Licensed under GPLv2.
 * Original: Breno Leitao <brenohl@br.ibm.com> &
 *           Gustavo Bueno Romero <gromero@br.ibm.com>
 * Edited: Michael Neuling
 *
 * Force VMX unavailable during a transaction and see if it corrupts
 * the checkpointed VMX register state after the abort.
 */

#include <inttypes.h>
#include <htmintrin.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>

#include "tm.h"
#include "utils.h"

int passed;

void *worker(void *unused)
{
	__int128 vmx0;
	uint64_t texasr;

	asm goto (
		"li       3, 1;"  /* Stick non-zero value in VMX0 */
		"std      3, 0(%[vmx0_ptr]);"
		"lvx      0, 0, %[vmx0_ptr];"

		/* Wait here a bit so we get scheduled out 255 times */
		"lis      3, 0x3fff;"
		"1: ;"
		"addi     3, 3, -1;"
		"cmpdi    3, 0;"
		"bne      1b;"

		/* Kernel will hopefully turn VMX off now */

		"tbegin. ;"
		"beq      failure;"

		/* Cause VMX unavail. Any VMX instruction */
		"vaddcuw  0,0,0;"

		"tend. ;"
		"b        %l[success];"

		/* Check VMX0 sanity after abort */
		"failure: ;"
		"lvx       1,  0, %[vmx0_ptr];"
		"vcmpequb. 2,  0, 1;"
		"bc        4, 24, %l[value_mismatch];"
		"b        %l[value_match];"
		:
		: [vmx0_ptr] "r"(&vmx0)
		: "r3"
		: success, value_match, value_mismatch
		);

	/* HTM aborted and VMX0 is corrupted */
value_mismatch:
	texasr = __builtin_get_texasr();

	printf("\n\n==============\n\n");
	printf("Failure with error: %lx\n",   _TEXASR_FAILURE_CODE(texasr));
	printf("Summary error     : %lx\n",   _TEXASR_FAILURE_SUMMARY(texasr));
	printf("TFIAR exact       : %lx\n\n", _TEXASR_TFIAR_EXACT(texasr));

	passed = 0;
	return NULL;

	/* HTM aborted but VMX0 is correct */
value_match:
//	printf("!");
	return NULL;

success:
//	printf(".");
	return NULL;
}

int tm_vmx_unavail_test()
{
	int threads;
	pthread_t *thread;

	SKIP_IF(!have_htm());

	passed = 1;

	threads = sysconf(_SC_NPROCESSORS_ONLN) * 4;
	thread = malloc(sizeof(pthread_t)*threads);
	if (!thread)
		return EXIT_FAILURE;

	for (uint64_t i = 0; i < threads; i++)
		pthread_create(&thread[i], NULL, &worker, NULL);

	for (uint64_t i = 0; i < threads; i++)
		pthread_join(thread[i], NULL);

	free(thread);

	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}


int main(int argc, char **argv)
{
	return test_harness(tm_vmx_unavail_test, "tm_vmx_unavail_test");
}
