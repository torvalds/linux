// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018, Michael Ellerman, IBM Corp.
 *
 * Test that an out-of-bounds branch to counter behaves as expected.
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

#include "utils.h"


#define BAD_NIP	0x788c545a18000000ull

static struct pt_regs signal_regs;
static jmp_buf setjmp_env;

static void save_regs(ucontext_t *ctxt)
{
	struct pt_regs *regs = ctxt->uc_mcontext.regs;

	memcpy(&signal_regs, regs, sizeof(signal_regs));
}

static void segv_handler(int signum, siginfo_t *info, void *ctxt_v)
{
	save_regs(ctxt_v);
	longjmp(setjmp_env, 1);
}

static void usr2_handler(int signum, siginfo_t *info, void *ctxt_v)
{
	save_regs(ctxt_v);
}

static int ok(void)
{
	printf("Everything is OK in here.\n");
	return 0;
}

#define REG_POISON	0x5a5aUL
#define POISONED_REG(n)	((REG_POISON << 48) | ((n) << 32) | (REG_POISON << 16) | (n))

static inline void poison_regs(void)
{
	#define POISON_REG(n)	\
	  "lis  " __stringify(n) "," __stringify(REG_POISON) ";" \
	  "addi " __stringify(n) "," __stringify(n) "," __stringify(n) ";" \
	  "sldi " __stringify(n) "," __stringify(n) ", 32 ;" \
	  "oris " __stringify(n) "," __stringify(n) "," __stringify(REG_POISON) ";" \
	  "addi " __stringify(n) "," __stringify(n) "," __stringify(n) ";"

	asm (POISON_REG(15)
	     POISON_REG(16)
	     POISON_REG(17)
	     POISON_REG(18)
	     POISON_REG(19)
	     POISON_REG(20)
	     POISON_REG(21)
	     POISON_REG(22)
	     POISON_REG(23)
	     POISON_REG(24)
	     POISON_REG(25)
	     POISON_REG(26)
	     POISON_REG(27)
	     POISON_REG(28)
	     POISON_REG(29)
	     : // inputs
	     : // outputs
	     : "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25",
	       "26", "27", "28", "29"
	);
	#undef POISON_REG
}

static int check_regs(void)
{
	unsigned long i;

	for (i = 15; i <= 29; i++)
		FAIL_IF(signal_regs.gpr[i] != POISONED_REG(i));

	printf("Regs OK\n");
	return 0;
}

static void dump_regs(void)
{
	for (int i = 0; i < 32; i += 4) {
		printf("r%02d 0x%016lx  r%02d 0x%016lx  " \
		       "r%02d 0x%016lx  r%02d 0x%016lx\n",
		       i, signal_regs.gpr[i],
		       i+1, signal_regs.gpr[i+1],
		       i+2, signal_regs.gpr[i+2],
		       i+3, signal_regs.gpr[i+3]);
	}
}

int test_wild_bctr(void)
{
	int (*func_ptr)(void);
	struct sigaction segv = {
		.sa_sigaction = segv_handler,
		.sa_flags = SA_SIGINFO
	};
	struct sigaction usr2 = {
		.sa_sigaction = usr2_handler,
		.sa_flags = SA_SIGINFO
	};

	FAIL_IF(sigaction(SIGSEGV, &segv, NULL));
	FAIL_IF(sigaction(SIGUSR2, &usr2, NULL));

	bzero(&signal_regs, sizeof(signal_regs));

	if (setjmp(setjmp_env) == 0) {
		func_ptr = ok;
		func_ptr();

		kill(getpid(), SIGUSR2);
		printf("Regs before:\n");
		dump_regs();
		bzero(&signal_regs, sizeof(signal_regs));

		poison_regs();

		func_ptr = (int (*)(void))BAD_NIP;
		func_ptr();

		FAIL_IF(1); /* we didn't segv? */
	}

	FAIL_IF(signal_regs.nip != BAD_NIP);

	printf("All good - took SEGV as expected branching to 0x%llx\n", BAD_NIP);

	dump_regs();
	FAIL_IF(check_regs());

	return 0;
}

int main(void)
{
	return test_harness(test_wild_bctr, "wild_bctr");
}
