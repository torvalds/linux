// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015, Michael Neuling, IBM Corp.
 * Original: Michael Neuling 19/7/2013
 * Edited: Rashmica Gupta 01/12/2015
 *
 * Do some transactions, see if the tar is corrupted.
 * If the transaction is aborted, the TAR should be rolled back to the
 * checkpointed value before the transaction began. The value written to
 * TAR in suspended mode should only remain in TAR if the transaction
 * completes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "tm.h"
#include "utils.h"

int	num_loops	= 10000;

int test_tar(void)
{
	int i;

	SKIP_IF(!have_htm());
	SKIP_IF(htm_is_synthetic());
	SKIP_IF(!is_ppc64le());

	for (i = 0; i < num_loops; i++)
	{
		uint64_t result = 0;
		asm __volatile__(
			"li	7, 1;"
			"mtspr	%[tar], 7;"	/* tar = 1 */
			"tbegin.;"
			"beq	3f;"
			"li	4, 0x7000;"	/* Loop lots, to use time */
			"2:;"			/* Start loop */
			"li	7, 2;"
			"mtspr	%[tar], 7;"	/* tar = 2 */
			"tsuspend.;"
			"li	7, 3;"
			"mtspr	%[tar], 7;"	/* tar = 3 */
			"tresume.;"
			"subi	4, 4, 1;"
			"cmpdi	4, 0;"
			"bne	2b;"
			"tend.;"

			/* Transaction sucess! TAR should be 3 */
			"mfspr  7, %[tar];"
			"ori	%[res], 7, 4;"  // res = 3|4 = 7
			"b	4f;"

			/* Abort handler. TAR should be rolled back to 1 */
			"3:;"
			"mfspr  7, %[tar];"
			"ori	%[res], 7, 8;"	// res = 1|8 = 9
			"4:;"

			: [res]"=r"(result)
			: [tar]"i"(SPRN_TAR)
			   : "memory", "r0", "r4", "r7");

		/* If result is anything else other than 7 or 9, the tar
		 * value must have been corrupted. */
		if ((result != 7) && (result != 9))
			return 1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	/* A low number of iterations (eg 100) can cause a false pass */
	if (argc > 1) {
		if (strcmp(argv[1], "-h") == 0) {
			printf("Syntax:\n\t%s [<num loops>]\n",
			       argv[0]);
			return 1;
		} else {
			num_loops = atoi(argv[1]);
		}
	}

	printf("Starting, %d loops\n", num_loops);

	return test_harness(test_tar, "tm_tar");
}
