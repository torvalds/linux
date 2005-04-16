#ifndef __ASM_SH64_SIGCONTEXT_H
#define __ASM_SH64_SIGCONTEXT_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/sigcontext.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */

struct sigcontext {
	unsigned long	oldmask;

	/* CPU registers */
	unsigned long long sc_regs[63];
	unsigned long long sc_tregs[8];
	unsigned long long sc_pc;
	unsigned long long sc_sr;

	/* FPU registers */
	unsigned long long sc_fpregs[32];
	unsigned int sc_fpscr;
	unsigned int sc_fpvalid;
};

#endif /* __ASM_SH64_SIGCONTEXT_H */
