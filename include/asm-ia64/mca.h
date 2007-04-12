/*
 * File:	mca.h
 * Purpose:	Machine check handling specific defines
 *
 * Copyright (C) 1999, 2004 Silicon Graphics, Inc.
 * Copyright (C) Vijay Chander (vijay@engr.sgi.com)
 * Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
 * Copyright (C) Russ Anderson (rja@sgi.com)
 */

#ifndef _ASM_IA64_MCA_H
#define _ASM_IA64_MCA_H

#if !defined(__ASSEMBLY__)

#include <linux/interrupt.h>
#include <linux/types.h>

#include <asm/param.h>
#include <asm/sal.h>
#include <asm/processor.h>
#include <asm/mca_asm.h>

#define IA64_MCA_RENDEZ_TIMEOUT		(20 * 1000)	/* value in milliseconds - 20 seconds */

typedef struct ia64_fptr {
	unsigned long fp;
	unsigned long gp;
} ia64_fptr_t;

typedef union cmcv_reg_u {
	u64	cmcv_regval;
	struct	{
		u64	cmcr_vector		: 8;
		u64	cmcr_reserved1		: 4;
		u64	cmcr_ignored1		: 1;
		u64	cmcr_reserved2		: 3;
		u64	cmcr_mask		: 1;
		u64	cmcr_ignored2		: 47;
	} cmcv_reg_s;

} cmcv_reg_t;

#define cmcv_mask		cmcv_reg_s.cmcr_mask
#define cmcv_vector		cmcv_reg_s.cmcr_vector

enum {
	IA64_MCA_RENDEZ_CHECKIN_NOTDONE	=	0x0,
	IA64_MCA_RENDEZ_CHECKIN_DONE	=	0x1,
	IA64_MCA_RENDEZ_CHECKIN_INIT	=	0x2,
};

/* Information maintained by the MC infrastructure */
typedef struct ia64_mc_info_s {
	u64		imi_mca_handler;
	size_t		imi_mca_handler_size;
	u64		imi_monarch_init_handler;
	size_t		imi_monarch_init_handler_size;
	u64		imi_slave_init_handler;
	size_t		imi_slave_init_handler_size;
	u8		imi_rendez_checkin[NR_CPUS];

} ia64_mc_info_t;

/* Handover state from SAL to OS and vice versa, for both MCA and INIT events.
 * Besides the handover state, it also contains some saved registers from the
 * time of the event.
 * Note: mca_asm.S depends on the precise layout of this structure.
 */

struct ia64_sal_os_state {

	/* SAL to OS */
	u64			os_gp;			/* GP of the os registered with the SAL, physical */
	u64			pal_proc;		/* PAL_PROC entry point, physical */
	u64			sal_proc;		/* SAL_PROC entry point, physical */
	u64			rv_rc;			/* MCA - Rendezvous state, INIT - reason code */
	u64			proc_state_param;	/* from R18 */
	u64			monarch;		/* 1 for a monarch event, 0 for a slave */

	/* common */
	u64			sal_ra;			/* Return address in SAL, physical */
	u64			sal_gp;			/* GP of the SAL - physical */
	pal_min_state_area_t	*pal_min_state;		/* from R17.  physical in asm, virtual in C */
	/* Previous values of IA64_KR(CURRENT) and IA64_KR(CURRENT_STACK).
	 * Note: if the MCA/INIT recovery code wants to resume to a new context
	 * then it must change these values to reflect the new kernel stack.
	 */
	u64			prev_IA64_KR_CURRENT;	/* previous value of IA64_KR(CURRENT) */
	u64			prev_IA64_KR_CURRENT_STACK;
	struct task_struct	*prev_task;		/* previous task, NULL if it is not useful */
	/* Some interrupt registers are not saved in minstate, pt_regs or
	 * switch_stack.  Because MCA/INIT can occur when interrupts are
	 * disabled, we need to save the additional interrupt registers over
	 * MCA/INIT and resume.
	 */
	u64			isr;
	u64			ifa;
	u64			itir;
	u64			iipa;
	u64			iim;
	u64			iha;

	/* OS to SAL */
	u64			os_status;		/* OS status to SAL, enum below */
	u64			context;		/* 0 if return to same context
							   1 if return to new context */
};

enum {
	IA64_MCA_CORRECTED	=	0x0,	/* Error has been corrected by OS_MCA */
	IA64_MCA_WARM_BOOT	=	-1,	/* Warm boot of the system need from SAL */
	IA64_MCA_COLD_BOOT	=	-2,	/* Cold boot of the system need from SAL */
	IA64_MCA_HALT		=	-3	/* System to be halted by SAL */
};

enum {
	IA64_INIT_RESUME	=	0x0,	/* Resume after return from INIT */
	IA64_INIT_WARM_BOOT	=	-1,	/* Warm boot of the system need from SAL */
};

enum {
	IA64_MCA_SAME_CONTEXT	=	0x0,	/* SAL to return to same context */
	IA64_MCA_NEW_CONTEXT	=	-1	/* SAL to return to new context */
};

/* Per-CPU MCA state that is too big for normal per-CPU variables.  */

struct ia64_mca_cpu {
	u64 mca_stack[KERNEL_STACK_SIZE/8];
	u64 init_stack[KERNEL_STACK_SIZE/8];
};

/* Array of physical addresses of each CPU's MCA area.  */
extern unsigned long __per_cpu_mca[NR_CPUS];

extern int cpe_vector;
extern int ia64_cpe_irq;
extern void ia64_mca_init(void);
extern void ia64_mca_cpu_init(void *);
extern void ia64_os_mca_dispatch(void);
extern void ia64_os_mca_dispatch_end(void);
extern void ia64_mca_ucmc_handler(struct pt_regs *, struct ia64_sal_os_state *);
extern void ia64_init_handler(struct pt_regs *,
			      struct switch_stack *,
			      struct ia64_sal_os_state *);
extern void ia64_monarch_init_handler(void);
extern void ia64_slave_init_handler(void);
extern void ia64_mca_cmc_vector_setup(void);
extern int  ia64_reg_MCA_extension(int (*fn)(void *, struct ia64_sal_os_state *));
extern void ia64_unreg_MCA_extension(void);
extern u64 ia64_get_rnat(u64 *);

struct ia64_mca_notify_die {
	struct ia64_sal_os_state *sos;
	int *monarch_cpu;
};

DECLARE_PER_CPU(u64, ia64_mca_pal_base);

#else	/* __ASSEMBLY__ */

#define IA64_MCA_CORRECTED	0x0	/* Error has been corrected by OS_MCA */
#define IA64_MCA_WARM_BOOT	-1	/* Warm boot of the system need from SAL */
#define IA64_MCA_COLD_BOOT	-2	/* Cold boot of the system need from SAL */
#define IA64_MCA_HALT		-3	/* System to be halted by SAL */

#define IA64_INIT_RESUME	0x0	/* Resume after return from INIT */
#define IA64_INIT_WARM_BOOT	-1	/* Warm boot of the system need from SAL */

#define IA64_MCA_SAME_CONTEXT	0x0	/* SAL to return to same context */
#define IA64_MCA_NEW_CONTEXT	-1	/* SAL to return to new context */

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_IA64_MCA_H */
