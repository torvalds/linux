#ifndef __ASM_SH_RENESAS_RTS7751R2D_H
#define __ASM_SH_RENESAS_RTS7751R2D_H

/*
 * linux/include/asm-sh/renesas_rts7751r2d.h
 *
 * Copyright (C) 2000  Atom Create Engineering Co., Ltd.
 *
 * Renesas Technology Sales RTS7751R2D support
 */

/* Box specific addresses.  */

#define PA_BCR		0xa4000000	/* FPGA */
#define PA_IRLMON	0xa4000002	/* Interrupt Status control */
#define PA_CFCTL	0xa4000004	/* CF Timing control */
#define PA_CFPOW	0xa4000006	/* CF Power control */
#define PA_DISPCTL	0xa4000008	/* Display Timing control */
#define PA_SDMPOW	0xa400000a	/* SD Power control */
#define PA_RTCCE	0xa400000c	/* RTC(9701) Enable control */
#define PA_PCICD	0xa400000e	/* PCI Extention detect control */
#define PA_VOYAGERRTS	0xa4000020	/* VOYAGER Reset control */
#if defined(CONFIG_RTS7751R2D_REV11)
#define PA_AXRST	0xa4000022	/* AX_LAN Reset control */
#define PA_CFRST	0xa4000024	/* CF Reset control */
#define	PA_ADMRTS	0xa4000026	/* SD Reset control */
#define PA_EXTRST	0xa4000028	/* Extention Reset control */
#define PA_CFCDINTCLR	0xa400002a	/* CF Insert Interrupt clear */
#else
#define PA_CFRST	0xa4000022	/* CF Reset control */
#define	PA_ADMRTS	0xa4000024	/* SD Reset control */
#define PA_EXTRST	0xa4000026	/* Extention Reset control */
#define PA_CFCDINTCLR	0xa4000028	/* CF Insert Interrupt clear */
#define	PA_KEYCTLCLR	0xa400002a	/* Key Interrupt clear */
#endif
#define PA_POWOFF	0xa4000030	/* Board Power OFF control */
#define PA_VERREG	0xa4000032	/* FPGA Version Register */
#define PA_INPORT	0xa4000034	/* KEY Input Port control */
#define PA_OUTPORT	0xa4000036	/* LED control */
#define PA_BVERREG	0xa4000038	/* Board Revision Register */

#define PA_AX88796L	0xaa000400	/* AX88796L Area */
#define PA_VOYAGER	0xab000000	/* VOYAGER GX Area */
#define PA_IDE_OFFSET	0x1f0		/* CF IDE Offset */
#define AX88796L_IO_BASE	0x1000	/* AX88796L IO Base Address */

#define IRLCNTR1	(PA_BCR + 0)	/* Interrupt Control Register1 */

#if defined(CONFIG_RTS7751R2D_REV11)
#define IRQ_PCIETH	0		/* PCI Ethernet IRQ */
#define IRQ_CFCARD	1		/* CF Card IRQ */
#define IRQ_CFINST	2		/* CF Card Insert IRQ */
#define IRQ_PCMCIA	3		/* PCMCIA IRQ */
#define IRQ_VOYAGER	4		/* VOYAGER IRQ */
#define IRQ_ONETH	5		/* On board Ethernet IRQ */
#else
#define IRQ_KEYIN	0		/* Key Input IRQ */
#define IRQ_PCIETH	1		/* PCI Ethernet IRQ */
#define IRQ_CFCARD	2		/* CF Card IRQ */
#define IRQ_CFINST	3		/* CF Card Insert IRQ */
#define IRQ_PCMCIA	4		/* PCMCIA IRQ */
#define IRQ_VOYAGER	5		/* VOYAGER IRQ */
#endif
#define IRQ_RTCALM	6		/* RTC Alarm IRQ */
#define IRQ_RTCTIME	7		/* RTC Timer IRQ */
#define IRQ_SDCARD	8		/* SD Card IRQ */
#define IRQ_PCISLOT1	9		/* PCI Slot #1 IRQ */
#define IRQ_PCISLOT2	10		/* PCI Slot #2 IRQ */
#define	IRQ_EXTENTION	11		/* EXTn IRQ */

/* arch/sh/boards/renesas/rts7751r2d/irq.c */
void init_rts7751r2d_IRQ(void);
int rts7751r2d_irq_demux(int);

#define __IO_PREFIX rts7751r2d
#include <asm/io_generic.h>

#endif  /* __ASM_SH_RENESAS_RTS7751R2D */
