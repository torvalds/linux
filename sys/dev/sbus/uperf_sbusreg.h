/*	$OpenBSD: uperf_sbusreg.h,v 1.4 2024/09/04 07:54:52 mglocker Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
 * All rights reserved.
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
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

/*
 * System Controller registers (for both uni- and dual- processor versions)
 */

/*
 * Ebus interface registers
 */
#define	USC_ADDR	0x0000
#define	USC_DATA	0x0004

/* Indirect registers */
#define	USC_CTRL	0x00		/* overall: control */
#define	USC_ID		0x04		/* overall: identity */
#define	USC_PERF0	0x08		/* overall: perf counter 0 */
#define	USC_PERF1	0x0c		/* overall: perf connter 1 */
#define	USC_PERFSHAD	0x10		/* overall: perf shadow */
#define	USC_PERFCTRL	0x20		/* overall: perf control */
#define	USC_DBGPIN	0x30		/* overall: debug pin control */
#define	USC_P0CFG	0x40		/* port/processor 1 config */
#define	USC_P0STS	0x44		/* port/processor 0 status */
#define	USC_P1CFG	0x48		/* port/processor 1 config */
#define	USC_P1STS	0x4c		/* port/processor 1 status */
#define	USC_SYSIOCFG	0x50		/* sysio/u2s port config */
#define	USC_SYSIOSTS	0x54		/* sysio/u2s port status */
#define	USC_FFBCFG	0x58		/* dsc: ffb config */
#define	USC_FFBSTS	0x5c		/* dsc: ffb status */
#define	USC_MCCTRL0	0x60		/* usc: memory control 0 */
#define	USC_MCCTRL1	0x64		/* usc: memory control 1 */
#define	USC_CCDIAG	0x70		/* dsc: */
#define	USC_CCVEC	0x74		/* dsc: */
#define	USC_CCFLT	0x78		/* dsc: */
#define	USC_CCPRC	0x7c		/* dsc: */
#define	USC_MEM0	0x80		/* dsc: mem control 0 */
#define	USC_MEM1	0x84		/* dsc: RAS control */
#define	USC_MEM2	0x88		/* dsc: CAS_RD control */
#define	USC_MEM3	0x8c		/* dsc: Bank_Sel control */
#define	USC_MEM4	0x90		/* dsc: BMX_Buffer control */
#define	USC_MEM5	0x94		/* dsc: CAS_WR control */
#define	USC_MEM6	0x98		/* dsc: Phase_Level control */
#define	USC_MEM7	0x9c		/* dsc: SIMM_Busy_Rd control */
#define	USC_MEM8	0xa0		/* dsc: Count_Control */
#define	USC_MEM9	0xa4		/* dsc: Refresh control */
#define	USC_MEMA	0xa8		/* dsc: Row control */
#define	USC_MEMB	0xac		/* dsc: guess! */

#define	USC_CTRL_POR	0x80000000	/* power on reset */
#define	USC_CTRL_SFTPOR	0x40000000	/* last reset was soft */
#define	USC_CTRL_XIR	0x20000000	/* initiate XIR */
#define	USC_CTRL_BPOR	0x10000000	/* last reset was scan */
#define	USC_CTRL_BXIR	0x08000000	/* last reset was xir */
#define	USC_CTRL_WAKEUP	0x04000000	/* last reset was wakeup */
#define	USC_CTRL_FATAL	0x02000000	/* fatal error detect */
#define	USC_CTRL_IAP	0x00800000	/* inv parity on addr bus */
#define	USC_CTRL_ENWKUP	0x00400000	/* enable wakeup POR */

#define	USC_ID_JEDEC	0xffff0000	/* JEDEC ID */
#define	USC_ID_UPANUM_M	0x0000f000	/* #UPA ports supported mask */
#define	USC_ID_UPANUM_S	12		/* #UPA ports supported shift */
#define	USC_ID_IMPL_M	0x000000f0	/* implementation # mask */
#define	USC_ID_IMPL_S	4		/* implementation # shift */
#define	USC_ID_VERS_M	0x0000000f	/* version mask */
#define	USC_ID_VERS_S	0		/* version shift */

#define	USC_PCTRL_CLR1	0x00008000	/* clear cntr in SEL1 */
#define	USC_PCTRL_SEL1	0x00000f00	/* event source 1 */
#define	USC_PCTRL_CLR0	0x00000080	/* clear cntrl in SEL0 */
#define	USC_PCTRL_SEL0	0x0000000f	/* event source 0 */

/* Event sources, counter 0 */
#define	SEL0_SYSCK	0x00000000	/*  system clock count */
#define	SEL0_PRALL	0x00000001	/*  prequests, all sources */
#define	SEL0_PRP0	0x00000002	/*  prequests, proc 0 */
#define	SEL0_PRUS	0x00000004	/*  prequests, u2s */
#define	SEL0_128BUSY	0x00000005	/*  # of cycles 128bit UPA busy */
#define	SEL0_64BUSY	0x00000006	/*  # of cycles 64bit UPA busy */
#define	SEL0_PIOSTALL	0x00000007	/*  # cycles stalled during PIO */
#define	SEL0_MEMREQ	0x00000008	/*  # memory requests issued */
#define	SEL0_MCBUSY	0x00000009	/*  # cycles memory controller busy */
#define	SEL0_PENDSTALL	0x0000000a	/*  # stalls pending xact scbd hit */
#define	SEL0_CWMRP0	0x0000000b	/*  # coherent write miss req, prc0 */
#define	SEL0_CWMRP1	0x0000000c	/*  # coherent write miss req, prc1 */
#define	SEL0_CIT	0x0000000d	/*  # coherent intervene xacts */
#define	SEL0_DACT	0x0000000e	/*  # data transactions from u2s */
#define	SEL0_CRXI	0x0000000f	/*  # coherent read xacts issued */

/* Event sources, counter 1 */
#define	SEL1_SYSCK	0x00000000	/*  system clock count */
#define	SEL1_PRALL	0x00000001	/*  prequests, all sources */
#define	SEL1_PRP0	0x00000002	/*  prequests, proc 0 */
#define	SEL1_PRUS	0x00000004	/*  prequests, u2s */
#define	SEL1_RDP0	0x00000005	/*  read reqs from P0 */
#define	SEL1_CRMP0	0x00000006	/*  coherent read misses from P0 */
#define	SEL1_PIOP0	0x00000007	/*  PIO accesses from P0 */
#define	SEL1_MRI	0x00000008	/*  memory reqs issued */
#define	SEL1_MRC	0x00000009	/*  memory reqs complete */
#define	SEL1_RDP1	0x0000000a	/*  read reqs from P1 */
#define	SEL1_CRMP1	0x0000000b	/*  coherent read misses from P1 */
#define	SEL1_PIOP1	0x0000000c	/*  PIO accesses from P1 */
#define	SEL1_CWXI	0x0000000d	/*  coherent write xacts issued */
#define	SEL1_DXU	0x0000000e 	/*  # data xacts from u2s */

/* Port config (USC_P0CFG, USC_P1CFG, USC_SYSIOCFG, USC_FFBCFG) */
#define	USC_PCFG_MD	0x80000000	/* master disable */
#define	USC_PCFG_SSLP	0x40000000	/* slave sleep */
#define	USC_PCFG_SPRQS	0x0f000000	/* slave prequest queue size */
#define	USC_PCFG_SIQS	0x00030000	/* slave interrupt queue size */
#define	USC_PCFG_SQEN	0x00008000	/* qualifies writes to SPRQS,SIQS */
#define	USC_PCFG_ONER	0x00004000	/* one read */

/* Port status (USC_P0STS, USC_P1STS, USC_SYSIOSTS, USC_FFBSTS) */
#define	USC_PSTS_FATAL	0x80000000	/* fatal error detected */
#define	USC_PSTS_IADDR	0x40000000	/* invalid address */
#define	USC_PSTS_IPORT	0x20000000	/* invalid port */
#define	USC_PSTS_IPRTY	0x10000000	/* parity error */
#define	USC_PSTS_MC0OF	0x08000000	/* master class 0 overflow */
#define	USC_PSTS_MC1OF	0x04000000	/* master class 1 overflow */
#define	USC_PSTS_MCQ0	0x03800000	/* # reqs before mc0 overflow */
#define	USC_PSTS_MC1Q	0x00700000	/* # reqs before mc1 overflow */

/* usc: memory control 0 */
#define	USC_MC0_REFEN	0x80000000	/* refresh enable */
#define	USC_MC0_SIMP	0x0000ff00	/* simms present */
#define	USC_MC0_REFI	0x000000ff	/* refresh interval */

/* usc: memory control 1 */
#define	USC_MC1_CSR	0x00001000	/* CAS-to-RAS delay for CBR ref cyc */
#define	USC_MC1_WPC1	0x00000c00	/* page cycle 1 write */
#define	USC_MC1_RCD	0x00000200	/* RAS-to-CAS delay */
#define	USC_MC1_CP	0x00000100	/* CAS precharge */
#define	USC_MC1_RP	0x000000c0	/* RAS precharge */
#define	USC_MC1_RAS	0x00000030	/* length of RAS for precharge */
#define	USC_MC1_PC0	0x0000000c	/* page cycle 0 */
#define	USC_MC1_PC1	0x00000003	/* page cycle 1 */

/* dsc: memory control 0 */
#define	USC_MEM0_REFE	0x80000000	/* refresh enable */
#define	USC_MEM0_FSME	0x10000000	/* fsm error */
#define	USC_MEM0_PPE	0x08000000	/* ping-pong buffer error */
#define	USC_MEM0_DPSE	0x04000000	/* data path scheduler error */
#define	USC_MEM0_MCE	0x02000000	/* memory controller error */
#define	USC_MEM0_MRE	0x01000000	/* missed refresh error */
#define	USC_MEM0_RPMC	0x00800000	/* RAS Phi 0 cookie for refresh */
#define	USC_MEM0_RWMC	0x00400000	/* RAS Phi 0 cookie for writers */
#define	USC_MEM0_SW0	0x001f0000	/* stretch count for first write */
#define	USC_MEM0_SP	0x00000f00	/* simm present mask */
#define	USC_MEM0_REFI	0x000000ff	/* refresh interval */

/* cache coherence diagnostic: USC_CCDIAG */
#define	USC_CCDIAG_SNP	0xffff0000	/* SRAM address */
#define	USC_CCDIAG_DME	0x00008000	/* enable writes to DTAG */

/* cache coherence snoop vector: USC_CCVEC */
#define	USC_CCVEC_TAG	0x7ff80000	/* tag portion of SRAM data */
#define	USC_CCVEC_ST	0x00060000	/* state portion of SRAM data */
#define	USC_CCVEC_PAR	0x00010000	/* parity portion of SRAM data */

/* cache coherence fault: USC_CCFLT */
#define	USC_CCFLT_PERR0	0x80000000	/* dual tag parity error, proc 0 */
#define	USC_CCFLT_CERR0	0x40000000	/* coherence error, proc 0 */
#define	USC_CCFLT_PERR1	0x20000000	/* dual tag parity error, proc 1 */
#define	USC_CCFLT_CERR1	0x10000000	/* coherence error, proc 1 */
#define	USC_CCFLT_IDX	0x0fffe000	/* index of fault */

/* cache coherence processor index: USC_CCPRC */
#define	USC_CCPRC_PIDX	0x7fffffff	/* address mask for ports 1 & 2 */
