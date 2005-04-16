#ifndef _ASM_M32R_UCONTEXT_H
#define _ASM_M32R_UCONTEXT_H

/* orig : i386 2.4.18 */

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif /* _ASM_M32R_UCONTEXT_H */
