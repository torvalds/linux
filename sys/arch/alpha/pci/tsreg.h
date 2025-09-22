/* $OpenBSD: tsreg.h,v 1.6 2014/12/24 18:46:14 miod Exp $ */
/* $NetBSD: tsreg.h,v 1.1 1999/06/29 06:46:47 ross Exp $ */

/*-
 * Copyright (c) 1999 by Ross Harvey.  All rights reserved.
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
 *	This product includes software developed by Ross Harvey.
 * 4. The name of Ross Harvey may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ROSS HARVEY ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURP0SE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ROSS HARVEY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * 21272 Core Logic registers and constants.
 */

#define	tsreg() { Generate ctags(1) key. }

/*
 * Superpage pointer from physical address.
 */
#define	S_PAGE(phys) ((void *)ALPHA_PHYS_TO_K0SEG(phys))

/*
 * {LD,ST}QP: LoaD and STore Quad Physical
 */
#define	LDQP(a)	(*(volatile long *)ALPHA_PHYS_TO_K0SEG(a))
#define	STQP(a) LDQP(a)

/*
 * Define extraction functions for bit fields via length and left,right bitno
 */
#define	TSFIELD(r,offs,len) (((r) >> (offs)) & (~0UL >> (64 - (len))))
#define	TSFIELDBB(r,lb,rb) TSFIELD((r), (rb), (lb) - (rb) + 1)

/*
 * EV6 has a new superpage which can pass through 44 address bits.  (Umm, a
 * superduperpage?) But, the firmware doesn't turn it on, so we use the old
 * one and let the HW sign extend va/pa<40> to get us the pa<43> that makes
 * the needed I/O space access. This is just as well; it means we don't have
 * to worry about which GENERIC code might get called on other CPU models.
 *
 *	E.g., we want this:	      0x0801##a000##0000
 *	We use this:		      0x0101##a000##0000
 *	...mix in the old SP:	0xffff##fc00##0000##0000
 *	...after PA sign ext:	0xffff##ff00##a000##0000
 *	(PA<42:41> ignored)
 */

/*
 * This hack allows us to map the I/O address space without using
 * the KSEG sign extension hack.
 */
#define	TS_PHYSADDR(x)							\
	(((x) & ~0x0100##0000##0000) | 0x0800##0000##0000)

/*
 * Cchip CSR Map
 */

#define TS_C_CSC	0x101##a000##0000UL	/* Cchip System Configuration */

#	define	CSC_P1P		(1L << 14)
#	define	CSC_BC(r)	TSFIELD((r), 0, 2)
#	define	CSC_AW		(1L << 8)

#define TS_C_MTR	0x101##a000##0040UL

#define TS_C_MISC	0x101##a000##0080UL	/* Miscellaneous Register */

#	define	MISC_REV(r)	TSFIELD((r), 39, 8)

#define TS_C_MPD	0x101##a000##00c0UL

#	define	MPD_DR	0x08	/* RO: Data receive */
#	define	MPD_CKR	0x04	/* RO: Clock receive */
#	define	MPD_DS	0x02	/* WO: Data send - Must be a 1 to receive */
#	define	MPD_CKS	0x01	/* WO: Clock send */

#define TS_C_AAR0	0x101##a000##0100UL
#define TS_C_AAR1	0x101##a000##0140UL
#define TS_C_AAR2	0x101##a000##0180UL
#define TS_C_AAR3	0x101##a000##01c0UL

#	define	AAR_ASIZ(r)	TSFIELD((r), 12, 4)
#	define	AAR_SPLIT	(1L << 8)

#define TS_C_DIM0	0x101##a000##0200UL
#define TS_C_DIM1	0x101##a000##0240UL
#define TS_C_DIR0	0x101##a000##0280UL
#define TS_C_DIR1	0x101##a000##02c0UL
#define TS_C_DRIR	0x101##a000##0300UL
#define TS_C_PRBEN	0x101##a000##0340UL
#define TS_C_IIC0	0x101##a000##0380UL
#define TS_C_IIC1	0x101##a000##03c0UL
#define TS_C_MPR0	0x101##a000##0400UL
#define TS_C_MPR1	0x101##a000##0440UL
#define TS_C_MPR2	0x101##a000##0480UL
#define TS_C_MPR3	0x101##a000##04c0UL
#define TS_C_MCTL	0x101##a000##0500UL

#define TS_C_TTR	0x101##a000##0580UL
#define TS_C_TDR	0x101##a000##05c0UL

/*
 * Dchip CSR Map
 */

#define TS_D_DSC	0x101##b000##0800UL
#define TS_D_STR	0x101##b000##0840UL
#define TS_D_DREV	0x101##b000##0880UL
#define TS_D_DSC2	0x101##b000##08c0UL

/*
 * Pchip CSR Offsets
 */

#define P_WSBA0		0x0000
#define P_WSBA1		0x0040
#define P_WSBA2		0x0080
#define P_WSBA3		0x00c0

#	define	WSBA_ADDR(r) (TSFIELDBB((r), 31, 20) << 20)
#	define	WSBA_SG	     2
#	define	WSBA_ENA     1

#define P_WSM0		0x0100
#define P_WSM1		0x0140
#define P_WSM2		0x0180
#define P_WSM3		0x01c0

#	define	WSM_AM(r)    TSFIELDBB((r), 31, 20)
#	define	WSM_LEN(r)   ((WSM_AM(r) + 1) << 20)

#define P_TBA0		0x0200
#define P_TBA1		0x0240
#define P_TBA2		0x0280
#define P_TBA3		0x02c0

#define P_PCTL		0x0300
#define P_PLAT		0x0340
	/* reserved	0x0380 */
#define P_PERROR	0x03c0

#define P_PERRMASK	0x0400
#define P_PERRSET	0x0440
#define P_TLBIV		0x0480
#define P_TLBIA		0x04c0

#define P_PMONCTL	0x0500
#define P_PMONCNT	0x0540

#define P_SPRST		0x0800

#define	TS_STEP		0x40

/*
 * Pchip I/O offsets
 */

#define	P_CSRBASE	 0x001##8000##0000UL
#define	P_PCI_MEM	 0x800##0000##0000UL
#define	P_PCI_IO	 0x001##fc00##0000UL
#define	P_PCI_CONFIG	 0x001##fe00##0000UL

/*
 * Construct EV6 I/O Space Address for Pchip 0 and Pchip 1.
 */

#define	TS_P0(offs)	(0x100##0000##0000UL + (offs))
#define	TS_P1(offs)	(0x102##0000##0000UL + (offs))
#define	TS_Pn(n, offs)	(0x100##0000##0000UL + 0x2##0000##0000UL * (n) + (offs))

/*
 * Tsunami Generic Register Type
 */
typedef struct _ts_gr {
	volatile u_int64_t tsg_r;
	long	tsg_deadspace[7];
} TS_GR;

/*
 * Tsunami Pchip
 */

struct	ts_pport {
	TS_GR	tsp_resA;
	TS_GR	tsp_error;	/* Pchip Error */

	TS_GR	tsp_perrmask;	/* Pchip Error Mask */
	TS_GR	tsp_perrset;	/* Pchip Error Set */
	TS_GR	tsp_tlbiv;	/* Translation Buffer Invalidate Virtual */
	TS_GR	tsp_tlbia;	/* Translation Buffer Invalidate All */

	TS_GR	tsp_pmonctl;	/* PChip Monitor Control */
	TS_GR	tsp_pmoncnt;	/* PChip Monitor Counters */
	TS_GR	tsp_resB;
	TS_GR	tsp_resC;

	TS_GR	tsp_resD_K[8];
};

struct	ts_gport {
	TS_GR	tsp_resA[2];
	TS_GR	tsp_serror;
	TS_GR	tsp_serrmask;
	TS_GR	tsp_serrset;
	TS_GR	tsp_resB;
	TS_GR	tsp_gperrmask;
	TS_GR	tsp_gperren;
	TS_GR	tsp_gperrset;
	TS_GR	tsp_resC;
	TS_GR	tsp_tlbiv;
	TS_GR	tsp_tlbia;
	TS_GR	tsp_resD[2];
	TS_GR	tsp_sctl;
	TS_GR	tsp_resE[3];
};

struct	ts_aport {
	TS_GR	tsp_resA[2];
	TS_GR	tsp_agperror;
	TS_GR	tsp_agperrmask;
	TS_GR	tsp_agperrset;
	TS_GR	tsp_agplastwr;
	TS_GR	tsp_aperror;
	TS_GR	tsp_aperrmask;
	TS_GR	tsp_aperrset;
	TS_GR	tsp_resB;
	TS_GR	tsp_tlbiv;
	TS_GR	tsp_tlbia;
	TS_GR	tsp_resC[6];
};

struct	ts_pchip {
	TS_GR	tsp_wsba[4];	/* Window Space Base Address */

	TS_GR	tsp_wsm[4];	/* Window Space Mask */

	TS_GR	tsp_tba[4];	/* Translated Base Address */

	TS_GR	tsp_pctl;	/* Pchip Control */
	TS_GR	tsp_plat;	/* Pchip Latency */

	union {
		struct ts_pport	p;
		struct ts_gport	g;
		struct ts_aport	a;
	} port;

	TS_GR	tsp_sprts;	/* ??? */
	TS_GR	tsp_res[31];
};
