// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2019, Gustavo Romero, Michael Neuling, IBM Corp.
 *
 * This test will spawn two processes. Both will be attached to the same
 * CPU (CPU 0). The child will be in a loop writing to FP register f31 and
 * VMX/VEC/Altivec register vr31 a known value, called poison, calling
 * sched_yield syscall after to allow the parent to switch on the CPU.
 * Parent will set f31 and vr31 to 1 and in a loop will check if f31 and
 * vr31 remain 1 as expected until a given timeout (2m). If the issue is
 * present child's poison will leak into parent's f31 or vr31 registers,
 * otherwise, poison will never leak into parent's f31 and vr31 registers.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sched.h>
#include <sys/types.h>
#include <signal.h>

#include "tm.h"

int tm_poison_test(void)
{
	int cpu, pid;
	cpu_set_t cpuset;
	uint64_t poison = 0xdeadbeefc0dec0fe;
	uint64_t unknown = 0;
	bool fail_fp = false;
	bool fail_vr = false;

	SKIP_IF(!have_htm());
	SKIP_IF(htm_is_synthetic());

	cpu = pick_online_cpu();
	FAIL_IF(cpu < 0);

	// Attach both Child and Parent to the same CPU
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	FAIL_IF(sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0);

	pid = fork();
	if (!pid) {
		/**
		 * child
		 */
		while (1) {
			sched_yield();
			asm (
				"mtvsrd 31, %[poison];" // f31 = poison
				"mtvsrd 63, %[poison];" // vr31 = poison

				: : [poison] "r" (poison) : );
		}
	}

	/**
	 * parent
	 */
	asm (
		/*
		 * Set r3, r4, and f31 to known value 1 before entering
		 * in transaction. They won't be written after that.
		 */
		"       li      3, 0x1          ;"
		"       li      4, 0x1          ;"
		"       mtvsrd  31, 4           ;"

		/*
		 * The Time Base (TB) is a 64-bit counter register that is
		 * independent of the CPU clock and which is incremented
		 * at a frequency of 512000000 Hz, so every 1.953125ns.
		 * So it's necessary 120s/0.000000001953125s = 61440000000
		 * increments to get a 2 minutes timeout. Below we set that
		 * value in r5 and then use r6 to track initial TB value,
		 * updating TB values in r7 at every iteration and comparing it
		 * to r6. When r7 (current) - r6 (initial) > 61440000000 we bail
		 * out since for sure we spent already 2 minutes in the loop.
		 * SPR 268 is the TB register.
		 */
		"       lis     5, 14           ;"
		"       ori     5, 5, 19996     ;"
		"       sldi    5, 5, 16        ;" // r5 = 61440000000

		"       mfspr   6, 268          ;" // r6 (TB initial)
		"1:     mfspr   7, 268          ;" // r7 (TB current)
		"       subf    7, 6, 7         ;" // r7 - r6 > 61440000000 ?
		"       cmpd    7, 5            ;"
		"       bgt     3f              ;" // yes, exit

		/*
		 * Main loop to check f31
		 */
		"       tbegin.                 ;" // no, try again
		"       beq     1b              ;" // restart if no timeout
		"       mfvsrd  3, 31           ;" // read f31
		"       cmpd    3, 4            ;" // f31 == 1 ?
		"       bne     2f              ;" // broken :-(
		"       tabort. 3               ;" // try another transaction
		"2:     tend.                   ;" // commit transaction
		"3:     mr    %[unknown], 3     ;" // record r3

		: [unknown] "=r" (unknown)
		:
		: "cr0", "r3", "r4", "r5", "r6", "r7", "vs31"

		);

	/*
	 * On leak 'unknown' will contain 'poison' value from child,
	 * otherwise (no leak) 'unknown' will contain the same value
	 * as r3 before entering in transactional mode, i.e. 0x1.
	 */
	fail_fp = unknown != 0x1;
	if (fail_fp)
		printf("Unknown value %#"PRIx64" leaked into f31!\n", unknown);
	else
		printf("Good, no poison or leaked value into FP registers\n");

	asm (
		/*
		 * Set r3, r4, and vr31 to known value 1 before entering
		 * in transaction. They won't be written after that.
		 */
		"       li      3, 0x1          ;"
		"       li      4, 0x1          ;"
		"       mtvsrd  63, 4           ;"

		"       lis     5, 14           ;"
		"       ori     5, 5, 19996     ;"
		"       sldi    5, 5, 16        ;" // r5 = 61440000000

		"       mfspr   6, 268          ;" // r6 (TB initial)
		"1:     mfspr   7, 268          ;" // r7 (TB current)
		"       subf    7, 6, 7         ;" // r7 - r6 > 61440000000 ?
		"       cmpd    7, 5            ;"
		"       bgt     3f              ;" // yes, exit

		/*
		 * Main loop to check vr31
		 */
		"       tbegin.                 ;" // no, try again
		"       beq     1b              ;" // restart if no timeout
		"       mfvsrd  3, 63           ;" // read vr31
		"       cmpd    3, 4            ;" // vr31 == 1 ?
		"       bne     2f              ;" // broken :-(
		"       tabort. 3               ;" // try another transaction
		"2:     tend.                   ;" // commit transaction
		"3:     mr    %[unknown], 3     ;" // record r3

		: [unknown] "=r" (unknown)
		:
		: "cr0", "r3", "r4", "r5", "r6", "r7", "vs63"

		);

	/*
	 * On leak 'unknown' will contain 'poison' value from child,
	 * otherwise (no leak) 'unknown' will contain the same value
	 * as r3 before entering in transactional mode, i.e. 0x1.
	 */
	fail_vr = unknown != 0x1;
	if (fail_vr)
		printf("Unknown value %#"PRIx64" leaked into vr31!\n", unknown);
	else
		printf("Good, no poison or leaked value into VEC registers\n");

	kill(pid, SIGKILL);

	return (fail_fp | fail_vr);
}

int main(int argc, char *argv[])
{
	/* Test completes in about 4m */
	test_harness_set_timeout(250);
	return test_harness(tm_poison_test, "tm_poison_test");
}
