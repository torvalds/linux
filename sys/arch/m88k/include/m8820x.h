/*	$OpenBSD: m8820x.h,v 1.11 2025/08/12 16:17:10 miod Exp $ */
/*
 * Copyright (c) 2004, Miodrag Vallat.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
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

#ifndef	_M88K_M8820X_H_
#define	_M88K_M8820X_H_

/*
 * 8820x CMMU definitions
 */

/* CMMU registers */
#define CMMU_IDR	(0x000 / 4)	/* CMMU id register */
#define CMMU_SCR	(0x004 / 4)	/* system command register */
#define CMMU_SSR	(0x008 / 4)	/* system status register */
#define CMMU_SAR	(0x00c / 4)	/* system address register */
#define CMMU_SCTR	(0x104 / 4)	/* system control register */
#define CMMU_PFSR	(0x108 / 4)	/* P bus fault status register */
#define CMMU_PFAR	(0x10c / 4)	/* P bus fault address register */
#define CMMU_SAPR	(0x200 / 4)	/* supervisor area pointer register */
#define CMMU_UAPR	(0x204 / 4)	/* user area pointer register */
#define CMMU_BWP0	(0x400 / 4)	/* block ATC writer port 0 */
#define CMMU_BWP1	(0x404 / 4)	/* block ATC writer port 1 */
#define CMMU_BWP2	(0x408 / 4)	/* block ATC writer port 2 */
#define CMMU_BWP3	(0x40c / 4)	/* block ATC writer port 3 */
#define CMMU_BWP4	(0x410 / 4)	/* block ATC writer port 4 */
#define CMMU_BWP5	(0x414 / 4)	/* block ATC writer port 5 */
#define CMMU_BWP6	(0x418 / 4)	/* block ATC writer port 6 */
#define CMMU_BWP7	(0x41c / 4)	/* block ATC writer port 7 */
#define CMMU_BWP(n)	(CMMU_BWP0 + (n))
#define CMMU_CDP0	(0x800 / 4)	/* cache data port 0 */
#define CMMU_CDP1	(0x804 / 4)	/* cache data port 1 */
#define CMMU_CDP2	(0x808 / 4)	/* cache data port 2 */
#define CMMU_CDP3	(0x80c / 4)	/* cache data port 3 */
#define CMMU_CTP0	(0x840 / 4)	/* cache tag port 0 */
#define CMMU_CTP1	(0x844 / 4)	/* cache tag port 1 */
#define CMMU_CTP2	(0x848 / 4)	/* cache tag port 2 */
#define CMMU_CTP3	(0x84c / 4)	/* cache tag port 3 */
#define CMMU_CSSP0	(0x880 / 4)	/* cache set status register */
#define CMMU_CSSP(n)	((0x880 + (n * 0x10)) / 4)
/* the following only exist on 88204 */
#define CMMU_CSSP1	(0x890 / 4)	/* cache set status register */
#define CMMU_CSSP2	(0x8a0 / 4)	/* cache set status register */
#define CMMU_CSSP3	(0x8b0 / 4)	/* cache set status register */

/* system commands */
#define CMMU_FLUSH_CACHE_INV_LINE	0x14	/* data cache invalidate */
#define CMMU_FLUSH_CACHE_INV_PAGE	0x15
#define CMMU_FLUSH_CACHE_INV_SEGMENT	0x16
#define CMMU_FLUSH_CACHE_INV_ALL	0x17
#define CMMU_FLUSH_CACHE_CB_LINE	0x18	/* data cache copyback */
#define CMMU_FLUSH_CACHE_CB_PAGE	0x19
#define CMMU_FLUSH_CACHE_CB_SEGMENT	0x1a
#define CMMU_FLUSH_CACHE_CB_ALL		0x1b
#define CMMU_FLUSH_CACHE_CBI_LINE	0x1c	/* copyback and invalidate */
#define CMMU_FLUSH_CACHE_CBI_PAGE	0x1d
#define CMMU_FLUSH_CACHE_CBI_SEGMENT	0x1e
#define CMMU_FLUSH_CACHE_CBI_ALL	0x1f
#define CMMU_PROBE_USER			0x20	/* probe user address */
#define CMMU_PROBE_SUPER		0x24	/* probe supervisor address */
#define CMMU_FLUSH_USER_LINE		0x30	/* flush PATC */
#define CMMU_FLUSH_USER_PAGE		0x31
#define CMMU_FLUSH_USER_SEGMENT		0x32
#define CMMU_FLUSH_USER_ALL		0x33
#define CMMU_FLUSH_SUPER_LINE		0x34
#define CMMU_FLUSH_SUPER_PAGE		0x35
#define CMMU_FLUSH_SUPER_SEGMENT	0x36
#define CMMU_FLUSH_SUPER_ALL		0x37

/* system control values */
#define CMMU_SCTR_PE	0x00008000	/* parity enable */
#define CMMU_SCTR_SE	0x00004000	/* snoop enable */
#define CMMU_SCTR_PR	0x00002000	/* priority arbitration */

/* P bus fault status */
#define	CMMU_PFSR_FAULT(pfsr)	(((pfsr) >> 16) & 0x07)
#define CMMU_PFSR_SUCCESS	0	/* no fault */
#define CMMU_PFSR_BERROR	3	/* bus error */
#define CMMU_PFSR_SFAULT	4	/* segment fault */
#define CMMU_PFSR_PFAULT	5	/* page fault */
#define CMMU_PFSR_SUPER		6	/* supervisor violation */
#define CMMU_PFSR_WRITE		7	/* writer violation */

/* CSSP values */
#define	CMMU_CSSP_L5		0x20000000
#define	CMMU_CSSP_L4		0x10000000
#define	CMMU_CSSP_L3		0x08000000
#define	CMMU_CSSP_L2		0x04000000
#define	CMMU_CSSP_L1		0x02000000
#define	CMMU_CSSP_L0		0x01000000
#define	CMMU_CSSP_D3		0x00800000
#define	CMMU_CSSP_D2		0x00400000
#define	CMMU_CSSP_D1		0x00200000
#define	CMMU_CSSP_D0		0x00100000
#define	CMMU_CSSP_VV(n,v)	(((v) & 0x03) << (12 + 2 * (n)))
#define	CMMU_VV_EXCLUSIVE	0x00
#define	CMMU_VV_MODIFIED	0x01
#define	CMMU_VV_SHARED		0x02
#define	CMMU_VV_INVALID		0x03

/* IDR values */
#define	CMMU_ID(idr)		((idr) >> 24)
#define	CMMU_TYPE(idr)		(((idr) >> 21) & 0x07)
#define	CMMU_VERSION(idr)	(((idr) >> 16) & 0x1f)
#define	M88200_ID		5
#define	M88204_ID		6

/* SSR values */
#define	CMMU_SSR_CE		0x00008000	/* copyback error */
#define	CMMU_SSR_BE		0x00004000	/* bus error */
#define	CMMU_SSR_SO		0x00000100
#define	CMMU_SSR_M		0x00000010
#define	CMMU_SSR_U		0x00000008
#define	CMMU_SSR_PROT		0x00000004
#define	CMMU_SSR_BH		0x00000002	/* probe BATC hit */
#define	CMMU_SSR_V		0x00000001

/*
 * Cache line information
 */

#define	MC88200_CACHE_SHIFT	4
#define	MC88200_CACHE_LINE	(1 << MC88200_CACHE_SHIFT)

/*
 * Hardwired BATC information
 */

#define	BATC8			0xfff7ffb5
#define	BATC9			0xfffffff5

#define	BATC8_VA		0xfff00000
#define	BATC9_VA		0xfff80000

#ifndef _LOCORE

/*
 * CMMU kernel information
 */
struct m8820x_cmmu;

struct m8820x_cmmu {
	volatile u_int32_t *cmmu_regs;	/* CMMU "base" area */
	u_int32_t	cmmu_idr;
	/*
	 * These two pointers point to the next CMMU tied to the same CPU,
	 * and the next CMMU _of the same I/D type_ tied to the same CPU,
	 * respectively.
	 */
	struct m8820x_cmmu *cmmu_next;
	struct m8820x_cmmu *cmmu_next_same_mode;

#ifdef M88200_HAS_SPLIT_ADDRESS
	vaddr_t		cmmu_addr;	/* address range */
	vaddr_t		cmmu_addr_mask;	/* address mask */
#endif
};

#define	INST_CMMU	0x00	/* even number */
#define	DATA_CMMU	0x01	/* odd number */
#define	CMMU_MODE(num)	((num) & 1)

#ifdef	M88200_HAS_ASYMMETRICAL_ASSOCIATION
#define MAX_CMMUS	16		/* maximum cmmus on the board */
#else
#define MAX_CMMUS	8		/* maximum cmmus on the board */
#endif
extern struct m8820x_cmmu m8820x_cmmu[MAX_CMMUS];
extern u_int cmmu_shift;
extern u_int max_cmmus;

void	m8820x_setup_board_config(void);
cpuid_t	m8820x_cpu_number(void);

extern const struct cmmu_p cmmu8820x;

#endif	/* _LOCORE */
#endif	/* _M88K_M8820X_H_ */
