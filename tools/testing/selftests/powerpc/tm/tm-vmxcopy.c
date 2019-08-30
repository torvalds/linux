// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015, Michael Neuling, IBM Corp.
 *
 * Original: Michael Neuling 4/12/2013
 * Edited: Rashmica Gupta 4/12/2015
 *
 * See if the altivec state is leaked out of an aborted transaction due to
 * kernel vmx copy loops.
 *
 * When the transaction aborts, VSR values should rollback to the values
 * they held before the transaction commenced. Using VSRs while transaction
 * is suspended should not affect the checkpointed values.
 *
 * (1) write A to a VSR
 * (2) start transaction
 * (3) suspend transaction
 * (4) change the VSR to B
 * (5) trigger kernel vmx copy loop
 * (6) abort transaction
 * (7) check that the VSR value is A
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <assert.h>

#include "tm.h"
#include "utils.h"

int test_vmxcopy()
{
	long double vecin = 1.3;
	long double vecout;
	unsigned long pgsize = getpagesize();
	int i;
	int fd;
	int size = pgsize*16;
	char tmpfile[] = "/tmp/page_faultXXXXXX";
	char buf[pgsize];
	char *a;
	uint64_t aborted = 0;

	SKIP_IF(!have_htm());
	SKIP_IF(!is_ppc64le());

	fd = mkstemp(tmpfile);
	assert(fd >= 0);

	memset(buf, 0, pgsize);
	for (i = 0; i < size; i += pgsize)
		assert(write(fd, buf, pgsize) == pgsize);

	unlink(tmpfile);

	a = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	assert(a != MAP_FAILED);

	asm __volatile__(
		"lxvd2x 40,0,%[vecinptr];"	/* set 40 to initial value*/
		"tbegin.;"
		"beq	3f;"
		"tsuspend.;"
		"xxlxor 40,40,40;"		/* set 40 to 0 */
		"std	5, 0(%[map]);"		/* cause kernel vmx copy page */
		"tabort. 0;"
		"tresume.;"
		"tend.;"
		"li	%[res], 0;"
		"b	5f;"

		/* Abort handler */
		"3:;"
		"li	%[res], 1;"

		"5:;"
		"stxvd2x 40,0,%[vecoutptr];"
		: [res]"=r"(aborted)
		: [vecinptr]"r"(&vecin),
		  [vecoutptr]"r"(&vecout),
		  [map]"r"(a)
		: "memory", "r0", "r3", "r4", "r5", "r6", "r7");

	if (aborted && (vecin != vecout)){
		printf("FAILED: vector state leaked on abort %f != %f\n",
		       (double)vecin, (double)vecout);
		return 1;
	}

	munmap(a, size);

	close(fd);

	return 0;
}

int main(void)
{
	return test_harness(test_vmxcopy, "tm_vmxcopy");
}
