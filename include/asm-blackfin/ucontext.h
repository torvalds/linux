/** Changes made by Tony Kou   Lineo Inc.    May 2001
 *
 *  Based on: include/m68knommu/ucontext.h
 */

#ifndef _BLACKFIN_UCONTEXT_H
#define _BLACKFIN_UCONTEXT_H

struct ucontext {
	unsigned long uc_flags;	/* the others are necessary */
	struct ucontext *uc_link;
	stack_t uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t uc_sigmask;	/* mask last for extensibility */
};

#endif				/* _BLACKFIN_UCONTEXT_H */
