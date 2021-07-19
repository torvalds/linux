// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2017, Gustavo Romero, IBM Corp.
 *
 * Check if thread endianness is flipped inadvertently to BE on trap
 * caught in TM whilst MSR.FP and MSR.VEC are zero (i.e. just after
 * load_fp and load_vec overflowed).
 *
 * The issue can be checked on LE machines simply by zeroing load_fp
 * and load_vec and then causing a trap in TM. Since the endianness
 * changes to BE on return from the signal handler, 'nop' is
 * thread as an illegal instruction in following sequence:
 *	tbegin.
 *	beq 1f
 *	trap
 *	tend.
 * 1:	nop
 *
 * However, although the issue is also present on BE machines, it's a
 * bit trickier to check it on BE machines because MSR.LE bit is set
 * to zero which determines a BE endianness that is the native
 * endianness on BE machines, so nothing notably critical happens,
 * i.e. no illegal instruction is observed immediately after returning
 * from the signal handler (as it happens on LE machines). Thus to test
 * it on BE machines LE endianness is forced after a first trap and then
 * the endianness is verified on subsequent traps to determine if the
 * endianness "flipped back" to the native endianness (BE).
 */

#define _GNU_SOURCE
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <htmintrin.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>

#include "tm.h"
#include "utils.h"

#define pr_error(error_code, format, ...) \
	error_at_line(1, error_code, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define MSR_LE 1UL
#define LE     1UL

pthread_t t0_ping;
pthread_t t1_pong;

int exit_from_pong;

int trap_event;
int le;

bool success;

void trap_signal_handler(int signo, siginfo_t *si, void *uc)
{
	ucontext_t *ucp = uc;
	uint64_t thread_endianness;

	/* Get thread endianness: extract bit LE from MSR */
	thread_endianness = MSR_LE & ucp->uc_mcontext.gp_regs[PT_MSR];

	/*
	 * Little-Endian Machine
	 */

	if (le) {
		/* First trap event */
		if (trap_event == 0) {
			/* Do nothing. Since it is returning from this trap
			 * event that endianness is flipped by the bug, so just
			 * let the process return from the signal handler and
			 * check on the second trap event if endianness is
			 * flipped or not.
			 */
		}
		/* Second trap event */
		else if (trap_event == 1) {
			/*
			 * Since trap was caught in TM on first trap event, if
			 * endianness was still LE (not flipped inadvertently)
			 * after returning from the signal handler instruction
			 * (1) is executed (basically a 'nop'), as it's located
			 * at address of tbegin. +4 (rollback addr). As (1) on
			 * LE endianness does in effect nothing, instruction (2)
			 * is then executed again as 'trap', generating a second
			 * trap event (note that in that case 'trap' is caught
			 * not in transacional mode). On te other hand, if after
			 * the return from the signal handler the endianness in-
			 * advertently flipped, instruction (1) is tread as a
			 * branch instruction, i.e. b .+8, hence instruction (3)
			 * and (4) are executed (tbegin.; trap;) and we get sim-
			 * ilaly on the trap signal handler, but now in TM mode.
			 * Either way, it's now possible to check the MSR LE bit
			 * once in the trap handler to verify if endianness was
			 * flipped or not after the return from the second trap
			 * event. If endianness is flipped, the bug is present.
			 * Finally, getting a trap in TM mode or not is just
			 * worth noting because it affects the math to determine
			 * the offset added to the NIP on return: the NIP for a
			 * trap caught in TM is the rollback address, i.e. the
			 * next instruction after 'tbegin.', whilst the NIP for
			 * a trap caught in non-transactional mode is the very
			 * same address of the 'trap' instruction that generated
			 * the trap event.
			 */

			if (thread_endianness == LE) {
				/* Go to 'success', i.e. instruction (6) */
				ucp->uc_mcontext.gp_regs[PT_NIP] += 16;
			} else {
				/*
				 * Thread endianness is BE, so it flipped
				 * inadvertently. Thus we flip back to LE and
				 * set NIP to go to 'failure', instruction (5).
				 */
				ucp->uc_mcontext.gp_regs[PT_MSR] |= 1UL;
				ucp->uc_mcontext.gp_regs[PT_NIP] += 4;
			}
		}
	}

	/*
	 * Big-Endian Machine
	 */

	else {
		/* First trap event */
		if (trap_event == 0) {
			/*
			 * Force thread endianness to be LE. Instructions (1),
			 * (3), and (4) will be executed, generating a second
			 * trap in TM mode.
			 */
			ucp->uc_mcontext.gp_regs[PT_MSR] |= 1UL;
		}
		/* Second trap event */
		else if (trap_event == 1) {
			/*
			 * Do nothing. If bug is present on return from this
			 * second trap event endianness will flip back "automat-
			 * ically" to BE, otherwise thread endianness will
			 * continue to be LE, just as it was set above.
			 */
		}
		/* A third trap event */
		else {
			/*
			 * Once here it means that after returning from the sec-
			 * ond trap event instruction (4) (trap) was executed
			 * as LE, generating a third trap event. In that case
			 * endianness is still LE as set on return from the
			 * first trap event, hence no bug. Otherwise, bug
			 * flipped back to BE on return from the second trap
			 * event and instruction (4) was executed as 'tdi' (so
			 * basically a 'nop') and branch to 'failure' in
			 * instruction (5) was taken to indicate failure and we
			 * never get here.
			 */

			/*
			 * Flip back to BE and go to instruction (6), i.e. go to
			 * 'success'.
			 */
			ucp->uc_mcontext.gp_regs[PT_MSR] &= ~1UL;
			ucp->uc_mcontext.gp_regs[PT_NIP] += 8;
		}
	}

	trap_event++;
}

void usr1_signal_handler(int signo, siginfo_t *si, void *not_used)
{
	/* Got a USR1 signal from ping(), so just tell pong() to exit */
	exit_from_pong = 1;
}

void *ping(void *not_used)
{
	uint64_t i;

	trap_event = 0;

	/*
	 * Wait an amount of context switches so load_fp and load_vec overflows
	 * and MSR_[FP|VEC|V] is 0.
	 */
	for (i = 0; i < 1024*1024*512; i++)
		;

	asm goto(
		/*
		 * [NA] means "Native Endianness", i.e. it tells how a
		 * instruction is executed on machine's native endianness (in
		 * other words, native endianness matches kernel endianness).
		 * [OP] means "Opposite Endianness", i.e. on a BE machine, it
		 * tells how a instruction is executed as a LE instruction; con-
		 * versely, on a LE machine, it tells how a instruction is
		 * executed as a BE instruction. When [NA] is omitted, it means
		 * that the native interpretation of a given instruction is not
		 * relevant for the test. Likewise when [OP] is omitted.
		 */

		" tbegin.        ;" /* (0) tbegin. [NA]                    */
		" tdi  0, 0, 0x48;" /* (1) nop     [NA]; b (3) [OP]        */
		" trap           ;" /* (2) trap    [NA]                    */
		".long 0x1D05007C;" /* (3) tbegin. [OP]                    */
		".long 0x0800E07F;" /* (4) trap    [OP]; nop   [NA]        */
		" b %l[failure]  ;" /* (5) b [NA]; MSR.LE flipped (bug)    */
		" b %l[success]  ;" /* (6) b [NA]; MSR.LE did not flip (ok)*/

		: : : : failure, success);

failure:
	success = false;
	goto exit_from_ping;

success:
	success = true;

exit_from_ping:
	/* Tell pong() to exit before leaving */
	pthread_kill(t1_pong, SIGUSR1);
	return NULL;
}

void *pong(void *not_used)
{
	while (!exit_from_pong)
		/*
		 * Induce context switches on ping() thread
		 * until ping() finishes its job and signs
		 * to exit from this loop.
		 */
		sched_yield();

	return NULL;
}

int tm_trap_test(void)
{
	uint16_t k = 1;
	int cpu, rc;

	pthread_attr_t attr;
	cpu_set_t cpuset;

	struct sigaction trap_sa;

	SKIP_IF(!have_htm());

	trap_sa.sa_flags = SA_SIGINFO;
	trap_sa.sa_sigaction = trap_signal_handler;
	sigaction(SIGTRAP, &trap_sa, NULL);

	struct sigaction usr1_sa;

	usr1_sa.sa_flags = SA_SIGINFO;
	usr1_sa.sa_sigaction = usr1_signal_handler;
	sigaction(SIGUSR1, &usr1_sa, NULL);

	cpu = pick_online_cpu();
	FAIL_IF(cpu < 0);

	// Set only one CPU in the mask. Both threads will be bound to that CPU.
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	/* Init pthread attribute */
	rc = pthread_attr_init(&attr);
	if (rc)
		pr_error(rc, "pthread_attr_init()");

	/*
	 * Bind thread ping() and pong() both to CPU 0 so they ping-pong and
	 * speed up context switches on ping() thread, speeding up the load_fp
	 * and load_vec overflow.
	 */
	rc = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
	if (rc)
		pr_error(rc, "pthread_attr_setaffinity()");

	/* Figure out the machine endianness */
	le = (int) *(uint8_t *)&k;

	printf("%s machine detected. Checking if endianness flips %s",
		le ? "Little-Endian" : "Big-Endian",
		"inadvertently on trap in TM... ");

	rc = fflush(0);
	if (rc)
		pr_error(rc, "fflush()");

	/* Launch ping() */
	rc = pthread_create(&t0_ping, &attr, ping, NULL);
	if (rc)
		pr_error(rc, "pthread_create()");

	exit_from_pong = 0;

	/* Launch pong() */
	rc = pthread_create(&t1_pong, &attr, pong, NULL);
	if (rc)
		pr_error(rc, "pthread_create()");

	rc = pthread_join(t0_ping, NULL);
	if (rc)
		pr_error(rc, "pthread_join()");

	rc = pthread_join(t1_pong, NULL);
	if (rc)
		pr_error(rc, "pthread_join()");

	if (success) {
		printf("no.\n"); /* no, endianness did not flip inadvertently */
		return EXIT_SUCCESS;
	}

	printf("yes!\n"); /* yes, endianness did flip inadvertently */
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	return test_harness(tm_trap_test, "tm_trap_test");
}
