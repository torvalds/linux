#ifndef ASM_X86__UCONTEXT_H
#define ASM_X86__UCONTEXT_H

#define UC_FP_XSTATE	0x1	/* indicates the presence of extended state
				 * information in the memory layout pointed
				 * by the fpstate pointer in the ucontext's
				 * sigcontext struct (uc_mcontext).
				 */

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif /* ASM_X86__UCONTEXT_H */
