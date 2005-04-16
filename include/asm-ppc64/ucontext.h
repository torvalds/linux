#ifndef _ASMPPC64_UCONTEXT_H
#define _ASMPPC64_UCONTEXT_H

#include <asm/sigcontext.h>

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	sigset_t	  uc_sigmask;
	sigset_t	  __unsued[15];	/* Allow for uc_sigmask growth */
	struct sigcontext uc_mcontext;  /* last for extensibility */
};

#endif /* _ASMPPC64_UCONTEXT_H */
