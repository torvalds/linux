/*	$OpenBSD: frame.h,v 1.4 2007/11/15 21:24:12 miod Exp $ */
/*
 * Copyright (c) 1996 Nivas Madhur
 * Mach Operating System
 * Copyright (c) 1993-1992 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 * Motorola 88100 exception frame definitions
 *
 */
/*
 */
#ifndef _M88K_FRAME_H_
#define _M88K_FRAME_H_

#include <machine/reg.h>

struct trapframe {
	struct reg	tf_regs;
	register_t	tf_vector;	/* exception vector number */
	register_t	tf_mask;	/* interrupt mask level */
	register_t	tf_flags;	/* exception handling flags */
	register_t	tf_scratch1;	/* reserved for use by locore */
	register_t	tf_ipfsr;	/* P BUS status */
	register_t	tf_dpfsr;	/* P BUS status */
	void		*tf_cpu;	/* cpu_info pointer */
};

#define	tf_r		tf_regs.r
#define	tf_sp		tf_regs.r[31]
#define	tf_epsr		tf_regs.epsr
#define	tf_fpsr		tf_regs.fpsr
#define	tf_fpcr		tf_regs.fpcr
#define	tf_sxip		tf_regs.sxip
#define	tf_snip		tf_regs.snip
#define	tf_sfip		tf_regs.sfip
#define	tf_exip		tf_regs.sxip
#define	tf_enip		tf_regs.snip
#define	tf_ssbr		tf_regs.ssbr
#define	tf_dmt0		tf_regs.dmt0
#define	tf_dmd0		tf_regs.dmd0
#define	tf_dma0		tf_regs.dma0
#define	tf_dmt1		tf_regs.dmt1
#define	tf_dmd1		tf_regs.dmd1
#define	tf_dma1		tf_regs.dma1
#define	tf_dmt2		tf_regs.dmt2
#define	tf_dmd2		tf_regs.dmd2
#define	tf_dma2		tf_regs.dma2
#define	tf_duap		tf_regs.ssbr
#define	tf_dsr		tf_regs.dmt0
#define	tf_dlar		tf_regs.dmd0
#define	tf_dpar		tf_regs.dma0
#define	tf_isr		tf_regs.dmt1
#define	tf_ilar		tf_regs.dmd1
#define	tf_ipar		tf_regs.dma1
#define	tf_isap		tf_regs.dmt2
#define	tf_dsap		tf_regs.dmd2
#define	tf_iuap		tf_regs.dma2
#define	tf_fpecr	tf_regs.fpecr
#define	tf_fphs1	tf_regs.fphs1
#define	tf_fpls1	tf_regs.fpls1
#define	tf_fphs2	tf_regs.fphs2
#define	tf_fpls2	tf_regs.fpls2
#define	tf_fppt		tf_regs.fppt
#define	tf_fprh		tf_regs.fprh
#define	tf_fprl		tf_regs.fprl
#define	tf_fpit		tf_regs.fpit

#endif /* _M88K_FRAME_H_ */
