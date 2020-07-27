// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020, Sandipan Das, IBM Corp.
 *
 * Test if the signal information reports the correct memory protection
 * key upon getting a key access violation fault for a page that was
 * attempted to be protected by two different keys from two competing
 * threads at the same time.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>

#include "pkeys.h"

#define PPC_INST_NOP	0x60000000
#define PPC_INST_BLR	0x4e800020
#define PROT_RWX	(PROT_READ | PROT_WRITE | PROT_EXEC)

#define NUM_ITERATIONS	1000000

static volatile sig_atomic_t perm_pkey, rest_pkey;
static volatile sig_atomic_t rights, fault_count;
static volatile unsigned int *volatile fault_addr;
static pthread_barrier_t iteration_barrier;

static void segv_handler(int signum, siginfo_t *sinfo, void *ctx)
{
	void *pgstart;
	size_t pgsize;
	int pkey;

	pkey = siginfo_pkey(sinfo);

	/* Check if this fault originated from a pkey access violation */
	if (sinfo->si_code != SEGV_PKUERR) {
		sigsafe_err("got a fault for an unexpected reason\n");
		_exit(1);
	}

	/* Check if this fault originated from the expected address */
	if (sinfo->si_addr != (void *) fault_addr) {
		sigsafe_err("got a fault for an unexpected address\n");
		_exit(1);
	}

	/* Check if this fault originated from the restrictive pkey */
	if (pkey != rest_pkey) {
		sigsafe_err("got a fault for an unexpected pkey\n");
		_exit(1);
	}

	/* Check if too many faults have occurred for the same iteration */
	if (fault_count > 0) {
		sigsafe_err("got too many faults for the same address\n");
		_exit(1);
	}

	pgsize = getpagesize();
	pgstart = (void *) ((unsigned long) fault_addr & ~(pgsize - 1));

	/*
	 * If the current fault occurred due to lack of execute rights,
	 * reassociate the page with the exec-only pkey since execute
	 * rights cannot be changed directly for the faulting pkey as
	 * IAMR is inaccessible from userspace.
	 *
	 * Otherwise, if the current fault occurred due to lack of
	 * read-write rights, change the AMR permission bits for the
	 * pkey.
	 *
	 * This will let the test continue.
	 */
	if (rights == PKEY_DISABLE_EXECUTE &&
	    mprotect(pgstart, pgsize, PROT_EXEC))
		_exit(1);
	else
		pkey_set_rights(pkey, 0);

	fault_count++;
}

struct region {
	unsigned long rights;
	unsigned int *base;
	size_t size;
};

static void *protect(void *p)
{
	unsigned long rights;
	unsigned int *base;
	size_t size;
	int tid, i;

	tid = gettid();
	base = ((struct region *) p)->base;
	size = ((struct region *) p)->size;
	FAIL_IF_EXIT(!base);

	/* No read, write and execute restrictions */
	rights = 0;

	printf("tid %d, pkey permissions are %s\n", tid, pkey_rights(rights));

	/* Allocate the permissive pkey */
	perm_pkey = sys_pkey_alloc(0, rights);
	FAIL_IF_EXIT(perm_pkey < 0);

	/*
	 * Repeatedly try to protect the common region with a permissive
	 * pkey
	 */
	for (i = 0; i < NUM_ITERATIONS; i++) {
		/*
		 * Wait until the other thread has finished allocating the
		 * restrictive pkey or until the next iteration has begun
		 */
		pthread_barrier_wait(&iteration_barrier);

		/* Try to associate the permissive pkey with the region */
		FAIL_IF_EXIT(sys_pkey_mprotect(base, size, PROT_RWX,
					       perm_pkey));
	}

	/* Free the permissive pkey */
	sys_pkey_free(perm_pkey);

	return NULL;
}

static void *protect_access(void *p)
{
	size_t size, numinsns;
	unsigned int *base;
	int tid, i;

	tid = gettid();
	base = ((struct region *) p)->base;
	size = ((struct region *) p)->size;
	rights = ((struct region *) p)->rights;
	numinsns = size / sizeof(base[0]);
	FAIL_IF_EXIT(!base);

	/* Allocate the restrictive pkey */
	rest_pkey = sys_pkey_alloc(0, rights);
	FAIL_IF_EXIT(rest_pkey < 0);

	printf("tid %d, pkey permissions are %s\n", tid, pkey_rights(rights));
	printf("tid %d, %s randomly in range [%p, %p]\n", tid,
	       (rights == PKEY_DISABLE_EXECUTE) ? "execute" :
	       (rights == PKEY_DISABLE_WRITE)  ? "write" : "read",
	       base, base + numinsns);

	/*
	 * Repeatedly try to protect the common region with a restrictive
	 * pkey and read, write or execute from it
	 */
	for (i = 0; i < NUM_ITERATIONS; i++) {
		/*
		 * Wait until the other thread has finished allocating the
		 * permissive pkey or until the next iteration has begun
		 */
		pthread_barrier_wait(&iteration_barrier);

		/* Try to associate the restrictive pkey with the region */
		FAIL_IF_EXIT(sys_pkey_mprotect(base, size, PROT_RWX,
					       rest_pkey));

		/* Choose a random instruction word address from the region */
		fault_addr = base + (rand() % numinsns);
		fault_count = 0;

		switch (rights) {
		/* Read protection test */
		case PKEY_DISABLE_ACCESS:
			/*
			 * Read an instruction word from the region and
			 * verify if it has not been overwritten to
			 * something unexpected
			 */
			FAIL_IF_EXIT(*fault_addr != PPC_INST_NOP &&
				     *fault_addr != PPC_INST_BLR);
			break;

		/* Write protection test */
		case PKEY_DISABLE_WRITE:
			/*
			 * Write an instruction word to the region and
			 * verify if the overwrite has succeeded
			 */
			*fault_addr = PPC_INST_BLR;
			FAIL_IF_EXIT(*fault_addr != PPC_INST_BLR);
			break;

		/* Execute protection test */
		case PKEY_DISABLE_EXECUTE:
			/* Jump to the region and execute instructions */
			asm volatile(
				"mtctr	%0; bctrl"
				: : "r"(fault_addr) : "ctr", "lr");
			break;
		}

		/*
		 * Restore the restrictions originally imposed by the
		 * restrictive pkey as the signal handler would have
		 * cleared out the corresponding AMR bits
		 */
		pkey_set_rights(rest_pkey, rights);
	}

	/* Free restrictive pkey */
	sys_pkey_free(rest_pkey);

	return NULL;
}

static void reset_pkeys(unsigned long rights)
{
	int pkeys[NR_PKEYS], i;

	/* Exhaustively allocate all available pkeys */
	for (i = 0; i < NR_PKEYS; i++)
		pkeys[i] = sys_pkey_alloc(0, rights);

	/* Free all allocated pkeys */
	for (i = 0; i < NR_PKEYS; i++)
		sys_pkey_free(pkeys[i]);
}

static int test(void)
{
	pthread_t prot_thread, pacc_thread;
	struct sigaction act;
	pthread_attr_t attr;
	size_t numinsns;
	struct region r;
	int ret, i;

	srand(time(NULL));
	ret = pkeys_unsupported();
	if (ret)
		return ret;

	/* Allocate the region */
	r.size = getpagesize();
	r.base = mmap(NULL, r.size, PROT_RWX,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	FAIL_IF(r.base == MAP_FAILED);

	/*
	 * Fill the region with no-ops with a branch at the end
	 * for returning to the caller
	 */
	numinsns = r.size / sizeof(r.base[0]);
	for (i = 0; i < numinsns - 1; i++)
		r.base[i] = PPC_INST_NOP;
	r.base[i] = PPC_INST_BLR;

	/* Setup SIGSEGV handler */
	act.sa_handler = 0;
	act.sa_sigaction = segv_handler;
	FAIL_IF(sigprocmask(SIG_SETMASK, 0, &act.sa_mask) != 0);
	act.sa_flags = SA_SIGINFO;
	act.sa_restorer = 0;
	FAIL_IF(sigaction(SIGSEGV, &act, NULL) != 0);

	/*
	 * For these tests, the parent process should clear all bits of
	 * AMR and IAMR, i.e. impose no restrictions, for all available
	 * pkeys. This will be the base for the initial AMR and IAMR
	 * values for all the test thread pairs.
	 *
	 * If the AMR and IAMR bits of all available pkeys are cleared
	 * before running the tests and a fault is generated when
	 * attempting to read, write or execute instructions from a
	 * pkey protected region, the pkey responsible for this must be
	 * the one from the protect-and-access thread since the other
	 * one is fully permissive. Despite that, if the pkey reported
	 * by siginfo is not the restrictive pkey, then there must be a
	 * kernel bug.
	 */
	reset_pkeys(0);

	/* Setup barrier for protect and protect-and-access threads */
	FAIL_IF(pthread_attr_init(&attr) != 0);
	FAIL_IF(pthread_barrier_init(&iteration_barrier, NULL, 2) != 0);

	/* Setup and start protect and protect-and-read threads */
	puts("starting thread pair (protect, protect-and-read)");
	r.rights = PKEY_DISABLE_ACCESS;
	FAIL_IF(pthread_create(&prot_thread, &attr, &protect, &r) != 0);
	FAIL_IF(pthread_create(&pacc_thread, &attr, &protect_access, &r) != 0);
	FAIL_IF(pthread_join(prot_thread, NULL) != 0);
	FAIL_IF(pthread_join(pacc_thread, NULL) != 0);

	/* Setup and start protect and protect-and-write threads */
	puts("starting thread pair (protect, protect-and-write)");
	r.rights = PKEY_DISABLE_WRITE;
	FAIL_IF(pthread_create(&prot_thread, &attr, &protect, &r) != 0);
	FAIL_IF(pthread_create(&pacc_thread, &attr, &protect_access, &r) != 0);
	FAIL_IF(pthread_join(prot_thread, NULL) != 0);
	FAIL_IF(pthread_join(pacc_thread, NULL) != 0);

	/* Setup and start protect and protect-and-execute threads */
	puts("starting thread pair (protect, protect-and-execute)");
	r.rights = PKEY_DISABLE_EXECUTE;
	FAIL_IF(pthread_create(&prot_thread, &attr, &protect, &r) != 0);
	FAIL_IF(pthread_create(&pacc_thread, &attr, &protect_access, &r) != 0);
	FAIL_IF(pthread_join(prot_thread, NULL) != 0);
	FAIL_IF(pthread_join(pacc_thread, NULL) != 0);

	/* Cleanup */
	FAIL_IF(pthread_attr_destroy(&attr) != 0);
	FAIL_IF(pthread_barrier_destroy(&iteration_barrier) != 0);
	munmap(r.base, r.size);

	return 0;
}

int main(void)
{
	test_harness(test, "pkey_siginfo");
}
