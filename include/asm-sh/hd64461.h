#ifndef __ASM_SH_HD64461
#define __ASM_SH_HD64461
/*
 *	$Id: hd64461.h,v 1.5 2004/03/16 00:07:51 lethal Exp $
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *	Hitachi HD64461 companion chip support
 */

/* Constants for PCMCIA mappings */
#define HD64461_PCC_WINDOW	0x01000000

#define HD64461_PCC0_BASE	0xb8000000	/* area 6 */
#define HD64461_PCC0_ATTR	(HD64461_PCC0_BASE)
#define HD64461_PCC0_COMM	(HD64461_PCC0_BASE+HD64461_PCC_WINDOW)
#define HD64461_PCC0_IO		(HD64461_PCC0_BASE+2*HD64461_PCC_WINDOW)

#define HD64461_PCC1_BASE	0xb4000000	/* area 5 */
#define HD64461_PCC1_ATTR	(HD64461_PCC1_BASE)
#define HD64461_PCC1_COMM	(HD64461_PCC1_BASE+HD64461_PCC_WINDOW)

#define HD64461_STBCR	0x10000
#define HD64461_STBCR_CKIO_STBY			0x2000
#define HD64461_STBCR_SAFECKE_IST		0x1000
#define HD64461_STBCR_SLCKE_IST			0x0800
#define HD64461_STBCR_SAFECKE_OST		0x0400
#define HD64461_STBCR_SLCKE_OST			0x0200
#define HD64461_STBCR_SMIAST			0x0100
#define HD64461_STBCR_SLCDST			0x0080
#define HD64461_STBCR_SPC0ST			0x0040
#define HD64461_STBCR_SPC1ST			0x0020
#define HD64461_STBCR_SAFEST			0x0010
#define HD64461_STBCR_STM0ST			0x0008
#define HD64461_STBCR_STM1ST			0x0004
#define HD64461_STBCR_SIRST				0x0002
#define HD64461_STBCR_SURTST			0x0001

#define HD64461_SYSCR	0x10002
#define HD64461_SCPUCR	0x10004

#define HD64461_LCDCBAR		0x11000
#define HD64461_LCDCLOR		0x11002
#define HD64461_LCDCCR		0x11004
#define HD64461_LCDCCR_STBACK	0x0400
#define HD64461_LCDCCR_STREQ	0x0100
#define HD64461_LCDCCR_MOFF	0x0080
#define HD64461_LCDCCR_REFSEL	0x0040
#define HD64461_LCDCCR_EPON	0x0020
#define HD64461_LCDCCR_SPON	0x0010

#define	HD64461_LDR1		0x11010
#define	HD64461_LDR1_DON	0x01
#define	HD64461_LDR1_DINV	0x80

#define	HD64461_LDR2		0x11012
#define	HD64461_LDHNCR		0x11014
#define	HD64461_LDHNSR		0x11016
#define HD64461_LDVNTR		0x11018
#define HD64461_LDVNDR		0x1101a
#define HD64461_LDVSPR		0x1101c
#define HD64461_LDR3		0x1101e

#define HD64461_CPTWAR		0x11030
#define HD64461_CPTWDR		0x11032
#define HD64461_CPTRAR		0x11034
#define HD64461_CPTRDR		0x11036

#define HD64461_GRDOR		0x11040
#define HD64461_GRSCR		0x11042
#define HD64461_GRCFGR		0x11044
#define HD64461_GRCFGR_ACCSTATUS		0x10
#define HD64461_GRCFGR_ACCRESET			0x08
#define HD64461_GRCFGR_ACCSTART_BITBLT	0x06
#define HD64461_GRCFGR_ACCSTART_LINE	0x04
#define HD64461_GRCFGR_COLORDEPTH16		0x01

#define HD64461_LNSARH		0x11046
#define HD64461_LNSARL		0x11048
#define HD64461_LNAXLR		0x1104a
#define HD64461_LNDGR		0x1104c
#define HD64461_LNAXR		0x1104e
#define HD64461_LNERTR		0x11050
#define HD64461_LNMDR		0x11052
#define HD64461_BBTSSARH	0x11054
#define HD64461_BBTSSARL	0x11056
#define HD64461_BBTDSARH	0x11058
#define HD64461_BBTDSARL	0x1105a
#define HD64461_BBTDWR		0x1105c
#define HD64461_BBTDHR		0x1105e
#define HD64461_BBTPARH		0x11060
#define HD64461_BBTPARL		0x11062
#define HD64461_BBTMARH		0x11064
#define HD64461_BBTMARL		0x11066
#define HD64461_BBTROPR		0x11068
#define HD64461_BBTMDR		0x1106a

/* PC Card Controller Registers */
#define HD64461_PCC0ISR         0x12000 /* socket 0 interface status */
#define HD64461_PCC0GCR         0x12002 /* socket 0 general control */
#define HD64461_PCC0CSCR        0x12004 /* socket 0 card status change */
#define HD64461_PCC0CSCIER      0x12006 /* socket 0 card status change interrupt enable */
#define HD64461_PCC0SCR         0x12008 /* socket 0 software control */
#define HD64461_PCC1ISR         0x12010 /* socket 1 interface status */
#define HD64461_PCC1GCR         0x12012 /* socket 1 general control */
#define HD64461_PCC1CSCR        0x12014 /* socket 1 card status change */
#define HD64461_PCC1CSCIER      0x12016 /* socket 1 card status change interrupt enable */
#define HD64461_PCC1SCR         0x12018 /* socket 1 software control */

/* PCC Interface Status Register */
#define HD64461_PCCISR_READY		0x80	/* card ready */
#define HD64461_PCCISR_MWP		0x40	/* card write-protected */
#define HD64461_PCCISR_VS2		0x20	/* voltage select pin 2 */
#define HD64461_PCCISR_VS1		0x10	/* voltage select pin 1 */
#define HD64461_PCCISR_CD2		0x08	/* card detect 2 */
#define HD64461_PCCISR_CD1		0x04	/* card detect 1 */
#define HD64461_PCCISR_BVD2		0x02	/* battery 1 */
#define HD64461_PCCISR_BVD1		0x01	/* battery 1 */

#define HD64461_PCCISR_PCD_MASK		0x0c    /* card detect */
#define HD64461_PCCISR_BVD_MASK		0x03    /* battery voltage */
#define HD64461_PCCISR_BVD_BATGOOD	0x03    /* battery good */
#define HD64461_PCCISR_BVD_BATWARN	0x01    /* battery low warning */
#define HD64461_PCCISR_BVD_BATDEAD1	0x02    /* battery dead */
#define HD64461_PCCISR_BVD_BATDEAD2	0x00    /* battery dead */

/* PCC General Control Register */
#define HD64461_PCCGCR_DRVE		0x80    /* output drive */
#define HD64461_PCCGCR_PCCR		0x40    /* PC card reset */
#define HD64461_PCCGCR_PCCT		0x20    /* PC card type, 1=IO&mem, 0=mem */
#define HD64461_PCCGCR_VCC0		0x10    /* voltage control pin VCC0SEL0 */
#define HD64461_PCCGCR_PMMOD		0x08    /* memory mode */
#define HD64461_PCCGCR_PA25		0x04    /* pin A25 */
#define HD64461_PCCGCR_PA24		0x02    /* pin A24 */
#define HD64461_PCCGCR_REG		0x01    /* pin PCC0REG# */

/* PCC Card Status Change Register */
#define HD64461_PCCCSCR_SCDI		0x80    /* sw card detect intr */
#define HD64461_PCCCSCR_SRV1		0x40    /* reserved */
#define HD64461_PCCCSCR_IREQ		0x20    /* IREQ intr req */
#define HD64461_PCCCSCR_SC		0x10    /* STSCHG (status change) pin */
#define HD64461_PCCCSCR_CDC		0x08    /* CD (card detect) change */
#define HD64461_PCCCSCR_RC		0x04    /* READY change */
#define HD64461_PCCCSCR_BW		0x02    /* battery warning change */
#define HD64461_PCCCSCR_BD		0x01    /* battery dead change */

/* PCC Card Status Change Interrupt Enable Register */
#define HD64461_PCCCSCIER_CRE		0x80    /* change reset enable */
#define HD64461_PCCCSCIER_IREQE_MASK	0x60   /* IREQ enable */
#define HD64461_PCCCSCIER_IREQE_DISABLED	0x00   /* IREQ disabled */
#define HD64461_PCCCSCIER_IREQE_LEVEL	0x20   /* IREQ level-triggered */
#define HD64461_PCCCSCIER_IREQE_FALLING	0x40   /* IREQ falling-edge-trig */
#define HD64461_PCCCSCIER_IREQE_RISING	0x60   /* IREQ rising-edge-trig */

#define HD64461_PCCCSCIER_SCE		0x10    /* status change enable */
#define HD64461_PCCCSCIER_CDE		0x08    /* card detect change enable */
#define HD64461_PCCCSCIER_RE		0x04    /* ready change enable */
#define HD64461_PCCCSCIER_BWE		0x02    /* battery warn change enable */
#define HD64461_PCCCSCIER_BDE		0x01    /* battery dead change enable*/

/* PCC Software Control Register */
#define HD64461_PCCSCR_VCC1		0x02	/* voltage control pin 1 */
#define HD64461_PCCSCR_SWP		0x01    /* write protect */

#define HD64461_P0OCR           0x1202a
#define HD64461_P1OCR           0x1202c
#define HD64461_PGCR            0x1202e

#define HD64461_GPACR		0x14000
#define HD64461_GPBCR		0x14002
#define HD64461_GPCCR		0x14004
#define HD64461_GPDCR		0x14006
#define HD64461_GPADR		0x14010
#define HD64461_GPBDR		0x14012
#define HD64461_GPCDR		0x14014
#define HD64461_GPDDR		0x14016
#define HD64461_GPAICR		0x14020
#define HD64461_GPBICR		0x14022
#define HD64461_GPCICR		0x14024
#define HD64461_GPDICR		0x14026
#define HD64461_GPAISR		0x14040
#define HD64461_GPBISR		0x14042
#define HD64461_GPCISR		0x14044
#define HD64461_GPDISR		0x14046

#define HD64461_NIRR		0x15000
#define HD64461_NIMR		0x15002

#define HD64461_IRQBASE		OFFCHIP_IRQ_BASE
#define HD64461_IRQ_NUM		16

#define HD64461_IRQ_UART	(HD64461_IRQBASE+5)
#define HD64461_IRQ_IRDA	(HD64461_IRQBASE+6)
#define HD64461_IRQ_TMU1	(HD64461_IRQBASE+9)
#define HD64461_IRQ_TMU0	(HD64461_IRQBASE+10)
#define HD64461_IRQ_GPIO	(HD64461_IRQBASE+11)
#define HD64461_IRQ_AFE		(HD64461_IRQBASE+12)
#define HD64461_IRQ_PCC1	(HD64461_IRQBASE+13)
#define HD64461_IRQ_PCC0	(HD64461_IRQBASE+14)

#define __IO_PREFIX	hd64461
#include <asm/io_generic.h>

/* arch/sh/cchips/hd6446x/hd64461/setup.c */
int hd64461_irq_demux(int irq);
void hd64461_register_irq_demux(int irq,
				int (*demux) (int irq, void *dev), void *dev);
void hd64461_unregister_irq_demux(int irq);

#endif
