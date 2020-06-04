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
#include <sys/mman.h>

#include "reg.h"
#include "utils.h"

/*
 * Older versions of libc use the Intel-specific access rights.
 * Hence, override the definitions as they might be incorrect.
 */
#undef PKEY_DISABLE_ACCESS
#define PKEY_DISABLE_ACCESS	0x3

#undef PKEY_DISABLE_WRITE
#define PKEY_DISABLE_WRITE	0x2

#undef PKEY_DISABLE_EXECUTE
#define PKEY_DISABLE_EXECUTE	0x4

/* Older versions of libc do not not define this */
#ifndef SEGV_PKUERR
#define SEGV_PKUERR	4
#endif

#define SI_PKEY_OFFSET	0x20

#define SYS_pkey_mprotect	386
#define SYS_pkey_alloc		384
#define SYS_pkey_free		385

#define PKEY_BITS_PER_PKEY	2
#define NR_PKEYS		32
#define PKEY_BITS_MASK		((1UL << PKEY_BITS_PER_PKEY) - 1)

#define PPC_INST_NOP	0x60000000
#define PPC_INST_TRAP	0x7fe00008
#define PPC_INST_BLR	0x4e800020

#define sigsafe_err(msg)	({ \
		ssize_t nbytes __attribute__((unused)); \
		nbytes = write(STDERR_FILENO, msg, strlen(msg)); })

static inline unsigned long pkeyreg_get(void)
{
	return mfspr(SPRN_AMR);
}

static inline void pkeyreg_set(unsigned long amr)
{
	set_amr(amr);
}

static void pkey_set_rights(int pkey, unsigned long rights)
{
	unsigned long amr, shift;

	shift = (NR_PKEYS - pkey - 1) * PKEY_BITS_PER_PKEY;
	amr = pkeyreg_get();
	amr &= ~(PKEY_BITS_MASK << shift);
	amr |= (rights & PKEY_BITS_MASK) << shift;
	pkeyreg_set(amr);
}

static int sys_pkey_mprotect(void *addr, size_t len, int prot, int pkey)
{
	return syscall(SYS_pkey_mprotect, addr, len, prot, pkey);
}

static int sys_pkey_alloc(unsigned long flags, unsigned long rights)
{
	return syscall(SYS_pkey_alloc, flags, rights);
}

static int sys_pkey_free(int pkey)
{
	return syscall(SYS_pkey_free, pkey);
}

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

	/*
	 * In older versions of libc, siginfo_t does not have si_pkey as
	 * a member.
	 */
#ifdef si_pkey
	signal_pkey = sinfo->si_pkey;
#else
	signal_pkey = *((int *)(((char *) sinfo) + SI_PKEY_OFFSET));
#endif

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
			pkey_set_rights(fault_pkey, 0);
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

static int pkeys_unsupported(void)
{
	bool hash_mmu = false;
	int pkey;

	/* Protection keys are currently supported on Hash MMU only */
	FAIL_IF(using_hash_mmu(&hash_mmu));
	SKIP_IF(!hash_mmu);

	/* Check if the system call is supported */
	pkey = sys_pkey_alloc(0, 0);
	SKIP_IF(pkey < 0);
	sys_pkey_free(pkey);

	return 0;
}

static int test(void)
{
	struct sigaction segv_act, trap_act;
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
	pkey = sys_pkey_alloc(0, PKEY_DISABLE_EXECUTE);
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
	printf("read from %p, pkey is execute-disabled, access-enabled\n",
	       (void *) fault_addr);
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
	printf("write to %p, pkey is execute-disabled, access-enabled\n",
	       (void *) fault_addr);
	*fault_addr = PPC_INST_TRAP;
	FAIL_IF(remaining_faults != 0 || fault_code != SEGV_ACCERR);

	/* The following three cases will generate SEGV_PKUERR */
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
	printf("read from %p, pkey is execute-disabled, access-disabled\n",
	       (void *) fault_addr);
	pkey_set_rights(pkey, PKEY_DISABLE_ACCESS);
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
	printf("write to %p, pkey is execute-disabled, access-disabled\n",
	       (void *) fault_addr);
	pkey_set_rights(pkey, PKEY_DISABLE_ACCESS);
	*fault_addr = PPC_INST_NOP;
	FAIL_IF(remaining_faults != 0 || fault_code != SEGV_ACCERR);

	/*
	 * Jump to the executable region when AMR bits are set i.e.
	 * the pkey permits neither read nor write access.
	 *
	 * This should generate a pkey fault based on IAMR bits which
	 * are set to not permit execution. AMR bits should not affect
	 * execution.
	 *
	 * This also checks if the overwrite of the first instruction
	 * word from a trap to a no-op succeeded.
	 */
	fault_addr = insns;
	fault_type = PKEY_DISABLE_EXECUTE;
	fault_pkey = pkey;
	remaining_faults = 1;
	FAIL_IF(sys_pkey_mprotect(insns, pgsize, PROT_EXEC, pkey) != 0);
	pkey_set_rights(pkey, PKEY_DISABLE_ACCESS);
	printf("execute at %p, pkey is execute-disabled, access-disabled\n",
	       (void *) fault_addr);
	asm volatile("mtctr	%0; bctrl" : : "r"(insns));
	FAIL_IF(remaining_faults != 0 || fault_code != SEGV_PKUERR);

	/*
	 * Free the current pkey and allocate a new one that is
	 * fully permissive.
	 */
	sys_pkey_free(pkey);
	pkey = sys_pkey_alloc(0, 0);

	/*
	 * Jump to the executable region when AMR bits are not set
	 * i.e. the pkey permits read and write access.
	 *
	 * This should not generate any faults as the IAMR bits are
	 * also not set and hence will the pkey will not restrict
	 * execution.
	 */
	fault_pkey = pkey;
	remaining_faults = 0;
	FAIL_IF(sys_pkey_mprotect(insns, pgsize, PROT_EXEC, pkey) != 0);
	printf("execute at %p, pkey is execute-enabled, access-enabled\n",
	       (void *) fault_addr);
	asm volatile("mtctr	%0; bctrl" : : "r"(insns));
	FAIL_IF(remaining_faults != 0);

	/* Cleanup */
	munmap((void *) insns, pgsize);
	sys_pkey_free(pkey);

	return 0;
}

int main(void)
{
	test_harness(test, "pkey_exec_prot");
}
