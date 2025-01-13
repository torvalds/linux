// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2020, Sandipan Das, IBM Corp.
 *
 * Test if applying execute protection on pages using memory
 * protection keys works as expected.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <unistd.h>

#include "pkeys.h"

#define PPC_INST_NOP	0x60000000
#define PPC_INST_TRAP	0x7fe00008
#define PPC_INST_BLR	0x4e800020

static volatile sig_atomic_t fault_pkey, fault_code, fault_type;
static volatile sig_atomic_t remaining_faults;
static volatile unsigned int *fault_addr;
static unsigned long pgsize, numinsns;
static unsigned int *insns;

static void trap_handler(int signum, siginfo_t *sinfo, void *ctx)
{
	/* Check if this fault originated from the expected address */
	if (sinfo->si_addr != (void *) fault_addr)
		sigsafe_err("got a fault for an unexpected address\n");

	_exit(1);
}

static void segv_handler(int signum, siginfo_t *sinfo, void *ctx)
{
	int signal_pkey;

	signal_pkey = siginfo_pkey(sinfo);
	fault_code = sinfo->si_code;

	/* Check if this fault originated from the expected address */
	if (sinfo->si_addr != (void *) fault_addr) {
		sigsafe_err("got a fault for an unexpected address\n");
		_exit(1);
	}

	/* Check if too many faults have occurred for a single test case */
	if (!remaining_faults) {
		sigsafe_err("got too many faults for the same address\n");
		_exit(1);
	}


	/* Restore permissions in order to continue */
	switch (fault_code) {
	case SEGV_ACCERR:
		if (mprotect(insns, pgsize, PROT_READ | PROT_WRITE)) {
			sigsafe_err("failed to set access permissions\n");
			_exit(1);
		}
		break;
	case SEGV_PKUERR:
		if (signal_pkey != fault_pkey) {
			sigsafe_err("got a fault for an unexpected pkey\n");
			_exit(1);
		}

		switch (fault_type) {
		case PKEY_DISABLE_ACCESS:
			pkey_set_rights(fault_pkey, PKEY_UNRESTRICTED);
			break;
		case PKEY_DISABLE_EXECUTE:
			/*
			 * Reassociate the exec-only pkey with the region
			 * to be able to continue. Unlike AMR, we cannot
			 * set IAMR directly from userspace to restore the
			 * permissions.
			 */
			if (mprotect(insns, pgsize, PROT_EXEC)) {
				sigsafe_err("failed to set execute permissions\n");
				_exit(1);
			}
			break;
		default:
			sigsafe_err("got a fault with an unexpected type\n");
			_exit(1);
		}
		break;
	default:
		sigsafe_err("got a fault with an unexpected code\n");
		_exit(1);
	}

	remaining_faults--;
}

static int test(void)
{
	struct sigaction segv_act, trap_act;
	unsigned long rights;
	int pkey, ret, i;

	ret = pkeys_unsupported();
	if (ret)
		return ret;

	/* Setup SIGSEGV handler */
	segv_act.sa_handler = 0;
	segv_act.sa_sigaction = segv_handler;
	FAIL_IF(sigprocmask(SIG_SETMASK, 0, &segv_act.sa_mask) != 0);
	segv_act.sa_flags = SA_SIGINFO;
	segv_act.sa_restorer = 0;
	FAIL_IF(sigaction(SIGSEGV, &segv_act, NULL) != 0);

	/* Setup SIGTRAP handler */
	trap_act.sa_handler = 0;
	trap_act.sa_sigaction = trap_handler;
	FAIL_IF(sigprocmask(SIG_SETMASK, 0, &trap_act.sa_mask) != 0);
	trap_act.sa_flags = SA_SIGINFO;
	trap_act.sa_restorer = 0;
	FAIL_IF(sigaction(SIGTRAP, &trap_act, NULL) != 0);

	/* Setup executable region */
	pgsize = getpagesize();
	numinsns = pgsize / sizeof(unsigned int);
	insns = (unsigned int *) mmap(NULL, pgsize, PROT_READ | PROT_WRITE,
				      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	FAIL_IF(insns == MAP_FAILED);

	/* Write the instruction words */
	for (i = 1; i < numinsns - 1; i++)
		insns[i] = PPC_INST_NOP;

	/*
	 * Set the first instruction as an unconditional trap. If
	 * the last write to this address succeeds, this should
	 * get overwritten by a no-op.
	 */
	insns[0] = PPC_INST_TRAP;

	/*
	 * Later, to jump to the executable region, we use a branch
	 * and link instruction (bctrl) which sets the return address
	 * automatically in LR. Use that to return back.
	 */
	insns[numinsns - 1] = PPC_INST_BLR;

	/* Allocate a pkey that restricts execution */
	rights = PKEY_DISABLE_EXECUTE;
	pkey = sys_pkey_alloc(0, rights);
	FAIL_IF(pkey < 0);

	/*
	 * Pick the first instruction's address from the executable
	 * region.
	 */
	fault_addr = insns;

	/* The following two cases will avoid SEGV_PKUERR */
	fault_type = -1;
	fault_pkey = -1;

	/*
	 * Read an instruction word from the address when AMR bits
	 * are not set i.e. the pkey permits both read and write
	 * access.
	 *
	 * This should not generate a fault as having PROT_EXEC
	 * implies PROT_READ on GNU systems. The pkey currently
	 * restricts execution only based on the IAMR bits. The
	 * AMR bits are cleared.
	 */
	remaining_faults = 0;
	FAIL_IF(sys_pkey_mprotect(insns, pgsize, PROT_EXEC, pkey) != 0);
	printf("read from %p, pkey permissions are %s\n", fault_addr,
	       pkey_rights(rights));
	i = *fault_addr;
	FAIL_IF(remaining_faults != 0);

	/*
	 * Write an instruction word to the address when AMR bits
	 * are not set i.e. the pkey permits both read and write
	 * access.
	 *
	 * This should generate an access fault as having just
	 * PROT_EXEC also restricts writes. The pkey currently
	 * restricts execution only based on the IAMR bits. The
	 * AMR bits are cleared.
	 */
	remaining_faults = 1;
	FAIL_IF(sys_pkey_mprotect(insns, pgsize, PROT_EXEC, pkey) != 0);
	printf("write to %p, pkey permissions are %s\n", fault_addr,
	       pkey_rights(rights));
	*fault_addr = PPC_INST_TRAP;
	FAIL_IF(remaining_faults != 0 || fault_code != SEGV_ACCERR);

	/* The following three cases will generate SEGV_PKUERR */
	rights |= PKEY_DISABLE_ACCESS;
	fault_type = PKEY_DISABLE_ACCESS;
	fault_pkey = pkey;

	/*
	 * Read an instruction word from the address when AMR bits
	 * are set i.e. the pkey permits neither read nor write
	 * access.
	 *
	 * This should generate a pkey fault based on AMR bits only
	 * as having PROT_EXEC implicitly allows reads.
	 */
	remaining_faults = 1;
	FAIL_IF(sys_pkey_mprotect(insns, pgsize, PROT_EXEC, pkey) != 0);
	pkey_set_rights(pkey, rights);
	printf("read from %p, pkey permissions are %s\n", fault_addr,
	       pkey_rights(rights));
	i = *fault_addr;
	FAIL_IF(remaining_faults != 0 || fault_code != SEGV_PKUERR);

	/*
	 * Write an instruction word to the address when AMR bits
	 * are set i.e. the pkey permits neither read nor write
	 * access.
	 *
	 * This should generate two faults. First, a pkey fault
	 * based on AMR bits and then an access fault since
	 * PROT_EXEC does not allow writes.
	 */
	remaining_faults = 2;
	FAIL_IF(sys_pkey_mprotect(insns, pgsize, PROT_EXEC, pkey) != 0);
	pkey_set_rights(pkey, rights);
	printf("write to %p, pkey permissions are %s\n", fault_addr,
	       pkey_rights(rights));
	*fault_addr = PPC_INST_NOP;
	FAIL_IF(remaining_faults != 0 || fault_code != SEGV_ACCERR);

	/* Free the current pkey */
	sys_pkey_free(pkey);

	rights = 0;
	do {
		/*
		 * Allocate pkeys with all valid combinations of read,
		 * write and execute restrictions.
		 */
		pkey = sys_pkey_alloc(0, rights);
		FAIL_IF(pkey < 0);

		/*
		 * Jump to the executable region. AMR bits may or may not
		 * be set but they should not affect execution.
		 *
		 * This should generate pkey faults based on IAMR bits which
		 * may be set to restrict execution.
		 *
		 * The first iteration also checks if the overwrite of the
		 * first instruction word from a trap to a no-op succeeded.
		 */
		fault_pkey = pkey;
		fault_type = -1;
		remaining_faults = 0;
		if (rights & PKEY_DISABLE_EXECUTE) {
			fault_type = PKEY_DISABLE_EXECUTE;
			remaining_faults = 1;
		}

		FAIL_IF(sys_pkey_mprotect(insns, pgsize, PROT_EXEC, pkey) != 0);
		printf("execute at %p, pkey permissions are %s\n", fault_addr,
		       pkey_rights(rights));
		asm volatile("mtctr	%0; bctrl" : : "r"(insns));
		FAIL_IF(remaining_faults != 0);
		if (rights & PKEY_DISABLE_EXECUTE)
			FAIL_IF(fault_code != SEGV_PKUERR);

		/* Free the current pkey */
		sys_pkey_free(pkey);

		/* Find next valid combination of pkey rights */
		rights = next_pkey_rights(rights);
	} while (rights);

	/* Cleanup */
	munmap((void *) insns, pgsize);

	return 0;
}

int main(void)
{
	return test_harness(test, "pkey_exec_prot");
}
