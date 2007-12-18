#ifndef __ASM_SH_HD64461
#define __ASM_SH_HD64461
/*
 *	Copyright (C) 2007 Kristoffer Ericson <Kristoffer.Ericson@gmail.com>
 *	Copyright (C) 2004 Paul Mundt
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *
 *		Hitachi HD64461 companion chip support
 *	(please note manual reference 0x10000000 = 0xb0000000)
 */

/* Constants for PCMCIA mappings */
#define	HD64461_PCC_WINDOW	0x01000000

/* Area 6 - Slot 0 - memory and/or IO card */
#define	HD64461_PCC0_BASE	(CONFIG_HD64461_IOBASE + 0x8000000)
#define	HD64461_PCC0_ATTR	(HD64461_PCC0_BASE)				/* 0xb80000000 */
#define	HD64461_PCC0_COMM	(HD64461_PCC0_BASE+HD64461_PCC_WINDOW)		/* 0xb90000000 */
#define	HD64461_PCC0_IO		(HD64461_PCC0_BASE+2*HD64461_PCC_WINDOW)	/* 0xba0000000 */

/* Area 5 - Slot 1 - memory card only */
#define	HD64461_PCC1_BASE	(CONFIG_HD64461_IOBASE + 0x4000000)
#define	HD64461_PCC1_ATTR	(HD64461_PCC1_BASE)				/* 0xb4000000 */
#define	HD64461_PCC1_COMM	(HD64461_PCC1_BASE+HD64461_PCC_WINDOW)		/* 0xb5000000 */

/* Standby Control Register for HD64461 */
#define	HD64461_STBCR			CONFIG_HD64461_IOBASE
#define	HD64461_STBCR_CKIO_STBY		0x2000
#define	HD64461_STBCR_SAFECKE_IST	0x1000
#define	HD64461_STBCR_SLCKE_IST		0x0800
#define	HD64461_STBCR_SAFECKE_OST	0x0400
#define	HD64461_STBCR_SLCKE_OST		0x0200
#define	HD64461_STBCR_SMIAST		0x0100
#define	HD64461_STBCR_SLCDST		0x0080
#define	HD64461_STBCR_SPC0ST		0x0040
#define	HD64461_STBCR_SPC1ST		0x0020
#define	HD64461_STBCR_SAFEST		0x0010
#define	HD64461_STBCR_STM0ST		0x0008
#define	HD64461_STBCR_STM1ST		0x0004
#define	HD64461_STBCR_SIRST		0x0002
#define	HD64461_STBCR_SURTST		0x0001

/* System Configuration Register */
#define	HD64461_SYSCR		(CONFIG_HD64461_IOBASE + 0x02)

/* CPU Data Bus Control Register */
#define	HD64461_SCPUCR		(CONFIG_HD64461_IOBASE + 0x04)

/* Base Address Register */
#define	HD64461_LCDCBAR		(CONFIG_HD64461_IOBASE + 0x1000)

/* Line increment address */
#define	HD64461_LCDCLOR		(CONFIG_HD64461_IOBASE + 0x1002)

/* Controls LCD controller */
#define	HD64461_LCDCCR		(CONFIG_HD64461_IOBASE + 0x1004)

/* LCCDR control bits */
#define	HD64461_LCDCCR_STBACK	0x0400	/* Standby Back */
#define	HD64461_LCDCCR_STREQ	0x0100	/* Standby Request */
#define	HD64461_LCDCCR_MOFF	0x0080	/* Memory Off */
#define	HD64461_LCDCCR_REFSEL	0x0040	/* Refresh Select */
#define	HD64461_LCDCCR_EPON	0x0020	/* End Power On */
#define	HD64461_LCDCCR_SPON	0x0010	/* Start Power On */

/* Controls LCD (1) */
#define	HD64461_LDR1		(CONFIG_HD64461_IOBASE + 0x1010)
#define	HD64461_LDR1_DON	0x01	/* Display On */
#define	HD64461_LDR1_DINV	0x80	/* Display Invert */

/* Controls LCD (2) */
#define	HD64461_LDR2		(CONFIG_HD64461_IOBASE + 0x1012)
#define	HD64461_LDHNCR		(CONFIG_HD64461_IOBASE + 0x1014)	/* Number of horizontal characters */
#define	HD64461_LDHNSR		(CONFIG_HD64461_IOBASE + 0x1016)	/* Specify output start position + width of CL1 */
#define	HD64461_LDVNTR		(CONFIG_HD64461_IOBASE + 0x1018)	/* Specify total vertical lines */
#define	HD64461_LDVNDR		(CONFIG_HD64461_IOBASE + 0x101a)	/* specify number of display vertical lines */
#define	HD64461_LDVSPR		(CONFIG_HD64461_IOBASE + 0x101c)	/* specify vertical synchronization pos and AC nr */

/* Controls LCD (3) */
#define	HD64461_LDR3		(CONFIG_HD64461_IOBASE + 0x101e)

/* Palette Registers */
#define	HD64461_CPTWAR		(CONFIG_HD64461_IOBASE + 0x1030)	/* Color Palette Write Address Register */
#define	HD64461_CPTWDR		(CONFIG_HD64461_IOBASE + 0x1032)	/* Color Palette Write Data Register */
#define	HD64461_CPTRAR		(CONFIG_HD64461_IOBASE + 0x1034)	/* Color Palette Read Address Register */
#define	HD64461_CPTRDR		(CONFIG_HD64461_IOBASE + 0x1036)	/* Color Palette Read Data Register */

#define	HD64461_GRDOR		(CONFIG_HD64461_IOBASE + 0x1040)	/* Display Resolution Offset Register */
#define	HD64461_GRSCR		(CONFIG_HD64461_IOBASE + 0x1042)	/* Solid Color Register */
#define	HD64461_GRCFGR		(CONFIG_HD64461_IOBASE + 0x1044)	/* Accelerator Configuration Register */

#define	HD64461_GRCFGR_ACCSTATUS	0x10	/* Accelerator Status */
#define	HD64461_GRCFGR_ACCRESET		0x08	/* Accelerator Reset */
#define	HD64461_GRCFGR_ACCSTART_BITBLT	0x06	/* Accelerator Start BITBLT */
#define	HD64461_GRCFGR_ACCSTART_LINE	0x04	/* Accelerator Start Line Drawing */
#define	HD64461_GRCFGR_COLORDEPTH16	0x01	/* Sets Colordepth 16 for Accelerator */
#define	HD64461_GRCFGR_COLORDEPTH8	0x01	/* Sets Colordepth 8 for Accelerator */

/* Line Drawing Registers */
#define	HD64461_LNSARH		(CONFIG_HD64461_IOBASE + 0x1046)	/* Line Start Address Register (H) */
#define	HD64461_LNSARL		(CONFIG_HD64461_IOBASE + 0x1048)	/* Line Start Address Register (L) */
#define	HD64461_LNAXLR		(CONFIG_HD64461_IOBASE + 0x104a)	/* Axis Pixel Length Register */
#define	HD64461_LNDGR		(CONFIG_HD64461_IOBASE + 0x104c)	/* Diagonal Register */
#define	HD64461_LNAXR		(CONFIG_HD64461_IOBASE + 0x104e)	/* Axial Register */
#define	HD64461_LNERTR		(CONFIG_HD64461_IOBASE + 0x1050)	/* Start Error Term Register */
#define	HD64461_LNMDR		(CONFIG_HD64461_IOBASE + 0x1052)	/* Line Mode Register */

/* BitBLT Registers */
#define	HD64461_BBTSSARH	(CONFIG_HD64461_IOBASE + 0x1054)	/* Source Start Address Register (H) */
#define	HD64461_BBTSSARL	(CONFIG_HD64461_IOBASE + 0x1056)	/* Source Start Address Register (L) */
#define	HD64461_BBTDSARH	(CONFIG_HD64461_IOBASE + 0x1058)	/* Destination Start Address Register (H) */
#define	HD64461_BBTDSARL	(CONFIG_HD64461_IOBASE + 0x105a)	/* Destination Start Address Register (L) */
#define	HD64461_BBTDWR		(CONFIG_HD64461_IOBASE + 0x105c)	/* Destination Block Width Register */
#define	HD64461_BBTDHR		(CONFIG_HD64461_IOBASE + 0x105e)	/* Destination Block Height Register */
#define	HD64461_BBTPARH		(CONFIG_HD64461_IOBASE + 0x1060)	/* Pattern Start Address Register (H) */
#define	HD64461_BBTPARL		(CONFIG_HD64461_IOBASE + 0x1062)	/* Pattern Start Address Register (L) */
#define	HD64461_BBTMARH		(CONFIG_HD64461_IOBASE + 0x1064)	/* Mask Start Address Register (H) */
#define	HD64461_BBTMARL		(CONFIG_HD64461_IOBASE + 0x1066)	/* Mask Start Address Register (L) */
#define	HD64461_BBTROPR		(CONFIG_HD64461_IOBASE + 0x1068)	/* ROP Register */
#define	HD64461_BBTMDR		(CONFIG_HD64461_IOBASE + 0x106a)	/* BitBLT Mode Register */

/* PC Card Controller Registers */
/* Maps to Physical Area 6 */
#define	HD64461_PCC0ISR		(CONFIG_HD64461_IOBASE + 0x2000)	/* socket 0 interface status */
#define	HD64461_PCC0GCR		(CONFIG_HD64461_IOBASE + 0x2002)	/* socket 0 general control */
#define	HD64461_PCC0CSCR	(CONFIG_HD64461_IOBASE + 0x2004)	/* socket 0 card status change */
#define	HD64461_PCC0CSCIER	(CONFIG_HD64461_IOBASE + 0x2006)	/* socket 0 card status change interrupt enable */
#define	HD64461_PCC0SCR		(CONFIG_HD64461_IOBASE + 0x2008)	/* socket 0 software control */
/* Maps to Physical Area 5 */
#define	HD64461_PCC1ISR		(CONFIG_HD64461_IOBASE + 0x2010)	/* socket 1 interface status */
#define	HD64461_PCC1GCR		(CONFIG_HD64461_IOBASE + 0x2012)	/* socket 1 general control */
#define	HD64461_PCC1CSCR	(CONFIG_HD64461_IOBASE + 0x2014)	/* socket 1 card status change */
#define	HD64461_PCC1CSCIER	(CONFIG_HD64461_IOBASE + 0x2016)	/* socket 1 card status change interrupt enable */
#define	HD64461_PCC1SCR		(CONFIG_HD64461_IOBASE + 0x2018)	/* socket 1 software control */

/* PCC Interface Status Register */
#define	HD64461_PCCISR_READY		0x80	/* card ready */
#define	HD64461_PCCISR_MWP		0x40	/* card write-protected */
#define	HD64461_PCCISR_VS2		0x20	/* voltage select pin 2 */
#define	HD64461_PCCISR_VS1		0x10	/* voltage select pin 1 */
#define	HD64461_PCCISR_CD2		0x08	/* card detect 2 */
#define	HD64461_PCCISR_CD1		0x04	/* card detect 1 */
#define	HD64461_PCCISR_BVD2		0x02	/* battery 1 */
#define	HD64461_PCCISR_BVD1		0x01	/* battery 1 */

#define	HD64461_PCCISR_PCD_MASK		0x0c	/* card detect */
#define	HD64461_PCCISR_BVD_MASK		0x03	/* battery voltage */
#define	HD64461_PCCISR_BVD_BATGOOD	0x03	/* battery good */
#define	HD64461_PCCISR_BVD_BATWARN	0x01	/* battery low warning */
#define	HD64461_PCCISR_BVD_BATDEAD1	0x02	/* battery dead */
#define	HD64461_PCCISR_BVD_BATDEAD2	0x00	/* battery dead */

/* PCC General Control Register */
#define	HD64461_PCCGCR_DRVE		0x80	/* output drive */
#define	HD64461_PCCGCR_PCCR		0x40	/* PC card reset */
#define	HD64461_PCCGCR_PCCT		0x20	/* PC card type, 1=IO&mem, 0=mem */
#define	HD64461_PCCGCR_VCC0		0x10	/* voltage control pin VCC0SEL0 */
#define	HD64461_PCCGCR_PMMOD		0x08	/* memory mode */
#define	HD64461_PCCGCR_PA25		0x04	/* pin A25 */
#define	HD64461_PCCGCR_PA24		0x02	/* pin A24 */
#define	HD64461_PCCGCR_REG		0x01	/* pin PCC0REG# */

/* PCC Card Status Change Register */
#define	HD64461_PCCCSCR_SCDI		0x80	/* sw card detect intr */
#define	HD64461_PCCCSCR_SRV1		0x40	/* reserved */
#define	HD64461_PCCCSCR_IREQ		0x20	/* IREQ intr req */
#define	HD64461_PCCCSCR_SC		0x10	/* STSCHG (status change) pin */
#define	HD64461_PCCCSCR_CDC		0x08	/* CD (card detect) change */
#define	HD64461_PCCCSCR_RC		0x04	/* READY change */
#define	HD64461_PCCCSCR_BW		0x02	/* battery warning change */
#define	HD64461_PCCCSCR_BD		0x01	/* battery dead change */

/* PCC Card Status Change Interrupt Enable Register */
#define	HD64461_PCCCSCIER_CRE		0x80	/* change reset enable */
#define	HD64461_PCCCSCIER_IREQE_MASK	0x60	/* IREQ enable */
#define	HD64461_PCCCSCIER_IREQE_DISABLED 0x00	/* IREQ disabled */
#define	HD64461_PCCCSCIER_IREQE_LEVEL	0x20	/* IREQ level-triggered */
#define	HD64461_PCCCSCIER_IREQE_FALLING	0x40	/* IREQ falling-edge-trig */
#define	HD64461_PCCCSCIER_IREQE_RISING	0x60	/* IREQ rising-edge-trig */

#define	HD64461_PCCCSCIER_SCE		0x10	/* status change enable */
#define	HD64461_PCCCSCIER_CDE		0x08	/* card detect change enable */
#define	HD64461_PCCCSCIER_RE		0x04	/* ready change enable */
#define	HD64461_PCCCSCIER_BWE		0x02	/* battery warn change enable */
#define	HD64461_PCCCSCIER_BDE		0x01	/* battery dead change enable*/

/* PCC Software Control Register */
#define	HD64461_PCCSCR_VCC1		0x02	/* voltage control pin 1 */
#define	HD64461_PCCSCR_SWP		0x01	/* write protect */

/* PCC0 Output Pins Control Register */
#define	HD64461_P0OCR		(CONFIG_HD64461_IOBASE + 0x202a)

/* PCC1 Output Pins Control Register */
#define	HD64461_P1OCR		(CONFIG_HD64461_IOBASE + 0x202c)

/* PC Card General Control Register */
#define	HD64461_PGCR		(CONFIG_HD64461_IOBASE + 0x202e)

/* Port Control Registers */
#define	HD64461_GPACR		(CONFIG_HD64461_IOBASE + 0x4000)	/* Port A - Handles IRDA/TIMER */
#define	HD64461_GPBCR		(CONFIG_HD64461_IOBASE + 0x4002)	/* Port B - Handles UART */
#define	HD64461_GPCCR		(CONFIG_HD64461_IOBASE + 0x4004)	/* Port C - Handles PCMCIA 1 */
#define	HD64461_GPDCR		(CONFIG_HD64461_IOBASE + 0x4006)	/* Port D - Handles PCMCIA 1 */

/* Port Control Data Registers */
#define	HD64461_GPADR		(CONFIG_HD64461_IOBASE + 0x4010)	/* A */
#define	HD64461_GPBDR		(CONFIG_HD64461_IOBASE + 0x4012)	/* B */
#define	HD64461_GPCDR		(CONFIG_HD64461_IOBASE + 0x4014)	/* C */
#define	HD64461_GPDDR		(CONFIG_HD64461_IOBASE + 0x4016)	/* D */

/* Interrupt Control Registers */
#define	HD64461_GPAICR		(CONFIG_HD64461_IOBASE + 0x4020)	/* A */
#define	HD64461_GPBICR		(CONFIG_HD64461_IOBASE + 0x4022)	/* B */
#define	HD64461_GPCICR		(CONFIG_HD64461_IOBASE + 0x4024)	/* C */
#define	HD64461_GPDICR		(CONFIG_HD64461_IOBASE + 0x4026)	/* D */

/* Interrupt Status Registers */
#define	HD64461_GPAISR		(CONFIG_HD64461_IOBASE + 0x4040)	/* A */
#define	HD64461_GPBISR		(CONFIG_HD64461_IOBASE + 0x4042)	/* B */
#define	HD64461_GPCISR		(CONFIG_HD64461_IOBASE + 0x4044)	/* C */
#define	HD64461_GPDISR		(CONFIG_HD64461_IOBASE + 0x4046)	/* D */

/* Interrupt Request Register & Interrupt Mask Register */
#define	HD64461_NIRR		(CONFIG_HD64461_IOBASE + 0x5000)
#define	HD64461_NIMR		(CONFIG_HD64461_IOBASE + 0x5002)

#define	HD64461_IRQBASE		OFFCHIP_IRQ_BASE
#define	OFFCHIP_IRQ_BASE	64
#define	HD64461_IRQ_NUM		16

#define	HD64461_IRQ_UART	(HD64461_IRQBASE+5)
#define	HD64461_IRQ_IRDA	(HD64461_IRQBASE+6)
#define	HD64461_IRQ_TMU1	(HD64461_IRQBASE+9)
#define	HD64461_IRQ_TMU0	(HD64461_IRQBASE+10)
#define	HD64461_IRQ_GPIO	(HD64461_IRQBASE+11)
#define	HD64461_IRQ_AFE		(HD64461_IRQBASE+12)
#define	HD64461_IRQ_PCC1	(HD64461_IRQBASE+13)
#define	HD64461_IRQ_PCC0	(HD64461_IRQBASE+14)

#define __IO_PREFIX	hd64461
#include <asm/io_generic.h>

/* arch/sh/cchips/hd6446x/hd64461/setup.c */
int hd64461_irq_demux(int irq);
void hd64461_register_irq_demux(int irq,
				int (*demux) (int irq, void *dev), void *dev);
void hd64461_unregister_irq_demux(int irq);

#endif
