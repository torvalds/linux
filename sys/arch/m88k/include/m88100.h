/*	$OpenBSD: m88100.h,v 1.9 2014/05/31 11:19:06 miod Exp $ */
/*
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
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
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

#ifndef _M88K_M88100_H_
#define _M88K_M88100_H_

/*
 *	88100 RISC definitions
 */

/*
 * DMT0, DMT1, DMT2 layout
 */
#define DMT_BO		0x00008000	/* Byte-Ordering */
#define DMT_DAS		0x00004000	/* Data Access Space */
#define DMT_DOUB1	0x00002000	/* Double Word */
#define DMT_LOCKBAR	0x00001000	/* Bud Lock */
#define DMT_DREG	0x00000F80	/* Destination Registers 5bits */
#define DMT_SIGNED	0x00000040	/* Sign-Extended Bit */
#define DMT_EN		0x0000003C	/* Byte Enable Bit */
#define DMT_WRITE	0x00000002	/* Read/Write Transaction Bit */
#define	DMT_VALID	0x00000001	/* Valid Transaction Bit */

#define	DMT_DREGSHIFT	7
#define	DMT_ENSHIFT	2

#define	DMT_DREGBITS(x)	(((x) & DMT_DREG) >> DMT_DREGSHIFT)
#define	DMT_ENBITS(x)	(((x) & DMT_EN) >> DMT_ENSHIFT)

#if defined(_KERNEL) && !defined(_LOCORE)

void	dae_print(u_int *);
void	data_access_emulation(u_int *);

u_int32_t do_load_word(vaddr_t, int);
u_int32_t do_load_half(vaddr_t, int);
u_int32_t do_load_byte(vaddr_t, int);
void      do_store_word(vaddr_t, u_int32_t, int);
void      do_store_half(vaddr_t, u_int16_t, int);
void      do_store_byte(vaddr_t, u_int8_t, int);
u_int32_t do_xmem_word(vaddr_t, u_int32_t, int);
u_int8_t  do_xmem_byte(vaddr_t, u_int8_t, int);

void	m88100_apply_patches(void);
void	m88100_smp_setup(struct cpu_info *);

/* rewind one instruction */
static __inline__ void
m88100_rewind_insn(struct reg *regs)
{
	regs->sfip = regs->snip;
	regs->snip = regs->sxip;
}

#endif

#endif /* _M88K_M88100_H_ */
