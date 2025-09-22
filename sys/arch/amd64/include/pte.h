/*	$OpenBSD: pte.h,v 1.18 2024/07/09 19:11:06 bluhm Exp $	*/
/*	$NetBSD: pte.h,v 1.1 2003/04/26 18:39:47 fvdl Exp $	*/

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_PTE_H_
#define _MACHINE_PTE_H_

/*
 * amd64 MMU hardware structure:
 *
 * the (first generation) amd64 MMU is a 4-level MMU which maps 2^48 bytes
 * of  virtual memory. The  pagesize we use is 4K (4096 [0x1000] bytes), 
 * although 2M and 4M can be used as well. The indexes in the levels
 * are 9 bits wide (512 64bit entries per level), dividing the bits
 * 9-9-9-9-12.
 *
 * The top level table, called PML4, contains 512 64bit entries pointing
 * to 3rd level table. The 3rd level table is called the 'page directory
 * pointers directory' and has 512 entries pointing to page directories.
 * The 2nd level is the page directory, containing 512 pointers to
 * page table pages. Lastly, level 1 consists of pages containing 512
 * PTEs.
 *
 * Simply put, levels 4-1 all consist of pages containing 512
 * entries pointing to the next level. Level 0 is the actual PTEs
 * themselves.
 *
 * For a description on the other bits, which are i386 compatible,
 * see the i386 pte.h
 */

#if !defined(_LOCORE)

/*
 * here we define the data types for PDEs and PTEs
 */

typedef u_int64_t pd_entry_t;		/* PDE */
typedef u_int64_t pt_entry_t;		/* PTE */

#endif

/*
 * now we define various for playing with virtual addresses
 */

#define L1_SHIFT	12
#define	L2_SHIFT	21
#define	L3_SHIFT	30
#define	L4_SHIFT	39
#define	NBPD_L1		(1ULL << L1_SHIFT) /* # bytes mapped by L1 ent (4K) */
#define	NBPD_L2		(1ULL << L2_SHIFT) /* # bytes mapped by L2 ent (2MB) */
#define	NBPD_L3		(1ULL << L3_SHIFT) /* # bytes mapped by L3 ent (1G) */
#define	NBPD_L4		(1ULL << L4_SHIFT) /* # bytes mapped by L4 ent (512G) */

#define L4_MASK		0x0000ff8000000000UL
#define L3_MASK		0x0000007fc0000000UL
#define L2_MASK		0x000000003fe00000UL
#define L1_MASK		0x00000000001ff000UL

#define L4_FRAME	L4_MASK
#define L3_FRAME	(L4_FRAME|L3_MASK)
#define L2_FRAME	(L3_FRAME|L2_MASK)
#define L1_FRAME	(L2_FRAME|L1_MASK)

#define PAGE_MASK_L2 	(NBPD_L2 - 1)

#define	x86_round_pdr(x) \
	((((unsigned long)(x)) + (NBPD_L2 - 1)) & ~(NBPD_L2 - 1))

/*
 * PDE/PTE bits. These are no different from their i386 counterparts.
 */

#define	PG_V		0x0000000000000001UL	/* valid */
#define	PG_RO		0x0000000000000000UL	/* read-only */
#define	PG_RW		0x0000000000000002UL	/* read-write */
#define	PG_u		0x0000000000000004UL	/* user accessible */
#define	PG_PROT		0x0000000000000006UL
#define	PG_WT		0x0000000000000008UL	/* write through */
#define	PG_N		0x0000000000000010UL	/* non-cacheable */
#define	PG_U		0x0000000000000020UL	/* used */
#define	PG_M		0x0000000000000040UL	/* modified */
#define	PG_PAT		0x0000000000000080UL	/* PAT bit. (on pte) */
#define	PG_PS		0x0000000000000080UL	/* 2MB page size (on pde) */
#define	PG_G		0x0000000000000100UL	/* not flushed */
#define	PG_AVAIL1	0x0000000000000200UL
#define	PG_AVAIL2	0x0000000000000400UL
#define	PG_AVAIL3	0x0000000000000800UL
#define	PG_PATLG	0x0000000000001000UL	/* PAT on large pages */
#define	PG_PKMASK	0x7800000000000000UL	/* Protection Key Mask */
#define	PG_XO		0x0800000000000000UL	/* key1 used for execute-only */
#define	PG_NX		0x8000000000000000UL	/* non-executable */
#define	PG_FRAME	0x000ffffffffff000UL

#define	PG_LGFRAME	0x000fffffffe00000UL	/* large (2M) page frame mask */

#define PGK_VALUE	0xfffffffc		/* key0 is normal */

/* EPT PTE bits */
#define EPT_R		(1ULL << 0)
#define EPT_W		(1ULL << 1)
#define EPT_X		(1ULL << 2)
#define EPT_WB		(6ULL << 3)
#define EPT_PS		(1ULL << 7)

/* Cacheability bits when we are using PAT */
#define	PG_WB		(0)		/* The default */
#define	PG_WC		(PG_WT)		/* WT and CD is WC */
#define	PG_UCMINUS	(PG_N)		/* UC but mtrr can override */
#define	PG_UC		(PG_WT | PG_N)	/* hard UC */

/*
 * short forms of protection codes
 */

#define	PG_KR		0x0000000000000000UL	/* kernel read-only */
#define	PG_KW		0x0000000000000002UL	/* kernel read-write */

/*
 * page protection exception bits
 */

#define PGEX_P		0x01	/* protection violation (vs. no mapping) */
#define PGEX_W		0x02	/* exception during a write cycle */
#define PGEX_U		0x04	/* exception while in user mode (upl) */
#define PGEX_I		0x10	/* instruction fetch blocked by NX */
#define PGEX_PK		0x20	/* protection-key violation */

#ifdef _KERNEL
extern pt_entry_t pg_xo;	/* XO pte bits using PKU key1 */
extern pt_entry_t pg_nx;	/* NX pte bit */
extern pt_entry_t pg_crypt;	/* C pte bit */
extern pt_entry_t pg_g_kern;	/* PG_G if glbl mappings can be used in kern */
#endif /* _KERNEL */

#endif /* _MACHINE_PTE_H_ */
