/* $Id: sbi.h,v 1.2 1998/03/09 14:04:48 jj Exp $
 * sbi.h:  SBI (Sbus Interface on sun4d) definitions
 *
 * Copyright (C) 1997 Jakub Jelinek <jj@sunsite.mff.cuni.cz>
 */

#ifndef _SPARC_SBI_H
#define _SPARC_SBI_H

#include <asm/obio.h>

/* SBI */
struct sbi_regs {
/* 0x0000 */	u32		cid;		/* Component ID */
/* 0x0004 */	u32		ctl;		/* Control */
/* 0x0008 */	u32		status;		/* Status */
		u32		_unused1;
		
/* 0x0010 */	u32		cfg0;		/* Slot0 config reg */
/* 0x0014 */	u32		cfg1;		/* Slot1 config reg */
/* 0x0018 */	u32		cfg2;		/* Slot2 config reg */
/* 0x001c */	u32		cfg3;		/* Slot3 config reg */

/* 0x0020 */	u32		stb0;		/* Streaming buf control for slot 0 */
/* 0x0024 */	u32		stb1;		/* Streaming buf control for slot 1 */
/* 0x0028 */	u32		stb2;		/* Streaming buf control for slot 2 */
/* 0x002c */	u32		stb3;		/* Streaming buf control for slot 3 */

/* 0x0030 */	u32		intr_state;	/* Interrupt state */
/* 0x0034 */	u32		intr_tid;	/* Interrupt target ID */
/* 0x0038 */	u32		intr_diag;	/* Interrupt diagnostics */
};

#define SBI_CID			0x02800000
#define SBI_CTL			0x02800004
#define SBI_STATUS		0x02800008
#define SBI_CFG0		0x02800010
#define SBI_CFG1		0x02800014
#define SBI_CFG2		0x02800018
#define SBI_CFG3		0x0280001c
#define SBI_STB0		0x02800020
#define SBI_STB1		0x02800024
#define SBI_STB2		0x02800028
#define SBI_STB3		0x0280002c
#define SBI_INTR_STATE		0x02800030
#define SBI_INTR_TID		0x02800034
#define SBI_INTR_DIAG		0x02800038

/* Burst bits for 8, 16, 32, 64 are in cfgX registers at bits 2, 3, 4, 5 respectively */
#define SBI_CFG_BURST_MASK	0x0000001e

/* How to make devid from sbi no */
#define SBI2DEVID(sbino) ((sbino<<4)|2)

/* intr_state has 4 bits for slots 0 .. 3 and these bits are repeated for each sbus irq level
 *
 *		   +-------+-------+-------+-------+-------+-------+-------+-------+
 *  SBUS IRQ LEVEL |   7   |   6   |   5   |   4   |   3   |   2   |   1   |       |
 *		   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ Reser |
 *  SLOT #         |3|2|1|0|3|2|1|0|3|2|1|0|3|2|1|0|3|2|1|0|3|2|1|0|3|2|1|0|  ved  |
 *                 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-------+
 *  Bits           31      27      23      19      15      11      7       3      0
 */


#ifndef __ASSEMBLY__

static inline int acquire_sbi(int devid, int mask)
{
	__asm__ __volatile__ ("swapa [%2] %3, %0" :
			      "=r" (mask) :
			      "0" (mask),
			      "r" (ECSR_DEV_BASE(devid) | SBI_INTR_STATE),
			      "i" (ASI_M_CTL));
	return mask;
}

static inline void release_sbi(int devid, int mask)
{
	__asm__ __volatile__ ("sta %0, [%1] %2" : :
			      "r" (mask),
			      "r" (ECSR_DEV_BASE(devid) | SBI_INTR_STATE),
			      "i" (ASI_M_CTL));
}

static inline void set_sbi_tid(int devid, int targetid)
{
	__asm__ __volatile__ ("sta %0, [%1] %2" : :
			      "r" (targetid),
			      "r" (ECSR_DEV_BASE(devid) | SBI_INTR_TID),
			      "i" (ASI_M_CTL));
}

static inline int get_sbi_ctl(int devid, int cfgno)
{
	int cfg;
	
	__asm__ __volatile__ ("lda [%1] %2, %0" :
			      "=r" (cfg) :
			      "r" ((ECSR_DEV_BASE(devid) | SBI_CFG0) + (cfgno<<2)),
			      "i" (ASI_M_CTL));
	return cfg;
}

static inline void set_sbi_ctl(int devid, int cfgno, int cfg)
{
	__asm__ __volatile__ ("sta %0, [%1] %2" : :
			      "r" (cfg),
			      "r" ((ECSR_DEV_BASE(devid) | SBI_CFG0) + (cfgno<<2)),
			      "i" (ASI_M_CTL));
}

#endif /* !__ASSEMBLY__ */

#endif /* !(_SPARC_SBI_H) */
