#ifndef __ASM_SH64_UCONTEXT_H
#define __ASM_SH64_UCONTEXT_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/ucontext.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif /* __ASM_SH64_UCONTEXT_H */
