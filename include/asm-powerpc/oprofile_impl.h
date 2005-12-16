/*
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * Based on alpha version.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_OPROFILE_IMPL_H
#define _ASM_POWERPC_OPROFILE_IMPL_H
#ifdef __KERNEL__

#define OP_MAX_COUNTER 8

/* Per-counter configuration as set via oprofilefs.  */
struct op_counter_config {
#ifdef __powerpc64__
	unsigned long valid;
#endif
	unsigned long enabled;
	unsigned long event;
	unsigned long count;
	/* Classic doesn't support per-counter user/kernel selection */
	unsigned long kernel;
	unsigned long user;
	unsigned long unit_mask;
};

/* System-wide configuration as set via oprofilefs.  */
struct op_system_config {
#ifdef CONFIG_PPC64
	unsigned long mmcr0;
	unsigned long mmcr1;
	unsigned long mmcra;
#endif
	unsigned long enable_kernel;
	unsigned long enable_user;
#ifdef CONFIG_PPC64
	unsigned long backtrace_spinlocks;
#endif
};

/* Per-arch configuration */
struct op_powerpc_model {
	void (*reg_setup) (struct op_counter_config *,
			   struct op_system_config *,
			   int num_counters);
	void (*cpu_setup) (void *);
	void (*start) (struct op_counter_config *);
	void (*stop) (void);
	void (*handle_interrupt) (struct pt_regs *,
				  struct op_counter_config *);
	int num_counters;
};

#ifdef CONFIG_FSL_BOOKE
extern struct op_powerpc_model op_model_fsl_booke;
#else /* Otherwise, it's classic */

#ifdef CONFIG_PPC64
extern struct op_powerpc_model op_model_rs64;
extern struct op_powerpc_model op_model_power4;

#else /* Otherwise, CONFIG_PPC32 */
extern struct op_powerpc_model op_model_7450;
#endif

/* All the classic PPC parts use these */
static inline unsigned int ctr_read(unsigned int i)
{
	switch(i) {
	case 0:
		return mfspr(SPRN_PMC1);
	case 1:
		return mfspr(SPRN_PMC2);
	case 2:
		return mfspr(SPRN_PMC3);
	case 3:
		return mfspr(SPRN_PMC4);
	case 4:
		return mfspr(SPRN_PMC5);
	case 5:
		return mfspr(SPRN_PMC6);

/* No PPC32 chip has more than 6 so far */
#ifdef CONFIG_PPC64
	case 6:
		return mfspr(SPRN_PMC7);
	case 7:
		return mfspr(SPRN_PMC8);
#endif
	default:
		return 0;
	}
}

static inline void ctr_write(unsigned int i, unsigned int val)
{
	switch(i) {
	case 0:
		mtspr(SPRN_PMC1, val);
		break;
	case 1:
		mtspr(SPRN_PMC2, val);
		break;
	case 2:
		mtspr(SPRN_PMC3, val);
		break;
	case 3:
		mtspr(SPRN_PMC4, val);
		break;
	case 4:
		mtspr(SPRN_PMC5, val);
		break;
	case 5:
		mtspr(SPRN_PMC6, val);
		break;

/* No PPC32 chip has more than 6, yet */
#ifdef CONFIG_PPC64
	case 6:
		mtspr(SPRN_PMC7, val);
		break;
	case 7:
		mtspr(SPRN_PMC8, val);
		break;
#endif
	default:
		break;
	}
}
#endif /* !CONFIG_FSL_BOOKE */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_OPROFILE_IMPL_H */
