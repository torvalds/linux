/*	$OpenBSD: pte.h,v 1.14 2023/01/07 10:09:34 kettenis Exp $	*/

/* 
 * Copyright (c) 1990,1993,1994 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 * 	Utah $Hdr: pmap.h 1.24 94/12/14$
 *	Author: Mike Hibler, Bob Wheeler, University of Utah CSL, 9/90
 */

#ifndef	_MACHINE_PTE_H_
#define	_MACHINE_PTE_H_

#define	PTE_PROT_SHIFT	19
#define	PTE_PROT(tlb)	((tlb) >> PTE_PROT_SHIFT)
#define	TLB_PROT(pte)	((pte) << PTE_PROT_SHIFT)
#define	PDE_MASK	(0xffc00000)
#define	PDE_SIZE	(0x00400000)
#define	PTE_MASK	(0x003ff000)
#define	PTE_PAGE(pte)	((pte) & ~PAGE_MASK)

/* TLB access/protection values */
#define TLB_WIRED	0x40000000	/* software only */
#define TLB_REFTRAP	0x20000000
#define TLB_DIRTY	0x10000000
#define TLB_BREAK	0x08000000
#define TLB_AR_MASK	0x07f00000
#define	TLB_READ	0x00000000
#define	TLB_WRITE	0x01000000
#define	TLB_EXECUTE	0x02000000
#define	TLB_GATEWAY	0x04000000
#define	TLB_USER	0x00f00000
/* no execute access at any PL; no GATEWAY promotion */ 
#define		TLB_AR_NA	0x07300000
#define		TLB_AR_R	TLB_READ
/* execute access at designated PL; no GATEWAY promotion */
#define		TLB_AR_X	0x07000000
#define		TLB_AR_RW	TLB_READ|TLB_WRITE
#define		TLB_AR_RX	TLB_READ|TLB_EXECUTE
#define		TLB_AR_RWX	TLB_READ|TLB_WRITE|TLB_EXECUTE
#define TLB_UNCACHABLE	0x00080000
#define TLB_PID_MASK	0x0000fffe

#define	TLB_BITS	"\020\024U\031W\032X\033N\034B\035D\036R\037H"

/* protection for a gateway page */
#define TLB_GATE_PROT	0x04c00000

/* protection for break page */
#define TLB_BREAK_PROT	0x02c00000

#ifndef	_LOCORE
typedef	u_int32_t	pt_entry_t;
#endif

#endif	/* _MACHINE_PTE_H_ */
