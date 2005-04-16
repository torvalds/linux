/*
 * include/asm/m32700ut_lan.h
 *
 * M32700UT-LAN board
 *
 * Copyright (c) 2002	Takeo Takahashi
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * $Id$
 */

#ifndef _M32700UT_M32700UT_LAN_H
#define _M32700UT_M32700UT_LAN_H

#include <linux/config.h>

#ifndef __ASSEMBLY__
/*
 * C functions use non-cache address.
 */
#define M32700UT_LAN_BASE	(0x10000000 /* + NONCACHE_OFFSET */)
#else
#define M32700UT_LAN_BASE	(0x10000000 + NONCACHE_OFFSET)
#endif	/* __ASSEMBLY__ */

/* ICU
 *  ICUISTS:	status register
 *  ICUIREQ0: 	request register
 *  ICUIREQ1: 	request register
 *  ICUCR3:	control register for CFIREQ# interrupt
 *  ICUCR4:	control register for CFC Card insert interrupt
 *  ICUCR5:	control register for CFC Card eject interrupt
 *  ICUCR6:	control register for external interrupt
 *  ICUCR11:	control register for MMC Card insert/eject interrupt
 *  ICUCR13:	control register for SC error interrupt
 *  ICUCR14:	control register for SC receive interrupt
 *  ICUCR15:	control register for SC send interrupt
 *  ICUCR16:	control register for SIO0 receive interrupt
 *  ICUCR17:	control register for SIO0 send interrupt
 */
#define M32700UT_LAN_IRQ_LAN	(M32700UT_LAN_PLD_IRQ_BASE + 1)	/* LAN */
#define M32700UT_LAN_IRQ_I2C	(M32700UT_LAN_PLD_IRQ_BASE + 3)	/* I2C */

#define M32700UT_LAN_ICUISTS	__reg16(M32700UT_LAN_BASE + 0xc0002)
#define M32700UT_LAN_ICUISTS_VECB_MASK	(0xf000)
#define M32700UT_LAN_VECB(x)	((x) & M32700UT_LAN_ICUISTS_VECB_MASK)
#define M32700UT_LAN_ICUISTS_ISN_MASK	(0x07c0)
#define M32700UT_LAN_ICUISTS_ISN(x)	((x) & M32700UT_LAN_ICUISTS_ISN_MASK)
#define M32700UT_LAN_ICUIREQ0	__reg16(M32700UT_LAN_BASE + 0xc0004)
#define M32700UT_LAN_ICUCR1	__reg16(M32700UT_LAN_BASE + 0xc0010)
#define M32700UT_LAN_ICUCR3	__reg16(M32700UT_LAN_BASE + 0xc0014)

/*
 * AR register on PLD
 */
#define ARVCR0		__reg32(M32700UT_LAN_BASE + 0x40000)
#define ARVCR0_VDS		0x00080000
#define ARVCR0_RST		0x00010000
#define ARVCR1		__reg32(M32700UT_LAN_BASE + 0x40004)
#define ARVCR1_QVGA		0x02000000
#define ARVCR1_NORMAL		0x01000000
#define ARVCR1_HIEN		0x00010000
#define ARVHCOUNT	__reg32(M32700UT_LAN_BASE + 0x40008)
#define ARDATA		__reg32(M32700UT_LAN_BASE + 0x40010)
#define ARINTSEL	__reg32(M32700UT_LAN_BASE + 0x40014)
#define ARINTSEL_INT3		0x10000000	/* CPU INT3 */
#define ARDATA32	__reg32(M32700UT_LAN_BASE + 0x04040010)	// Block 5
/*
#define ARINTSEL_SEL2		0x00002000
#define ARINTSEL_SEL3		0x00001000
#define ARINTSEL_SEL6		0x00000200
#define ARINTSEL_SEL7		0x00000100
#define ARINTSEL_SEL9		0x00000040
#define ARINTSEL_SEL10		0x00000020
#define ARINTSEL_SEL11		0x00000010
#define ARINTSEL_SEL12		0x00000008
*/

/*
 * I2C register on PLD
 */
#define PLDI2CCR	__reg32(M32700UT_LAN_BASE + 0x40040)
#define	PLDI2CCR_ES0		0x00000001	/* enable I2C interface */
#define PLDI2CMOD	__reg32(M32700UT_LAN_BASE + 0x40044)
#define PLDI2CMOD_ACKCLK	0x00000200
#define PLDI2CMOD_DTWD		0x00000100
#define PLDI2CMOD_10BT		0x00000004
#define PLDI2CMOD_ATM_NORMAL	0x00000000
#define PLDI2CMOD_ATM_AUTO	0x00000003
#define PLDI2CACK	__reg32(M32700UT_LAN_BASE + 0x40048)
#define PLDI2CACK_ACK		0x00000001
#define PLDI2CFREQ	__reg32(M32700UT_LAN_BASE + 0x4004c)
#define PLDI2CCND	__reg32(M32700UT_LAN_BASE + 0x40050)
#define PLDI2CCND_START		0x00000001
#define PLDI2CCND_STOP		0x00000002
#define PLDI2CSTEN	__reg32(M32700UT_LAN_BASE + 0x40054)
#define PLDI2CSTEN_STEN		0x00000001
#define PLDI2CDATA	__reg32(M32700UT_LAN_BASE + 0x40060)
#define PLDI2CSTS	__reg32(M32700UT_LAN_BASE + 0x40064)
#define PLDI2CSTS_TRX		0x00000020
#define PLDI2CSTS_BB		0x00000010
#define PLDI2CSTS_NOACK		0x00000001	/* 0:ack, 1:noack */

#endif	/* _M32700UT_M32700UT_LAN_H */
