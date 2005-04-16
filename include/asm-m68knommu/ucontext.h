#ifndef _M68KNOMMU_UCONTEXT_H
#define _M68KNOMMU_UCONTEXT_H

typedef int greg_t;
#define NGREG 18
typedef greg_t gregset_t[NGREG];

#ifdef CONFIG_FPU
typedef struct fpregset {
	int f_pcr;
	int f_psr;
	int f_fpiaddr;
	int f_fpregs[8][3];
} fpregset_t;
#endif

struct mcontext {
	int version;
	gregset_t gregs;
#ifdef CONFIG_FPU
	fpregset_t fpregs;
#endif
};

#define MCONTEXT_VERSION 2

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct mcontext	  uc_mcontext;
#ifdef CONFIG_FPU
	unsigned long	  uc_filler[80];
#endif
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif
