// SPDX-License-Identifier: GPL-2.0+

#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "dexcr.h"
#include "reg.h"
#include "utils.h"

static jmp_buf generic_signal_jump_buf;

static void generic_signal_handler(int signum, siginfo_t *info, void *context)
{
	longjmp(generic_signal_jump_buf, 0);
}

bool dexcr_exists(void)
{
	struct sigaction old;
	volatile bool exists;

	old = push_signal_handler(SIGILL, generic_signal_handler);
	if (setjmp(generic_signal_jump_buf))
		goto out;

	/*
	 * If the SPR is not recognised by the hardware it triggers
	 * a hypervisor emulation interrupt. If the kernel does not
	 * recognise/try to emulate it, we receive a SIGILL signal.
	 *
	 * If we do not receive a signal, assume we have the SPR or the
	 * kernel is trying to emulate it correctly.
	 */
	exists = false;
	mfspr(SPRN_DEXCR_RO);
	exists = true;

out:
	pop_signal_handler(SIGILL, old);
	return exists;
}

unsigned int pr_which_to_aspect(unsigned long which)
{
	switch (which) {
	case PR_PPC_DEXCR_SBHE:
		return DEXCR_PR_SBHE;
	case PR_PPC_DEXCR_IBRTPD:
		return DEXCR_PR_IBRTPD;
	case PR_PPC_DEXCR_SRAPD:
		return DEXCR_PR_SRAPD;
	case PR_PPC_DEXCR_NPHIE:
		return DEXCR_PR_NPHIE;
	default:
		FAIL_IF_EXIT_MSG(true, "unknown PR aspect");
	}
}

int pr_get_dexcr(unsigned long which)
{
	return prctl(PR_PPC_GET_DEXCR, which, 0UL, 0UL, 0UL);
}

int pr_set_dexcr(unsigned long which, unsigned long ctrl)
{
	return prctl(PR_PPC_SET_DEXCR, which, ctrl, 0UL, 0UL);
}

bool pr_dexcr_aspect_supported(unsigned long which)
{
	if (pr_get_dexcr(which) == -1)
		return errno == ENODEV;

	return true;
}

bool pr_dexcr_aspect_editable(unsigned long which)
{
	return pr_get_dexcr(which) & PR_PPC_DEXCR_CTRL_EDITABLE;
}

/*
 * Just test if a bad hashchk triggers a signal, without checking
 * for support or if the NPHIE aspect is enabled.
 */
bool hashchk_triggers(void)
{
	struct sigaction old;
	volatile bool triggers;

	old = push_signal_handler(SIGILL, generic_signal_handler);
	if (setjmp(generic_signal_jump_buf))
		goto out;

	triggers = true;
	do_bad_hashchk();
	triggers = false;

out:
	pop_signal_handler(SIGILL, old);
	return triggers;
}

unsigned int get_dexcr(enum dexcr_source source)
{
	switch (source) {
	case DEXCR:
		return mfspr(SPRN_DEXCR_RO);
	case HDEXCR:
		return mfspr(SPRN_HDEXCR_RO);
	case EFFECTIVE:
		return mfspr(SPRN_DEXCR_RO) | mfspr(SPRN_HDEXCR_RO);
	default:
		FAIL_IF_EXIT_MSG(true, "bad enum dexcr_source");
	}
}

void await_child_success(pid_t pid)
{
	int wstatus;

	FAIL_IF_EXIT_MSG(pid == -1, "fork failed");
	FAIL_IF_EXIT_MSG(waitpid(pid, &wstatus, 0) == -1, "wait failed");
	FAIL_IF_EXIT_MSG(!WIFEXITED(wstatus), "child did not exit cleanly");
	FAIL_IF_EXIT_MSG(WEXITSTATUS(wstatus) != 0, "child exit error");
}

/*
 * Perform a hashst instruction. The following components determine the result
 *
 * 1. The LR value (any register technically)
 * 2. The SP value (also any register, but it must be a valid address)
 * 3. A secret key managed by the kernel
 *
 * The result is stored to the address held in SP.
 */
void hashst(unsigned long lr, void *sp)
{
	asm volatile ("addi 31, %0, 0;"		/* set r31 (pretend LR) to lr */
		      "addi 30, %1, 8;"		/* set r30 (pretend SP) to sp + 8 */
		      PPC_RAW_HASHST(31, -8, 30)	/* compute hash into stack location */
		      : : "r" (lr), "r" (sp) : "r31", "r30", "memory");
}

/*
 * Perform a hashchk instruction. A hash is computed as per hashst(),
 * however the result is not stored to memory. Instead the existing
 * value is read and compared against the computed hash.
 *
 * If they match, execution continues.
 * If they differ, an interrupt triggers.
 */
void hashchk(unsigned long lr, void *sp)
{
	asm volatile ("addi 31, %0, 0;"		/* set r31 (pretend LR) to lr */
		      "addi 30, %1, 8;"		/* set r30 (pretend SP) to sp + 8 */
		      PPC_RAW_HASHCHK(31, -8, 30)	/* check hash at stack location */
		      : : "r" (lr), "r" (sp) : "r31", "r30", "memory");
}

void do_bad_hashchk(void)
{
	unsigned long hash = 0;

	hashst(0, &hash);
	hash += 1;
	hashchk(0, &hash);
}
