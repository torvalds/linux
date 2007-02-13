#ifndef _MAPPI2_PLD_H
#define _MAPPI2_PLD_H

/*
 * include/asm-m32r/mappi2/mappi2_pld.h
 *
 * Definitions for Extended IO Logic on MAPPI2 board.
 *  based on m32700ut_pld.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 */

#ifndef __ASSEMBLY__
/* FIXME:
 * Some C functions use non-cache address, so can't define non-cache address.
 */
#define PLD_BASE		(0x10c00000 /* + NONCACHE_OFFSET */)
#define __reg8			(volatile unsigned char *)
#define __reg16			(volatile unsigned short *)
#define __reg32			(volatile unsigned int *)
#else
#define PLD_BASE		(0x10c00000 + NONCACHE_OFFSET)
#define __reg8
#define __reg16
#define __reg32
#endif /* __ASSEMBLY__ */

/* CFC */
#define	PLD_CFRSTCR		__reg16(PLD_BASE + 0x0000)
#define PLD_CFSTS		__reg16(PLD_BASE + 0x0002)
#define PLD_CFIMASK		__reg16(PLD_BASE + 0x0004)
#define PLD_CFBUFCR		__reg16(PLD_BASE + 0x0006)
#define PLD_CFCR0		__reg16(PLD_BASE + 0x000a)
#define PLD_CFCR1		__reg16(PLD_BASE + 0x000c)

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

/* Power Control of MMC and CF */
#define PLD_CPCR		__reg16(PLD_BASE + 0x14000)


/*==== ICU ====*/
#define  M32R_IRQ_PC104        (5)   /* INT4(PC/104) */
#define  M32R_IRQ_I2C          (28)  /* I2C-BUS     */
#if 1
#define  PLD_IRQ_CFIREQ       (40)  /* CFC Card Interrupt */
#define  PLD_IRQ_CFC_INSERT   (41)  /* CFC Card Insert */
#define  PLD_IRQ_CFC_EJECT    (42)  /* CFC Card Eject */
#define  PLD_IRQ_MMCCARD      (43)  /* MMC Card Insert */
#define  PLD_IRQ_MMCIRQ       (44)  /* MMC Transfer Done */
#else
#define  PLD_IRQ_CFIREQ       (34)  /* CFC Card Interrupt */
#define  PLD_IRQ_CFC_INSERT   (35)  /* CFC Card Insert */
#define  PLD_IRQ_CFC_EJECT    (36)  /* CFC Card Eject */
#define  PLD_IRQ_MMCCARD      (37)  /* MMC Card Insert */
#define  PLD_IRQ_MMCIRQ       (38)  /* MMC Transfer Done */
#endif


#if 0
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

#endif

/* CRC */
#define PLD_CRC7DATA		__reg16(PLD_BASE + 0x18000)
#define PLD_CRC7INDATA		__reg16(PLD_BASE + 0x18002)
#define PLD_CRC16DATA		__reg16(PLD_BASE + 0x18004)
#define PLD_CRC16INDATA		__reg16(PLD_BASE + 0x18006)
#define PLD_CRC16ADATA		__reg16(PLD_BASE + 0x18008)
#define PLD_CRC16AINDATA	__reg16(PLD_BASE + 0x1800a)


#if 0
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

#endif

#endif /* _MAPPI2_PLD.H */
