/*	$OpenBSD: pccbbreg.h,v 1.12 2012/10/18 21:40:49 deraadt Exp $	*/
/*	$NetBSD: pccbbreg.h,v 1.5 2000/06/07 09:02:47 haya Exp $	*/
/*
 * Copyright (c) 1999 HAYAKAWA Koichi.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_PCI_PCCBBREG_H_
#define	_DEV_PCI_PCCBBREG_H_

#define PCI_SOCKBASE	0x10	/* Socket Base Address Register */
#define PCI_BUSNUM	0x18	/* Latency timer, Subordinate bus number */
#define PCI_BCR_INTR	0x3C	/* Intr line, intr pin, bridge control regs */
#define PCI_LEGACY	0x44	/* Legacy IO register address (32 bits) */
#define PCI_SYSCTRL	0x80	/* System control */
#define PCI_CBCTRL	0x90	/* Retry status, Card ctrl, Device ctrl */

#define PCI_CLASS_INTERFACE_MASK	0xFFFFFF00
#define PCI_CLASS_INTERFACE_YENTA	0x06070000

/* CardBus latency timer, Subordinate bus no, CardBus bus no and PCI bus no */
#define PCI_CB_LSCP_REG		0x18

/* CardBus memory and io windows */
#define PCI_CB_MEMBASE0		0x1C
#define PCI_CB_MEMLIMIT0	0x20
#define PCI_CB_MEMBASE1		0x24
#define PCI_CB_MEMLIMIT1	0x28
#define PCI_CB_IOBASE0		0x2C
#define PCI_CB_IOLIMIT0		0x30
#define PCI_CB_IOBASE1		0x34
#define PCI_CB_IOLIMIT1		0x38

/* PCI_CB_LSCP_REG */
#define PCI_CB_LATENCY_SHIFT	24
#define PCI_CB_LATENCY_MASK	0xFF
#define PCI_CB_LATENCY(x)(	\
	((x) >> PCI_CB_LATENCY_SHIFT) & PCI_CB_LATENCY_MASK)

/*
 * Cardbus socket registers
 */
#define CB_SOCKET_EVENT		0x00 /* Event */
#define CB_SOCKET_EVENT_CSTS		0x01 /* CARDSTS event occurs */
#define CB_SOCKET_EVENT_CD		0x06 /* CD event occurs */
#define CB_SOCKET_EVENT_CD1		0x02 /* CD1 event occurs */
#define CB_SOCKET_EVENT_CD2		0x04 /* CD2 event occurs */
#define CB_SOCKET_EVENT_POWER		0x08 /* Power cycle event occurs */

#define CB_SOCKET_MASK		0x04 /* Mask */
#define CB_SOCKET_MASK_CSTS		0x01 /* CARDSTS event mask */
#define CB_SOCKET_MASK_CD		0x06 /* CD event mask */
#define CB_SOCKET_MASK_POWER		0x08 /* Power cycle event mask */

#define CB_SOCKET_STAT		0x08 /* Present-state */
#define CB_SOCKET_STAT_CARDSTS		0x00000001 /* Card status change bit */
#define CB_SOCKET_STAT_CD1		0x00000002 /* Card detect 1 */
#define CB_SOCKET_STAT_CD2		0x00000004 /* Card detect 2 */
#define CB_SOCKET_STAT_CD		0x00000006 /* Card detect 1 and 2 */
#define CB_SOCKET_STAT_PWRCYCLE		0x00000008 /* Power cycle */
#define CB_SOCKET_STAT_16BIT		0x00000010 /* 16-bit card */
#define CB_SOCKET_STAT_CB		0x00000020 /* Cardbus card */
#define CB_SOCKET_STAT_IREQ		0x00000040 /* READY(~IREQ)//(~CINT) */
						   /* bit */
#define CB_SOCKET_STAT_NOTCARD		0x00000080 /* Inserted card is */
						   /* unrecognisable */
#define CB_SOCKET_STAT_DATALOST		0x00000100 /* Data lost */
#define CB_SOCKET_STAT_BADVCC		0x00000200 /* Bad Vcc Request */
#define CB_SOCKET_STAT_5VCARD		0x00000400 /* 5 V Card */
#define CB_SOCKET_STAT_3VCARD		0x00000800 /* 3.3 V Card */
#define CB_SOCKET_STAT_XVCARD		0x00001000 /* X.X V Card */
#define CB_SOCKET_STAT_YVCARD		0x00002000 /* Y.Y V Card */
#define CB_SOCKET_STAT_5VSOCK		0x10000000 /* 5 V Socket */
#define CB_SOCKET_STAT_3VSOCK		0x20000000 /* 3.3 V Socket */
#define CB_SOCKET_STAT_XVSOCK		0x40000000 /* X.X V Socket */
#define CB_SOCKET_STAT_YVSOCK		0x80000000 /* Y.Y V Socket */

#define CB_SOCKET_FORCE		0x0C /* Force event */
#define CB_SOCKET_FORCE_BADVCC		0x0200 /* Bad Vcc Request */

#define CB_SOCKET_CTRL		0x10 /* Control */
#define CB_SOCKET_CTRL_VPPMASK		0x007
#define CB_SOCKET_CTRL_VPP_OFF		0x000
#define CB_SOCKET_CTRL_VPP_12V		0x001
#define CB_SOCKET_CTRL_VPP_5V		0x002
#define CB_SOCKET_CTRL_VPP_3V		0x003
#define CB_SOCKET_CTRL_VPP_XV		0x004
#define CB_SOCKET_CTRL_VPP_YV		0x005

#define CB_SOCKET_CTRL_VCCMASK		0x070
#define CB_SOCKET_CTRL_VCC_OFF		0x000
#define CB_SOCKET_CTRL_VCC_5V		0x020
#define CB_SOCKET_CTRL_VCC_3V		0x030
#define CB_SOCKET_CTRL_VCC_XV		0x040
#define CB_SOCKET_CTRL_VCC_YV		0x050

#define CB_SOCKET_CTRL_STOPCLK	0x080

#define PCCBB_SOCKEVENT_BITS	"\020\001CSTS\002CD1\003CD2\004PWR"
#define PCCBB_SOCKSTATE_BITS	\
    "\020\001CSTS\002CD1\003CD3\004PWR\00516BIT\006CB\007CINT\010NOTA"	\
    "\011DLOST\012BADVCC\0135v\0143v\015Xv\016Yv\0355vS\0363vS\037XvS\040YvS"

/* PCI_BCR_INTR bits for generic PCI-CardBus bridge */
#define CB_BCR_RESET_ENABLE		0x00400000
#define CB_BCR_INTR_IREQ_ENABLE		0x00800000
#define CB_BCR_PREFETCH_MEMWIN0		0x01000000
#define CB_BCR_PREFETCH_MEMWIN1		0x02000000
#define CB_BCR_WRITE_POST_ENABLE	0x04000000

/* TI [14][245]xx */
#define PCI12XX_MMCTRL		0x84

/* TI 12xx/14xx/15xx (except 1250, 1251, 1251B/1450) */
#define PCI12XX_MFUNC			0x8c
#define PCI12XX_MFUNC_PIN0		0x0000000F
#define PCI12XX_MFUNC_PIN0_INTA		0x02
#define PCI12XX_MFUNC_PIN1		0x000000F0
#define PCI12XX_MFUNC_PIN1_INTB		0x20
#define PCI12XX_MFUNC_PIN2		0x00000F00
#define PCI12XX_MFUNC_PIN3		0x0000F000
#define PCI12XX_MFUNC_PIN3_INTSER	0x00001000
#define PCI12XX_MFUNC_PIN4		0x000F0000
#define PCI12XX_MFUNC_PIN5		0x00F00000
#define PCI12XX_MFUNC_PIN6		0x0F000000
#define PCI12XX_MFUNC_DEFAULT		PCI12XX_MFUNC_PIN3_INTSER

/*  PCI_CBCTRL bits for TI PCI113X */
#define PCI113X_CBCTRL_INT_SERIAL	0x040000
#define PCI113X_CBCTRL_INT_ISA		0x020000
#define PCI113X_CBCTRL_INT_MASK		0x060000
#define PCI113X_CBCTRL_RIENB		0x008000 /* Ring indicate output */
						 /* enable */
#define PCI113X_CBCTRL_ZVENAB		0x004000 /* ZV mode enable */
#define PCI113X_CBCTRL_PCI_IRQ_ENA	0x002000 /* PCI intr enable */
						 /* (funct and CSC) */
#define PCI113X_CBCTRL_PCI_INTR		0x001000 /* PCI functional intr req */
#define PCI113X_CBCTRL_PCI_CSC		0x000800 /* CSC intr route to PCI */
#define PCI113X_CBCTRL_PCI_CSC_D	0x000400 /* Unknown */
#define PCI113X_CBCTRL_SPK_ENA		0x000200 /* Speaker enable */
#define PCI113X_CBCTRL_INTR_DET		0x000100 /* Functional interrupt */
						 /* detect */

/*  PCI_CBCTRL bits for TI PCI12XX */
#define PCI12XX_SYSCTRL_INTRTIE		0x20000000u
#define PCI12XX_SYSCTRL_VCCPROT		0x200000
#define PCI12XX_SYSCTRL_PWRSAVE		0x000040
#define PCI12XX_SYSCTRL_SUBSYSRW	0x000020
#define PCI12XX_SYSCTRL_CB_DPAR		0x000010
#define PCI12XX_SYSCTRL_CDMA_EN		0x000008
#define PCI12XX_SYSCTRL_KEEPCLK		0x000002
#define PCI12XX_SYSCTRL_RIMUX		0x000001
#define PCI12XX_CBCTRL_CSC		0x20000000u
#define PCI12XX_CBCTRL_ASYNC_CSC	0x01000000u
#define PCI12XX_CBCTRL_INT_SERIAL	0x060000
#define PCI12XX_CBCTRL_INT_PCI_SERIAL	0x040000
#define PCI12XX_CBCTRL_INT_ISA		0x020000
#define PCI12XX_CBCTRL_INT_PCI		0x000000
#define PCI12XX_CBCTRL_INT_MASK		0x060000
#define PCI12XX_CBCTRL_RIENB		0x008000 /* Ring indicate output */
						 /* enable */
#define PCI12XX_CBCTRL_ZVENAB		0x004000 /* ZV mode enable */
#define PCI12XX_CBCTRL_AUD2MUX		0x000400 /* Unknown */
#define PCI12XX_CBCTRL_SPK_ENA		0x000200 /* Speaker enable */
#define PCI12XX_CBCTRL_INTR_DET		0x000100 /* Functional interrupt */
						 /* detect */


/* PCI_BCR_INTR additional bit for Rx5C46[567] */
#define CB_BCRI_RL_3E0_ENA		0x08000000
#define CB_BCRI_RL_3E2_ENA		0x10000000

/*
 * Special register definition for Toshiba ToPIC95/97
 * These values are borrowed from pcmcia-cs/Linux.
 */
#define TOPIC_SOCKET_CTRL	0x90
#define TOPIC_SOCKET_CTRL_SCR_IRQSEL	0x00000001 /* PCI intr */

#define TOPIC_SLOT_CTRL		0xA0
#define TOPIC_SLOT_CTRL_SLOTON		0x00000080
#define TOPIC_SLOT_CTRL_SLOTEN		0x00000040
#define TOPIC_SLOT_CTRL_ID_LOCK		0x00000020
#define TOPIC_SLOT_CTRL_ID_WP		0x00000010
#define TOPIC_SLOT_CTRL_PORT_MASK	0x0000000C
#define TOPIC_SLOT_CTRL_PORT_SHIFT	2
#define TOPIC_SLOT_CTRL_OSF_MASK	0x00000003
#define TOPIC_SLOT_CTRL_OSF_SHIFT	0

#define TOPIC_SLOT_CTRL_INTB		0x00002000
#define TOPIC_SLOT_CTRL_INTA		0x00001000
#define TOPIC_SLOT_CTRL_INT_MASK	0x00003000
#define TOPIC_SLOT_CTRL_CLOCK_MASK	0x00000C00
#define TOPIC_SLOT_CTRL_CLOCK_2		0x00000800 /* PCI Clock/2 */
#define TOPIC_SLOT_CTRL_CLOCK_1		0x00000400 /* PCI Clock */
#define TOPIC_SLOT_CTRL_CLOCK_0		0x00000000 /* No clock */
#define TOPIC97_SLOT_CTRL_STSIRQP	0x00000400 /* Status change intr */
						   /* pulse */
#define TOPIC97_SLOT_CTRL_IRQP		0x00000200 /* Function intr pulse */
#define TOPIC97_SLOT_CTRL_PCIINT	0x00000100 /* Intr routing to PCI INT */

#define TOPIC_SLOT_CTRL_CARDBUS		0x80000000
#define TOPIC_SLOT_CTRL_VS1		0x04000000
#define TOPIC_SLOT_CTRL_VS2		0x02000000
#define TOPIC_SLOT_CTRL_SWDETECT	0x01000000

#define TOPIC_REG_CTRL		0x00A4
#define TOPIC_REG_CTRL_RESUME_RESET	0x80000000
#define TOPIC_REG_CTRL_REMOVE_RESET	0x40000000
#define TOPIC97_REG_CTRL_CLKRUN_ENA	0x20000000
#define TOPIC97_REG_CTRL_TESTMODE	0x10000000
#define TOPIC97_REG_CTRL_IOPLUP		0x08000000
#define TOPIC_REG_CTRL_BUFOFF_PWROFF	0x02000000
#define TOPIC_REG_CTRL_BUFOFF_SIGOFF	0x01000000
#define TOPIC97_REG_CTRL_CB_DEV_MASK	0x0000F800
#define TOPIC97_REG_CTRL_CB_DEV_SHIFT	11
#define TOPIC97_REG_CTRL_RI_DISABLE	0x00000004
#define TOPIC97_REG_CTRL_CAUDIO_OFF	0x00000002
#define TOPIC_REG_CTRL_CAUDIO_INVERT	0x00000001

/*
 * Additional O2Micro registers
 *
 * 'reserved' register at 0x94/D4. allows setting read prefetch and write
 * bursting. read prefetching for example makes the RME Hammerfall DSP
 * working. for some bridges it is at 0x94, for others at 0xD4. it's
 * ok to write to both registers on all O2 bridges.
 * from Eric Still, 02Micro.
 */
#define O2MICRO_RESERVED1	0x94
#define O2MICRO_RESERVED2	0xD4
#define O2MICRO_RES_READ_PREFETCH	0x02
#define O2MICRO_RES_WRITE_BURST		0x08

#endif /* _DEV_PCI_PCCBBREG_H_ */
