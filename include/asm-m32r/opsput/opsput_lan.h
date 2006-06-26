/*
 * include/asm/opsput_lan.h
 *
 * OPSPUT-LAN board
 *
 * Copyright (c) 2002-2004	Takeo Takahashi, Mamoru Sakugawa
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * $Id: opsput_lan.h,v 1.1 2004/07/27 06:54:20 sakugawa Exp $
 */

#ifndef _OPSPUT_OPSPUT_LAN_H
#define _OPSPUT_OPSPUT_LAN_H


#ifndef __ASSEMBLY__
/*
 * C functions use non-cache address.
 */
#define OPSPUT_LAN_BASE	(0x10000000 /* + NONCACHE_OFFSET */)
#else
#define OPSPUT_LAN_BASE	(0x10000000 + NONCACHE_OFFSET)
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
#define OPSPUT_LAN_IRQ_LAN	(OPSPUT_LAN_PLD_IRQ_BASE + 1)	/* LAN */
#define OPSPUT_LAN_IRQ_I2C	(OPSPUT_LAN_PLD_IRQ_BASE + 3)	/* I2C */

#define OPSPUT_LAN_ICUISTS	__reg16(OPSPUT_LAN_BASE + 0xc0002)
#define OPSPUT_LAN_ICUISTS_VECB_MASK	(0xf000)
#define OPSPUT_LAN_VECB(x)	((x) & OPSPUT_LAN_ICUISTS_VECB_MASK)
#define OPSPUT_LAN_ICUISTS_ISN_MASK	(0x07c0)
#define OPSPUT_LAN_ICUISTS_ISN(x)	((x) & OPSPUT_LAN_ICUISTS_ISN_MASK)
#define OPSPUT_LAN_ICUIREQ0	__reg16(OPSPUT_LAN_BASE + 0xc0004)
#define OPSPUT_LAN_ICUCR1	__reg16(OPSPUT_LAN_BASE + 0xc0010)
#define OPSPUT_LAN_ICUCR3	__reg16(OPSPUT_LAN_BASE + 0xc0014)

#endif	/* _OPSPUT_OPSPUT_LAN_H */
