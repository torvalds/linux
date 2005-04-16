/* $Id: sigcontext.h,v 1.12 1999/09/06 08:22:09 jj Exp $ */
#ifndef __SPARC64_SIGCONTEXT_H
#define __SPARC64_SIGCONTEXT_H

#ifdef __KERNEL__
#include <asm/ptrace.h>
#endif

#ifndef __ASSEMBLY__

#ifdef __KERNEL__

#define __SUNOS_MAXWIN   31

/* This is what SunOS does, so shall I unless we use new 32bit signals or rt signals. */
struct sigcontext32 {
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
	unsigned sigc_spbuf[__SUNOS_MAXWIN];

	/* Windows to restore after signal */
	struct reg_window32 sigc_wbuf[__SUNOS_MAXWIN];
};

#endif

#ifdef __KERNEL__

/* This is what we use for 32bit new non-rt signals. */

typedef struct {
	struct {
		unsigned int psr;
		unsigned int pc;
		unsigned int npc;
		unsigned int y;
		unsigned int u_regs[16]; /* globals and ins */
	}			si_regs;
	int			si_mask;
} __siginfo32_t;

#endif

typedef struct {
	unsigned   int si_float_regs [64];
	unsigned   long si_fsr;
	unsigned   long si_gsr;
	unsigned   long si_fprs;
} __siginfo_fpu_t;

/* This is what SunOS doesn't, so we have to write this alone
   and do it properly. */
struct sigcontext {
	/* The size of this array has to match SI_MAX_SIZE from siginfo.h */
	char			sigc_info[128];
	struct {
		unsigned long	u_regs[16]; /* globals and ins */
		unsigned long	tstate;
		unsigned long	tpc;
		unsigned long	tnpc;
		unsigned int	y;
		unsigned int	fprs;
	}			sigc_regs;
	__siginfo_fpu_t *	sigc_fpu_save;
	struct {
		void	*	ss_sp;
		int		ss_flags;
		unsigned long	ss_size;
	}			sigc_stack;
	unsigned long		sigc_mask;
};

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SIGCONTEXT_H) */
