/*
 * include/asm-m32r/m32700ut/m32700ut_pld.h
 *
 * Definitions for Programable Logic Device(PLD) on M32700UT board.
 *
 * Copyright (c) 2002	Takeo Takahashi
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * $Id$
 */

#ifndef _M32700UT_M32700UT_PLD_H
#define _M32700UT_M32700UT_PLD_H


#if defined(CONFIG_PLAT_M32700UT_Alpha)
#define PLD_PLAT_BASE		0x08c00000
#elif defined(CONFIG_PLAT_M32700UT) || defined(CONFIG_PLAT_USRV)
#define PLD_PLAT_BASE		0x04c00000
#else
#error "no platform configuration"
#endif

#ifndef __ASSEMBLY__
/*
 * C functions use non-cache address.
 */
#define PLD_BASE		(PLD_PLAT_BASE /* + NONCACHE_OFFSET */)
#define __reg8			(volatile unsigned char *)
#define __reg16			(volatile unsigned short *)
#define __reg32			(volatile unsigned int *)
#else
#define PLD_BASE		(PLD_PLAT_BASE + NONCACHE_OFFSET)
#define __reg8
#define __reg16
#define __reg32
#endif	/* __ASSEMBLY__ */

/* CFC */
#define	PLD_CFRSTCR		__reg16(PLD_BASE + 0x0000)
#define PLD_CFSTS		__reg16(PLD_BASE + 0x0002)
#define PLD_CFIMASK		__reg16(PLD_BASE + 0x0004)
#define PLD_CFBUFCR		__reg16(PLD_BASE + 0x0006)
#define PLD_CFVENCR		__reg16(PLD_BASE + 0x0008)
#define PLD_CFCR0		__reg16(PLD_BASE + 0x000a)
#define PLD_CFCR1		__reg16(PLD_BASE + 0x000c)
#define PLD_IDERSTCR		__reg16(PLD_BASE + 0x0010)

/* MMC */
#define PLD_MMCCR		__reg16(PLD_BASE + 0x4000)
#define PLD_MMCMOD		__reg16(PLD_BASE + 0x4002)
#define PLD_MMCSTS		__reg16(PLD_BASE + 0x4006)
#define PLD_MMCBAUR		__reg16(PLD_BASE + 0x400a)
#define PLD_MMCCMDBCUT		__reg16(PLD_BASE + 0x400c)
#define PLD_MMCCDTBCUT		__reg16(PLD_BASE + 0x400e)
#define PLD_MMCDET		__reg16(PLD_BASE + 0x4010)
#define PLD_MMCWP		__reg16(PLD_BASE + 0x4012)
#define PLD_MMCWDATA		__reg16(PLD_BASE + 0x5000)
#define PLD_MMCRDATA		__reg16(PLD_BASE + 0x6000)
#define PLD_MMCCMDDATA		__reg16(PLD_BASE + 0x7000)
#define PLD_MMCRSPDATA		__reg16(PLD_BASE + 0x7006)

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
#if !defined(CONFIG_PLAT_USRV)
#define PLD_IRQ_INT0		(M32700UT_PLD_IRQ_BASE + 0)	/* None */
#define PLD_IRQ_INT1		(M32700UT_PLD_IRQ_BASE + 1)	/* reserved */
#define PLD_IRQ_INT2		(M32700UT_PLD_IRQ_BASE + 2)	/* reserved */
#define PLD_IRQ_CFIREQ		(M32700UT_PLD_IRQ_BASE + 3)	/* CF IREQ */
#define PLD_IRQ_CFC_INSERT	(M32700UT_PLD_IRQ_BASE + 4)	/* CF Insert */
#define PLD_IRQ_CFC_EJECT	(M32700UT_PLD_IRQ_BASE + 5)	/* CF Eject */
#define PLD_IRQ_EXINT		(M32700UT_PLD_IRQ_BASE + 6)	/* EXINT */
#define PLD_IRQ_INT7		(M32700UT_PLD_IRQ_BASE + 7)	/* reserved */
#define PLD_IRQ_INT8		(M32700UT_PLD_IRQ_BASE + 8)	/* reserved */
#define PLD_IRQ_INT9		(M32700UT_PLD_IRQ_BASE + 9)	/* reserved */
#define PLD_IRQ_INT10		(M32700UT_PLD_IRQ_BASE + 10)	/* reserved */
#define PLD_IRQ_MMCCARD		(M32700UT_PLD_IRQ_BASE + 11)	/* MMC Insert/Eject */
#define PLD_IRQ_INT12		(M32700UT_PLD_IRQ_BASE + 12)	/* reserved */
#define PLD_IRQ_SC_ERROR	(M32700UT_PLD_IRQ_BASE + 13)	/* SC error */
#define PLD_IRQ_SC_RCV		(M32700UT_PLD_IRQ_BASE + 14)	/* SC receive */
#define PLD_IRQ_SC_SND		(M32700UT_PLD_IRQ_BASE + 15)	/* SC send */
#define PLD_IRQ_SIO0_RCV	(M32700UT_PLD_IRQ_BASE + 16)	/* SIO receive */
#define PLD_IRQ_SIO0_SND	(M32700UT_PLD_IRQ_BASE + 17)	/* SIO send */
#define PLD_IRQ_INT18		(M32700UT_PLD_IRQ_BASE + 18)	/* reserved */
#define PLD_IRQ_INT19		(M32700UT_PLD_IRQ_BASE + 19)	/* reserved */
#define PLD_IRQ_INT20		(M32700UT_PLD_IRQ_BASE + 20)	/* reserved */
#define PLD_IRQ_INT21		(M32700UT_PLD_IRQ_BASE + 21)	/* reserved */
#define PLD_IRQ_INT22		(M32700UT_PLD_IRQ_BASE + 22)	/* reserved */
#define PLD_IRQ_INT23		(M32700UT_PLD_IRQ_BASE + 23)	/* reserved */
#define PLD_IRQ_INT24		(M32700UT_PLD_IRQ_BASE + 24)	/* reserved */
#define PLD_IRQ_INT25		(M32700UT_PLD_IRQ_BASE + 25)	/* reserved */
#define PLD_IRQ_INT26		(M32700UT_PLD_IRQ_BASE + 26)	/* reserved */
#define PLD_IRQ_INT27		(M32700UT_PLD_IRQ_BASE + 27)	/* reserved */
#define PLD_IRQ_INT28		(M32700UT_PLD_IRQ_BASE + 28)	/* reserved */
#define PLD_IRQ_INT29		(M32700UT_PLD_IRQ_BASE + 29)	/* reserved */
#define PLD_IRQ_INT30		(M32700UT_PLD_IRQ_BASE + 30)	/* reserved */
#define PLD_IRQ_INT31		(M32700UT_PLD_IRQ_BASE + 31)	/* reserved */

#else	/* CONFIG_PLAT_USRV */

#define PLD_IRQ_INT0		(M32700UT_PLD_IRQ_BASE + 0)	/* None */
#define PLD_IRQ_INT1		(M32700UT_PLD_IRQ_BASE + 1)	/* reserved */
#define PLD_IRQ_INT2		(M32700UT_PLD_IRQ_BASE + 2)	/* reserved */
#define PLD_IRQ_CF0		(M32700UT_PLD_IRQ_BASE + 3)	/* CF0# */
#define PLD_IRQ_CF1		(M32700UT_PLD_IRQ_BASE + 4)	/* CF1# */
#define PLD_IRQ_CF2		(M32700UT_PLD_IRQ_BASE + 5)	/* CF2# */
#define PLD_IRQ_CF3		(M32700UT_PLD_IRQ_BASE + 6)	/* CF3# */
#define PLD_IRQ_CF4		(M32700UT_PLD_IRQ_BASE + 7)	/* CF4# */
#define PLD_IRQ_INT8		(M32700UT_PLD_IRQ_BASE + 8)	/* reserved */
#define PLD_IRQ_INT9		(M32700UT_PLD_IRQ_BASE + 9)	/* reserved */
#define PLD_IRQ_INT10		(M32700UT_PLD_IRQ_BASE + 10)	/* reserved */
#define PLD_IRQ_INT11		(M32700UT_PLD_IRQ_BASE + 11)	/* reserved */
#define PLD_IRQ_UART0		(M32700UT_PLD_IRQ_BASE + 12)	/* UARTIRQ0 */
#define PLD_IRQ_UART1		(M32700UT_PLD_IRQ_BASE + 13)	/* UARTIRQ1 */
#define PLD_IRQ_INT14		(M32700UT_PLD_IRQ_BASE + 14)	/* reserved */
#define PLD_IRQ_INT15		(M32700UT_PLD_IRQ_BASE + 15)	/* reserved */
#define PLD_IRQ_SNDINT		(M32700UT_PLD_IRQ_BASE + 16)	/* SNDINT# */
#define PLD_IRQ_INT17		(M32700UT_PLD_IRQ_BASE + 17)	/* reserved */
#define PLD_IRQ_INT18		(M32700UT_PLD_IRQ_BASE + 18)	/* reserved */
#define PLD_IRQ_INT19		(M32700UT_PLD_IRQ_BASE + 19)	/* reserved */
#define PLD_IRQ_INT20		(M32700UT_PLD_IRQ_BASE + 20)	/* reserved */
#define PLD_IRQ_INT21		(M32700UT_PLD_IRQ_BASE + 21)	/* reserved */
#define PLD_IRQ_INT22		(M32700UT_PLD_IRQ_BASE + 22)	/* reserved */
#define PLD_IRQ_INT23		(M32700UT_PLD_IRQ_BASE + 23)	/* reserved */
#define PLD_IRQ_INT24		(M32700UT_PLD_IRQ_BASE + 24)	/* reserved */
#define PLD_IRQ_INT25		(M32700UT_PLD_IRQ_BASE + 25)	/* reserved */
#define PLD_IRQ_INT26		(M32700UT_PLD_IRQ_BASE + 26)	/* reserved */
#define PLD_IRQ_INT27		(M32700UT_PLD_IRQ_BASE + 27)	/* reserved */
#define PLD_IRQ_INT28		(M32700UT_PLD_IRQ_BASE + 28)	/* reserved */
#define PLD_IRQ_INT29		(M32700UT_PLD_IRQ_BASE + 29)	/* reserved */
#define PLD_IRQ_INT30		(M32700UT_PLD_IRQ_BASE + 30)	/* reserved */

#endif	/* CONFIG_PLAT_USRV */

#define PLD_ICUISTS		__reg16(PLD_BASE + 0x8002)
#define PLD_ICUISTS_VECB_MASK	(0xf000)
#define PLD_ICUISTS_VECB(x)	((x) & PLD_ICUISTS_VECB_MASK)
#define PLD_ICUISTS_ISN_MASK	(0x07c0)
#define PLD_ICUISTS_ISN(x)	((x) & PLD_ICUISTS_ISN_MASK)
#define PLD_ICUIREQ0		__reg16(PLD_BASE + 0x8004)
#define PLD_ICUIREQ1		__reg16(PLD_BASE + 0x8006)
#define PLD_ICUCR1		__reg16(PLD_BASE + 0x8100)
#define PLD_ICUCR2		__reg16(PLD_BASE + 0x8102)
#define PLD_ICUCR3		__reg16(PLD_BASE + 0x8104)
#define PLD_ICUCR4		__reg16(PLD_BASE + 0x8106)
#define PLD_ICUCR5		__reg16(PLD_BASE + 0x8108)
#define PLD_ICUCR6		__reg16(PLD_BASE + 0x810a)
#define PLD_ICUCR7		__reg16(PLD_BASE + 0x810c)
#define PLD_ICUCR8		__reg16(PLD_BASE + 0x810e)
#define PLD_ICUCR9		__reg16(PLD_BASE + 0x8110)
#define PLD_ICUCR10		__reg16(PLD_BASE + 0x8112)
#define PLD_ICUCR11		__reg16(PLD_BASE + 0x8114)
#define PLD_ICUCR12		__reg16(PLD_BASE + 0x8116)
#define PLD_ICUCR13		__reg16(PLD_BASE + 0x8118)
#define PLD_ICUCR14		__reg16(PLD_BASE + 0x811a)
#define PLD_ICUCR15		__reg16(PLD_BASE + 0x811c)
#define PLD_ICUCR16		__reg16(PLD_BASE + 0x811e)
#define PLD_ICUCR17		__reg16(PLD_BASE + 0x8120)
#define PLD_ICUCR_IEN		(0x1000)
#define PLD_ICUCR_IREQ		(0x0100)
#define PLD_ICUCR_ISMOD00	(0x0000)	/* Low edge */
#define PLD_ICUCR_ISMOD01	(0x0010)	/* Low level */
#define PLD_ICUCR_ISMOD02	(0x0020)	/* High edge */
#define PLD_ICUCR_ISMOD03	(0x0030)	/* High level */
#define PLD_ICUCR_ILEVEL0	(0x0000)
#define PLD_ICUCR_ILEVEL1	(0x0001)
#define PLD_ICUCR_ILEVEL2	(0x0002)
#define PLD_ICUCR_ILEVEL3	(0x0003)
#define PLD_ICUCR_ILEVEL4	(0x0004)
#define PLD_ICUCR_ILEVEL5	(0x0005)
#define PLD_ICUCR_ILEVEL6	(0x0006)
#define PLD_ICUCR_ILEVEL7	(0x0007)

/* Power Control of MMC and CF */
#define PLD_CPCR		__reg16(PLD_BASE + 0x14000)
#define PLD_CPCR_CF		0x0001
#define PLD_CPCR_MMC		0x0002

/* LED Control
 *
 * 1: DIP swich side
 * 2: Reset switch side
 */
#define PLD_IOLEDCR		__reg16(PLD_BASE + 0x14002)
#define PLD_IOLED_1_ON		0x001
#define PLD_IOLED_1_OFF		0x000
#define PLD_IOLED_2_ON		0x002
#define PLD_IOLED_2_OFF		0x000

/* DIP Switch
 *  0: Write-protect of Flash Memory (0:protected, 1:non-protected)
 *  1: -
 *  2: -
 *  3: -
 */
#define PLD_IOSWSTS		__reg16(PLD_BASE + 0x14004)
#define	PLD_IOSWSTS_IOSW2	0x0200
#define	PLD_IOSWSTS_IOSW1	0x0100
#define	PLD_IOSWSTS_IOWP0	0x0001

/* CRC */
#define PLD_CRC7DATA		__reg16(PLD_BASE + 0x18000)
#define PLD_CRC7INDATA		__reg16(PLD_BASE + 0x18002)
#define PLD_CRC16DATA		__reg16(PLD_BASE + 0x18004)
#define PLD_CRC16INDATA		__reg16(PLD_BASE + 0x18006)
#define PLD_CRC16ADATA		__reg16(PLD_BASE + 0x18008)
#define PLD_CRC16AINDATA	__reg16(PLD_BASE + 0x1800a)

/* RTC */
#define PLD_RTCCR		__reg16(PLD_BASE + 0x1c000)
#define PLD_RTCBAUR		__reg16(PLD_BASE + 0x1c002)
#define PLD_RTCWRDATA		__reg16(PLD_BASE + 0x1c004)
#define PLD_RTCRDDATA		__reg16(PLD_BASE + 0x1c006)
#define PLD_RTCRSTODT		__reg16(PLD_BASE + 0x1c008)

/* SIO0 */
#define PLD_ESIO0CR		__reg16(PLD_BASE + 0x20000)
#define	PLD_ESIO0CR_TXEN	0x0001
#define	PLD_ESIO0CR_RXEN	0x0002
#define PLD_ESIO0MOD0		__reg16(PLD_BASE + 0x20002)
#define	PLD_ESIO0MOD0_CTSS	0x0040
#define	PLD_ESIO0MOD0_RTSS	0x0080
#define PLD_ESIO0MOD1		__reg16(PLD_BASE + 0x20004)
#define	PLD_ESIO0MOD1_LMFS	0x0010
#define PLD_ESIO0STS		__reg16(PLD_BASE + 0x20006)
#define	PLD_ESIO0STS_TEMP	0x0001
#define	PLD_ESIO0STS_TXCP	0x0002
#define	PLD_ESIO0STS_RXCP	0x0004
#define	PLD_ESIO0STS_TXSC	0x0100
#define	PLD_ESIO0STS_RXSC	0x0200
#define PLD_ESIO0STS_TXREADY	(PLD_ESIO0STS_TXCP | PLD_ESIO0STS_TEMP)
#define PLD_ESIO0INTCR		__reg16(PLD_BASE + 0x20008)
#define	PLD_ESIO0INTCR_TXIEN	0x0002
#define	PLD_ESIO0INTCR_RXCEN	0x0004
#define PLD_ESIO0BAUR		__reg16(PLD_BASE + 0x2000a)
#define PLD_ESIO0TXB		__reg16(PLD_BASE + 0x2000c)
#define PLD_ESIO0RXB		__reg16(PLD_BASE + 0x2000e)

/* SIM Card */
#define PLD_SCCR		__reg16(PLD_BASE + 0x38000)
#define PLD_SCMOD		__reg16(PLD_BASE + 0x38004)
#define PLD_SCSTS		__reg16(PLD_BASE + 0x38006)
#define PLD_SCINTCR		__reg16(PLD_BASE + 0x38008)
#define PLD_SCBAUR		__reg16(PLD_BASE + 0x3800a)
#define PLD_SCTXB		__reg16(PLD_BASE + 0x3800c)
#define PLD_SCRXB		__reg16(PLD_BASE + 0x3800e)

#endif	/* _M32700UT_M32700UT_PLD.H */
