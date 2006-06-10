#ifndef _ASM_M32R_SIGCONTEXT_H
#define _ASM_M32R_SIGCONTEXT_H

/* $Id$ */

#include <linux/config.h>

struct sigcontext {
	/* CPU registers */
	/* Saved main processor registers. */
	unsigned long sc_r4;
	unsigned long sc_r5;
	unsigned long sc_r6;
	struct pt_regs *sc_pt_regs;
	unsigned long sc_r0;
	unsigned long sc_r1;
	unsigned long sc_r2;
	unsigned long sc_r3;
	unsigned long sc_r7;
	unsigned long sc_r8;
	unsigned long sc_r9;
	unsigned long sc_r10;
	unsigned long sc_r11;
	unsigned long sc_r12;

	/* Saved main processor status and miscellaneous context registers. */
#if defined(CONFIG_ISA_M32R2) && defined(CONFIG_ISA_DSP_LEVEL2)
	unsigned long sc_acc0h;
	unsigned long sc_acc0l;
	unsigned long sc_acc1h;
	unsigned long sc_acc1l;
#elif defined(CONFIG_ISA_M32R2) || defined(CONFIG_ISA_M32R)
	unsigned long sc_acch;
	unsigned long sc_accl;
	unsigned long sc_dummy_acc1h;
	unsigned long sc_dummy_acc1l;
#else
#error unknown isa configuration
#endif
	unsigned long sc_psw;
	unsigned long sc_bpc;		/* saved PC for TRAP syscalls */
	unsigned long sc_bbpsw;
	unsigned long sc_bbpc;
	unsigned long sc_spu;		/* saved user stack */
	unsigned long sc_fp;
	unsigned long sc_lr;		/* saved PC for JL syscalls */
	unsigned long sc_spi;		/* saved kernel stack */

	unsigned long	oldmask;
};

#endif  /* _ASM_M32R_SIGCONTEXT_H */
