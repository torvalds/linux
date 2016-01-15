/*
 * Copyright 2015, Michael Neuling, IBM Corp.
 * Licensed under GPLv2.
 *
 * Test the kernel's signal return code to ensure that it doesn't
 * crash when both the transactional and suspend MSR bits are set in
 * the signal context.
 *
 * For this test, we send ourselves a SIGUSR1.  In the SIGUSR1 handler
 * we modify the signal context to set both MSR TM S and T bits (which
 * is "reserved" by the PowerISA). When we return from the signal
 * handler (implicit sigreturn), the kernel should detect reserved MSR
 * value and send us with a SIGSEGV.
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "utils.h"
#include "tm.h"

int segv_expected = 0;

void signal_segv(int signum)
{
	if (segv_expected && (signum == SIGSEGV))
		_exit(0);
	_exit(1);
}

void signal_usr1(int signum, siginfo_t *info, void *uc)
{
	ucontext_t *ucp = uc;

	/* Link tm checkpointed context to normal context */
	ucp->uc_link = ucp;
	/* Set all TM bits so that the context is now invalid */
#ifdef __powerpc64__
	ucp->uc_mcontext.gp_regs[PT_MSR] |= (7ULL << 32);
#else
	ucp->uc_mcontext.regs->gpr[PT_MSR] |= (7ULL);
#endif
	/* Should segv on return becuase of invalid context */
	segv_expected = 1;
}

int tm_signal_msr_resv()
{
	struct sigaction act;

	SKIP_IF(!have_htm());

	act.sa_sigaction = signal_usr1;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGUSR1, &act, NULL) < 0) {
		perror("sigaction sigusr1");
		exit(1);
	}
	if (signal(SIGSEGV, signal_segv) == SIG_ERR)
		exit(1);

	raise(SIGUSR1);

	/* We shouldn't get here as we exit in the segv handler */
	return 1;
}

int main(void)
{
	return test_harness(tm_signal_msr_resv, "tm_signal_msr_resv");
}
