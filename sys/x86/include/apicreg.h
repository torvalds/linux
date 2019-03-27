/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996, by Peter Wemm and Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _X86_APICREG_H_
#define _X86_APICREG_H_

/*
 * Local && I/O APIC definitions.
 */

/*
 * Pentium P54C+ Built-in APIC
 * (Advanced programmable Interrupt Controller)
 * 
 * Base Address of Built-in APIC in memory location
 * is 0xfee00000.
 * 
 * Map of APIC Registers:
 * 
 * Offset (hex)    Description                     Read/Write state
 * 000             Reserved
 * 010             Reserved
 * 020 ID          Local APIC ID                   R/W
 * 030 VER         Local APIC Version              R
 * 040             Reserved
 * 050             Reserved
 * 060             Reserved
 * 070             Reserved
 * 080             Task Priority Register          R/W
 * 090             Arbitration Priority Register   R
 * 0A0             Processor Priority Register     R
 * 0B0             EOI Register                    W
 * 0C0 RRR         Remote read                     R
 * 0D0             Logical Destination             R/W
 * 0E0             Destination Format Register     0..27 R;  28..31 R/W
 * 0F0 SVR         Spurious Interrupt Vector Reg.  0..3  R;  4..9   R/W
 * 100             ISR  000-031                    R
 * 110             ISR  032-063                    R
 * 120             ISR  064-095                    R
 * 130             ISR  095-128                    R
 * 140             ISR  128-159                    R
 * 150             ISR  160-191                    R
 * 160             ISR  192-223                    R
 * 170             ISR  224-255                    R
 * 180             TMR  000-031                    R
 * 190             TMR  032-063                    R
 * 1A0             TMR  064-095                    R
 * 1B0             TMR  095-128                    R
 * 1C0             TMR  128-159                    R
 * 1D0             TMR  160-191                    R
 * 1E0             TMR  192-223                    R
 * 1F0             TMR  224-255                    R
 * 200             IRR  000-031                    R
 * 210             IRR  032-063                    R
 * 220             IRR  064-095                    R
 * 230             IRR  095-128                    R
 * 240             IRR  128-159                    R
 * 250             IRR  160-191                    R
 * 260             IRR  192-223                    R
 * 270             IRR  224-255                    R
 * 280             Error Status Register           R
 * 290             Reserved
 * 2A0             Reserved
 * 2B0             Reserved
 * 2C0             Reserved
 * 2D0             Reserved
 * 2E0             Reserved
 * 2F0             Local Vector Table (CMCI)       R/W
 * 300 ICR_LOW     Interrupt Command Reg. (0-31)   R/W
 * 310 ICR_HI      Interrupt Command Reg. (32-63)  R/W
 * 320             Local Vector Table (Timer)      R/W
 * 330             Local Vector Table (Thermal)    R/W (PIV+)
 * 340             Local Vector Table (Performance) R/W (P6+)
 * 350 LVT1        Local Vector Table (LINT0)      R/W
 * 360 LVT2        Local Vector Table (LINT1)      R/W
 * 370 LVT3        Local Vector Table (ERROR)      R/W
 * 380             Initial Count Reg. for Timer    R/W
 * 390             Current Count of Timer          R
 * 3A0             Reserved
 * 3B0             Reserved
 * 3C0             Reserved
 * 3D0             Reserved
 * 3E0             Timer Divide Configuration Reg. R/W
 * 3F0             Reserved
 */


/******************************************************************************
 * global defines, etc.
 */


/******************************************************************************
 * LOCAL APIC structure
 */

#ifndef LOCORE
#include <sys/types.h>

#define PAD3	int : 32; int : 32; int : 32
#define PAD4	int : 32; int : 32; int : 32; int : 32

struct LAPIC {
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t id;		PAD3;
	u_int32_t version;	PAD3;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t tpr;		PAD3;
	u_int32_t apr;		PAD3;
	u_int32_t ppr;		PAD3;
	u_int32_t eoi;		PAD3;
	/* reserved */		PAD4;
	u_int32_t ldr;		PAD3;
	u_int32_t dfr;		PAD3;
	u_int32_t svr;		PAD3;
	u_int32_t isr0;		PAD3;
	u_int32_t isr1;		PAD3;
	u_int32_t isr2;		PAD3;
	u_int32_t isr3;		PAD3;
	u_int32_t isr4;		PAD3;
	u_int32_t isr5;		PAD3;
	u_int32_t isr6;		PAD3;
	u_int32_t isr7;		PAD3;
	u_int32_t tmr0;		PAD3;
	u_int32_t tmr1;		PAD3;
	u_int32_t tmr2;		PAD3;
	u_int32_t tmr3;		PAD3;
	u_int32_t tmr4;		PAD3;
	u_int32_t tmr5;		PAD3;
	u_int32_t tmr6;		PAD3;
	u_int32_t tmr7;		PAD3;
	u_int32_t irr0;		PAD3;
	u_int32_t irr1;		PAD3;
	u_int32_t irr2;		PAD3;
	u_int32_t irr3;		PAD3;
	u_int32_t irr4;		PAD3;
	u_int32_t irr5;		PAD3;
	u_int32_t irr6;		PAD3;
	u_int32_t irr7;		PAD3;
	u_int32_t esr;		PAD3;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t lvt_cmci;	PAD3;
	u_int32_t icr_lo;	PAD3;
	u_int32_t icr_hi;	PAD3;
	u_int32_t lvt_timer;	PAD3;
	u_int32_t lvt_thermal;	PAD3;
	u_int32_t lvt_pcint;	PAD3;
	u_int32_t lvt_lint0;	PAD3;
	u_int32_t lvt_lint1;	PAD3;
	u_int32_t lvt_error;	PAD3;
	u_int32_t icr_timer;	PAD3;
	u_int32_t ccr_timer;	PAD3;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	/* reserved */		PAD4;
	u_int32_t dcr_timer;	PAD3;
	/* reserved */		PAD4;
};

typedef struct LAPIC lapic_t;

enum LAPIC_REGISTERS {
	LAPIC_ID	= 0x2,
	LAPIC_VERSION	= 0x3,
	LAPIC_TPR	= 0x8,
	LAPIC_APR	= 0x9,
	LAPIC_PPR	= 0xa,
	LAPIC_EOI	= 0xb,
	LAPIC_LDR	= 0xd,
	LAPIC_DFR	= 0xe, /* Not in x2APIC */
	LAPIC_SVR	= 0xf,
	LAPIC_ISR0	= 0x10,
	LAPIC_ISR1	= 0x11,
	LAPIC_ISR2	= 0x12,
	LAPIC_ISR3	= 0x13,
	LAPIC_ISR4	= 0x14,
	LAPIC_ISR5	= 0x15,
	LAPIC_ISR6	= 0x16,
	LAPIC_ISR7	= 0x17,
	LAPIC_TMR0	= 0x18,
	LAPIC_TMR1	= 0x19,
	LAPIC_TMR2	= 0x1a,
	LAPIC_TMR3	= 0x1b,
	LAPIC_TMR4	= 0x1c,
	LAPIC_TMR5	= 0x1d,
	LAPIC_TMR6	= 0x1e,
	LAPIC_TMR7	= 0x1f,
	LAPIC_IRR0	= 0x20,
	LAPIC_IRR1	= 0x21,
	LAPIC_IRR2	= 0x22,
	LAPIC_IRR3	= 0x23,
	LAPIC_IRR4	= 0x24,
	LAPIC_IRR5	= 0x25,
	LAPIC_IRR6	= 0x26,
	LAPIC_IRR7	= 0x27,
	LAPIC_ESR	= 0x28,
	LAPIC_LVT_CMCI	= 0x2f,
	LAPIC_ICR_LO	= 0x30,
	LAPIC_ICR_HI	= 0x31, /* Not in x2APIC */
	LAPIC_LVT_TIMER	= 0x32,
	LAPIC_LVT_THERMAL = 0x33,
	LAPIC_LVT_PCINT	= 0x34,
	LAPIC_LVT_LINT0	= 0x35,
	LAPIC_LVT_LINT1	= 0x36,
	LAPIC_LVT_ERROR	= 0x37,
	LAPIC_ICR_TIMER	= 0x38,
	LAPIC_CCR_TIMER	= 0x39,
	LAPIC_DCR_TIMER	= 0x3e,
	LAPIC_SELF_IPI	= 0x3f, /* Only in x2APIC */
	LAPIC_EXT_FEATURES = 0x40, /* AMD */
	LAPIC_EXT_CTRL	= 0x41, /* AMD */
	LAPIC_EXT_SEOI	= 0x42, /* AMD */
	LAPIC_EXT_IER0	= 0x48, /* AMD */
	LAPIC_EXT_IER1	= 0x49, /* AMD */
	LAPIC_EXT_IER2	= 0x4a, /* AMD */
	LAPIC_EXT_IER3	= 0x4b, /* AMD */
	LAPIC_EXT_IER4	= 0x4c, /* AMD */
	LAPIC_EXT_IER5	= 0x4d, /* AMD */
	LAPIC_EXT_IER6	= 0x4e, /* AMD */
	LAPIC_EXT_IER7	= 0x4f, /* AMD */
	LAPIC_EXT_LVT0	= 0x50, /* AMD */
	LAPIC_EXT_LVT1	= 0x51, /* AMD */
	LAPIC_EXT_LVT2	= 0x52, /* AMD */
	LAPIC_EXT_LVT3	= 0x53, /* AMD */
};

#define	LAPIC_MEM_MUL	0x10

/*
 * Although some registers are available on AMD processors only,
 * it's not a big waste to reserve them on all platforms.
 * However, we need to watch out for this space being assigned for
 * non-APIC purposes in the future processor models.
 */
#define	LAPIC_MEM_REGION ((LAPIC_EXT_LVT3 + 1) * LAPIC_MEM_MUL)

/******************************************************************************
 * I/O APIC structure
 */

struct IOAPIC {
	u_int32_t ioregsel;	PAD3;
	u_int32_t iowin;	PAD3;
};

typedef struct IOAPIC ioapic_t;

#undef PAD4
#undef PAD3

#endif  /* !LOCORE */


/******************************************************************************
 * various code 'logical' values
 */

/******************************************************************************
 * LOCAL APIC defines
 */

/* default physical locations of LOCAL (CPU) APICs */
#define DEFAULT_APIC_BASE	0xfee00000

/* constants relating to APIC ID registers */
#define APIC_ID_MASK		0xff000000
#define	APIC_ID_SHIFT		24
#define	APIC_ID_CLUSTER		0xf0
#define	APIC_ID_CLUSTER_ID	0x0f
#define	APIC_MAX_CLUSTER	0xe
#define	APIC_MAX_INTRACLUSTER_ID 3
#define	APIC_ID_CLUSTER_SHIFT	4

/* fields in VER */
#define APIC_VER_VERSION	0x000000ff
#define APIC_VER_MAXLVT		0x00ff0000
#define MAXLVTSHIFT		16
#define APIC_VER_EOI_SUPPRESSION 0x01000000
#define APIC_VER_AMD_EXT_SPACE	0x80000000

/* fields in LDR */
#define	APIC_LDR_RESERVED	0x00ffffff

/* fields in DFR */
#define	APIC_DFR_RESERVED	0x0fffffff
#define	APIC_DFR_MODEL_MASK	0xf0000000
#define	APIC_DFR_MODEL_FLAT	0xf0000000
#define	APIC_DFR_MODEL_CLUSTER	0x00000000

/* fields in SVR */
#define APIC_SVR_VECTOR		0x000000ff
#define APIC_SVR_VEC_PROG	0x000000f0
#define APIC_SVR_VEC_FIX	0x0000000f
#define APIC_SVR_ENABLE		0x00000100
# define APIC_SVR_SWDIS		0x00000000
# define APIC_SVR_SWEN		0x00000100
#define APIC_SVR_FOCUS		0x00000200
# define APIC_SVR_FEN		0x00000000
# define APIC_SVR_FDIS		0x00000200
#define APIC_SVR_EOI_SUPPRESSION 0x00001000

/* fields in TPR */
#define APIC_TPR_PRIO		0x000000ff
# define APIC_TPR_INT		0x000000f0
# define APIC_TPR_SUB		0x0000000f

/* fields in ESR */
#define	APIC_ESR_SEND_CS_ERROR		0x00000001
#define	APIC_ESR_RECEIVE_CS_ERROR	0x00000002
#define	APIC_ESR_SEND_ACCEPT		0x00000004
#define	APIC_ESR_RECEIVE_ACCEPT		0x00000008
#define	APIC_ESR_SEND_ILLEGAL_VECTOR	0x00000020
#define	APIC_ESR_RECEIVE_ILLEGAL_VECTOR	0x00000040
#define	APIC_ESR_ILLEGAL_REGISTER	0x00000080

/* fields in ICR_LOW */
#define APIC_VECTOR_MASK	0x000000ff

#define APIC_DELMODE_MASK	0x00000700
# define APIC_DELMODE_FIXED	0x00000000
# define APIC_DELMODE_LOWPRIO	0x00000100
# define APIC_DELMODE_SMI	0x00000200
# define APIC_DELMODE_RR	0x00000300
# define APIC_DELMODE_NMI	0x00000400
# define APIC_DELMODE_INIT	0x00000500
# define APIC_DELMODE_STARTUP	0x00000600
# define APIC_DELMODE_RESV	0x00000700

#define APIC_DESTMODE_MASK	0x00000800
# define APIC_DESTMODE_PHY	0x00000000
# define APIC_DESTMODE_LOG	0x00000800

#define APIC_DELSTAT_MASK	0x00001000
# define APIC_DELSTAT_IDLE	0x00000000
# define APIC_DELSTAT_PEND	0x00001000

#define APIC_RESV1_MASK		0x00002000

#define APIC_LEVEL_MASK		0x00004000
# define APIC_LEVEL_DEASSERT	0x00000000
# define APIC_LEVEL_ASSERT	0x00004000

#define APIC_TRIGMOD_MASK	0x00008000
# define APIC_TRIGMOD_EDGE	0x00000000
# define APIC_TRIGMOD_LEVEL	0x00008000

#define APIC_RRSTAT_MASK	0x00030000
# define APIC_RRSTAT_INVALID	0x00000000
# define APIC_RRSTAT_INPROG	0x00010000
# define APIC_RRSTAT_VALID	0x00020000
# define APIC_RRSTAT_RESV	0x00030000

#define APIC_DEST_MASK		0x000c0000
# define APIC_DEST_DESTFLD	0x00000000
# define APIC_DEST_SELF		0x00040000
# define APIC_DEST_ALLISELF	0x00080000
# define APIC_DEST_ALLESELF	0x000c0000

#define APIC_RESV2_MASK		0xfff00000

#define	APIC_ICRLO_RESV_MASK	(APIC_RESV1_MASK | APIC_RESV2_MASK)

/* fields in LVT1/2 */
#define APIC_LVT_VECTOR		0x000000ff
#define APIC_LVT_DM		0x00000700
# define APIC_LVT_DM_FIXED	0x00000000
# define APIC_LVT_DM_SMI	0x00000200
# define APIC_LVT_DM_NMI	0x00000400
# define APIC_LVT_DM_INIT	0x00000500
# define APIC_LVT_DM_EXTINT	0x00000700
#define APIC_LVT_DS		0x00001000
#define APIC_LVT_IIPP		0x00002000
#define APIC_LVT_IIPP_INTALO	0x00002000
#define APIC_LVT_IIPP_INTAHI	0x00000000
#define APIC_LVT_RIRR		0x00004000
#define APIC_LVT_TM		0x00008000
#define APIC_LVT_M		0x00010000


/* fields in LVT Timer */
#define APIC_LVTT_VECTOR	0x000000ff
#define APIC_LVTT_DS		0x00001000
#define APIC_LVTT_M		0x00010000
#define APIC_LVTT_TM		0x00060000
# define APIC_LVTT_TM_ONE_SHOT	0x00000000
# define APIC_LVTT_TM_PERIODIC	0x00020000
# define APIC_LVTT_TM_TSCDLT	0x00040000
# define APIC_LVTT_TM_RSRV	0x00060000

/* APIC timer current count */
#define	APIC_TIMER_MAX_COUNT	0xffffffff

/* fields in TDCR */
#define APIC_TDCR_2		0x00
#define APIC_TDCR_4		0x01
#define APIC_TDCR_8		0x02
#define APIC_TDCR_16		0x03
#define APIC_TDCR_32		0x08
#define APIC_TDCR_64		0x09
#define APIC_TDCR_128		0x0a
#define APIC_TDCR_1		0x0b

/* Constants related to AMD Extended APIC Features Register */
#define	APIC_EXTF_ELVT_MASK	0x00ff0000
#define	APIC_EXTF_ELVT_SHIFT	16
#define	APIC_EXTF_EXTID_CAP	0x00000004
#define	APIC_EXTF_SEIO_CAP	0x00000002
#define	APIC_EXTF_IER_CAP	0x00000001

/* LVT table indices */
#define	APIC_LVT_LINT0		0
#define	APIC_LVT_LINT1		1
#define	APIC_LVT_TIMER		2
#define	APIC_LVT_ERROR		3
#define	APIC_LVT_PMC		4
#define	APIC_LVT_THERMAL	5
#define	APIC_LVT_CMCI		6
#define	APIC_LVT_MAX		APIC_LVT_CMCI

/* AMD extended LVT constants, seem to be assigned by fiat */
#define	APIC_ELVT_IBS		0 /* Instruction based sampling */
#define	APIC_ELVT_MCA		1 /* MCE thresholding */
#define	APIC_ELVT_DEI		2 /* Deferred error interrupt */
#define	APIC_ELVT_SBI		3 /* Sideband interface */
#define	APIC_ELVT_MAX		APIC_ELVT_SBI

/******************************************************************************
 * I/O APIC defines
 */

/* default physical locations of an IO APIC */
#define DEFAULT_IO_APIC_BASE	0xfec00000

/* window register offset */
#define IOAPIC_WINDOW		0x10
#define IOAPIC_EOIR		0x40

#define	IOAPIC_WND_SIZE		0x50

/* indexes into IO APIC */
#define IOAPIC_ID		0x00
#define IOAPIC_VER		0x01
#define IOAPIC_ARB		0x02
#define IOAPIC_REDTBL		0x10
#define IOAPIC_REDTBL0		IOAPIC_REDTBL
#define IOAPIC_REDTBL1		(IOAPIC_REDTBL+0x02)
#define IOAPIC_REDTBL2		(IOAPIC_REDTBL+0x04)
#define IOAPIC_REDTBL3		(IOAPIC_REDTBL+0x06)
#define IOAPIC_REDTBL4		(IOAPIC_REDTBL+0x08)
#define IOAPIC_REDTBL5		(IOAPIC_REDTBL+0x0a)
#define IOAPIC_REDTBL6		(IOAPIC_REDTBL+0x0c)
#define IOAPIC_REDTBL7		(IOAPIC_REDTBL+0x0e)
#define IOAPIC_REDTBL8		(IOAPIC_REDTBL+0x10)
#define IOAPIC_REDTBL9		(IOAPIC_REDTBL+0x12)
#define IOAPIC_REDTBL10		(IOAPIC_REDTBL+0x14)
#define IOAPIC_REDTBL11		(IOAPIC_REDTBL+0x16)
#define IOAPIC_REDTBL12		(IOAPIC_REDTBL+0x18)
#define IOAPIC_REDTBL13		(IOAPIC_REDTBL+0x1a)
#define IOAPIC_REDTBL14		(IOAPIC_REDTBL+0x1c)
#define IOAPIC_REDTBL15		(IOAPIC_REDTBL+0x1e)
#define IOAPIC_REDTBL16		(IOAPIC_REDTBL+0x20)
#define IOAPIC_REDTBL17		(IOAPIC_REDTBL+0x22)
#define IOAPIC_REDTBL18		(IOAPIC_REDTBL+0x24)
#define IOAPIC_REDTBL19		(IOAPIC_REDTBL+0x26)
#define IOAPIC_REDTBL20		(IOAPIC_REDTBL+0x28)
#define IOAPIC_REDTBL21		(IOAPIC_REDTBL+0x2a)
#define IOAPIC_REDTBL22		(IOAPIC_REDTBL+0x2c)
#define IOAPIC_REDTBL23		(IOAPIC_REDTBL+0x2e)

/* fields in VER */
#define IOART_VER_VERSION	0x000000ff
#define IOART_VER_MAXREDIR	0x00ff0000
#define MAXREDIRSHIFT		16

/*
 * fields in the IO APIC's redirection table entries
 */
#define IOART_DEST	APIC_ID_MASK	/* broadcast addr: all APICs */

#define IOART_RESV	0x00fe0000	/* reserved */

#define IOART_INTMASK	0x00010000	/* R/W: INTerrupt mask */
# define IOART_INTMCLR	0x00000000	/*       clear, allow INTs */
# define IOART_INTMSET	0x00010000	/*       set, inhibit INTs */

#define IOART_TRGRMOD	0x00008000	/* R/W: trigger mode */
# define IOART_TRGREDG	0x00000000	/*       edge */
# define IOART_TRGRLVL	0x00008000	/*       level */

#define IOART_REM_IRR	0x00004000	/* RO: remote IRR */

#define IOART_INTPOL	0x00002000	/* R/W: INT input pin polarity */
# define IOART_INTAHI	0x00000000	/*      active high */
# define IOART_INTALO	0x00002000	/*      active low */

#define IOART_DELIVS	0x00001000	/* RO: delivery status */

#define IOART_DESTMOD	0x00000800	/* R/W: destination mode */
# define IOART_DESTPHY	0x00000000	/*      physical */
# define IOART_DESTLOG	0x00000800	/*      logical */

#define IOART_DELMOD	0x00000700	/* R/W: delivery mode */
# define IOART_DELFIXED	0x00000000	/*       fixed */
# define IOART_DELLOPRI	0x00000100	/*       lowest priority */
# define IOART_DELSMI	0x00000200	/*       System Management INT */
# define IOART_DELRSV1	0x00000300	/*       reserved */
# define IOART_DELNMI	0x00000400	/*       NMI signal */
# define IOART_DELINIT	0x00000500	/*       INIT signal */
# define IOART_DELRSV2	0x00000600	/*       reserved */
# define IOART_DELEXINT	0x00000700	/*       External INTerrupt */

#define IOART_INTVEC	0x000000ff	/* R/W: INTerrupt vector field */

#endif /* _X86_APICREG_H_ */
