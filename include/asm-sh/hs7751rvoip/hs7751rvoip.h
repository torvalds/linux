#ifndef __ASM_SH_RENESAS_HS7751RVOIP_H
#define __ASM_SH_RENESAS_HS7751RVOIP_H

/*
 * linux/include/asm-sh/hs7751rvoip/hs7751rvoip.h
 *
 * Copyright (C) 2000  Atom Create Engineering Co., Ltd.
 *
 * Renesas Technology Sales HS7751RVoIP support
 */

/* Box specific addresses.  */

#define PA_BCR		0xa4000000	/* FPGA */
#define PA_SLICCNTR1	0xa4000006	/* SLIC PIO Control 1 */
#define PA_SLICCNTR2	0xa4000008	/* SLIC PIO Control 2 */
#define PA_DMACNTR	0xa400000a	/* USB DMA Control */
#define PA_INPORTR	0xa400000c	/* Input Port Register */
#define PA_OUTPORTR	0xa400000e	/* Output Port Reguster */
#define PA_VERREG	0xa4000014	/* FPGA Version Register */

#define PA_AREA5_IO	0xb4000000	/* Area 5 IO Memory */
#define PA_AREA6_IO	0xb8000000	/* Area 6 IO Memory */
#define PA_IDE_OFFSET	0x1f0		/* CF IDE Offset */

#define IRLCNTR1	(PA_BCR + 0)	/* Interrupt Control Register1 */
#define IRLCNTR2	(PA_BCR + 2)	/* Interrupt Control Register2 */
#define IRLCNTR3	(PA_BCR + 4)	/* Interrupt Control Register3 */
#define IRLCNTR4	(PA_BCR + 16)	/* Interrupt Control Register4 */
#define IRLCNTR5	(PA_BCR + 18)	/* Interrupt Control Register5 */

#define IRQ_PCIETH	6		/* PCI Ethernet IRQ */
#define IRQ_PCIHUB	7		/* PCI Ethernet Hub IRQ */
#define IRQ_USBCOM	8		/* USB Comunication IRQ */
#define IRQ_USBCON	9		/* USB Connect IRQ */
#define IRQ_USBDMA	10		/* USB DMA IRQ */
#define IRQ_CFCARD	11		/* CF Card IRQ */
#define IRQ_PCMCIA	12		/* PCMCIA IRQ */
#define IRQ_PCISLOT	13		/* PCI Slot #1 IRQ */
#define IRQ_ONHOOK1	0		/* ON HOOK1 IRQ */
#define IRQ_OFFHOOK1	1		/* OFF HOOK1 IRQ */
#define IRQ_ONHOOK2	2		/* ON HOOK2 IRQ */
#define IRQ_OFFHOOK2	3		/* OFF HOOK2 IRQ */
#define	IRQ_RINGING	4		/* Ringing IRQ */
#define	IRQ_CODEC	5		/* CODEC IRQ */

#endif  /* __ASM_SH_RENESAS_HS7751RVOIP */
