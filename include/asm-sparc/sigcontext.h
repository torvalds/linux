#ifndef __SPARC_SIGCONTEXT_H
#define __SPARC_SIGCONTEXT_H

#ifdef __KERNEL__
#include <asm/ptrace.h>

#ifndef __ASSEMBLY__

#define __SUNOS_MAXWIN   31

/* This is what SunOS does, so shall I. */
struct sigcontext {
	int sigc_onstack;      /* state to restore */
	int sigc_mask;         /* sigmask to restore */
	int sigc_sp;           /* stack pointer */
	int sigc_pc;           /* program counter */
	int sigc_npc;          /* next program counter */
	int sigc_psr;          /* for condition codes etc */
	int sigc_g1;           /* User uses these two registers */
	int sigc_o0;           /* within the trampoline code. */

	/* Now comes information regarding the users window set
	 * at the time of the signal.
	 */
	int sigc_oswins;       /* outstanding windows */

	/* stack ptrs for each regwin buf */
	char *sigc_spbuf[__SUNOS_MAXWIN];

	/* Windows to restore after signal */
	struct {
		unsigned long	locals[8];
		unsigned long	ins[8];
	} sigc_wbuf[__SUNOS_MAXWIN];
};

typedef struct {
	struct {
		unsigned long psr;
		unsigned long pc;
		unsigned long npc;
		unsigned long y;
		unsigned long u_regs[16]; /* globals and ins */
	}		si_regs;
	int		si_mask;
} __siginfo_t;

typedef struct {
	unsigned   long si_float_regs [32];
	unsigned   long si_fsr;
	unsigned   long si_fpqdepth;
	struct {
		unsigned long *insn_addr;
		unsigned long insn;
	} si_fpqueue [16];
} __siginfo_fpu_t;

#endif /* !(__ASSEMBLY__) */

#endif /* (__KERNEL__) */

#endif /* !(__SPARC_SIGCONTEXT_H) */
