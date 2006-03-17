/*
 *  linux/include/asm-arm/fpstate.h
 *
 *  Copyright (C) 1995 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_FPSTATE_H
#define __ASM_ARM_FPSTATE_H

#include <linux/config.h>

#ifndef __ASSEMBLY__

/*
 * VFP storage area has:
 *  - FPEXC, FPSCR, FPINST and FPINST2.
 *  - 16 double precision data registers
 *  - an implementation-dependant word of state for FLDMX/FSTMX
 * 
 *  FPEXC will always be non-zero once the VFP has been used in this process.
 */

struct vfp_hard_struct {
	__u64 fpregs[16];
	__u32 fpmx_state;
	__u32 fpexc;
	__u32 fpscr;
	/*
	 * VFP implementation specific state
	 */
	__u32 fpinst;
	__u32 fpinst2;
};

union vfp_state {
	struct vfp_hard_struct	hard;
};

extern void vfp_flush_thread(union vfp_state *);
extern void vfp_release_thread(union vfp_state *);

#define FP_HARD_SIZE 35

struct fp_hard_struct {
	unsigned int save[FP_HARD_SIZE];		/* as yet undefined */
};

#define FP_SOFT_SIZE 35

struct fp_soft_struct {
	unsigned int save[FP_SOFT_SIZE];		/* undefined information */
};

#define IWMMXT_SIZE	0x98

struct iwmmxt_struct {
	unsigned int save[IWMMXT_SIZE / sizeof(unsigned int)];
};

union fp_state {
	struct fp_hard_struct	hard;
	struct fp_soft_struct	soft;
#ifdef CONFIG_IWMMXT
	struct iwmmxt_struct	iwmmxt;
#endif
};

#define FP_SIZE (sizeof(union fp_state) / sizeof(int))

#endif

#endif
