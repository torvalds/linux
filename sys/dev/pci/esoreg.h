/*	$OpenBSD: esoreg.h,v 1.2 2007/11/11 01:32:52 jakemsr Exp $	*/
/*	$NetBSD: esoreg.h,v 1.6 2004/05/25 20:59:37 kleink Exp $	*/

/*
 * Copyright (c) 1999 Klaus J. Klein
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _DEV_PCI_ESOREG_H_
#define _DEV_PCI_ESOREG_H_

/*
 * PCI Configuration Registers
 */
#define ESO_PCI_BAR_IO		0x10	/* I/O space base address */
#define ESO_PCI_BAR_SB		0x14	/* SB I/O space base address */
#define ESO_PCI_BAR_VC		0x18	/* VC I/O space base address */
#define ESO_PCI_BAR_MPU		0x1c	/* MPU-401 I/O space base address */
#define ESO_PCI_BAR_GAME	0x20	/* Game port I/O space base address */
#define ESO_PCI_S1C		0x50	/* Solo-1 Configuration */
#define  ESO_PCI_S1C_IRQP_MASK	0x0006000 /* ISA IRQ emulation policy */
#define  ESO_PCI_S1C_DMAP_MASK	0x0000700 /* DMA policy */
#define ESO_PCI_DDMAC		0x60	/* DDMA Control base address */
#define  ESO_PCI_DDMAC_DE	0x01	 /* Distributed DMA enable */

/* PCI Revision IDs of the Solo-1 PCI AudioDrive family */
#define ESO_PCI_REVISION_ES1938	0x00	/* ES1938 */
#define ESO_PCI_REVISION_ES1946	0x01	/* ES1946 */

/*
 * Check the validity of a PCI I/O space base address for use in
 * ESO_PCI_DDMAC; see the relevant comment in the attach function.
 */
#define ESO_VALID_DDMAC_BASE(addr) \
    (((addr) & 0x03ff) != 0)

/*
 * I/O Port offsets
 */
/* I/O Device ports */
#define ESO_IO_A2DMAA		0x00	/* [RW] Audio 2 b/c DMA address */
#define ESO_IO_A2DMAC		0x04	/* [RW] Audio 2 b/c DMA count */
#define ESO_IO_A2DMAM		0x06	/* [RW] Solo-1 mode register */
#define  ESO_IO_A2DMAM_DMAENB	0x02	 /* DMA enable */
#define  ESO_IO_A2DMAM_AUTO	0x08	 /* Auto-Initialize DMA enable */
#define ESO_IO_IRQCTL		0x07	/* [RW] IRQ control register */
#define  ESO_IO_IRQCTL_A1IRQ	0x10	 /* Audio 1 IRQ */
#define  ESO_IO_IRQCTL_A2IRQ	0x20	 /* Audio 2 IRQ */
#define  ESO_IO_IRQCTL_HVIRQ	0x40	 /* Hardware volume IRQ */
#define  ESO_IO_IRQCTL_MPUIRQ	0x80	 /* MPU-401 IRQ */
#define  ESO_IO_IRQCTL_MASK	0xf0	 /* all of the above */

/* Audio/FM Device ports */
#define ESO_SB_STATUS		0x00	/* [R]  FM Status */
#define ESO_SB_LBA		0x00	/* [W]  FM Low Bank Address */
#define ESO_SB_LBDW		0x01	/* [W]  FM Low Bank Data Write */
#define ESO_SB_HBA		0x02	/* [W]  FM High Bank Address */
#define ESO_SB_HBDW		0x03	/* [W]  FM High Bank Data Write */
#define ESO_SB_MIXERADDR	0x04	/* [RW] Mixer Address Register */
#define ESO_SB_MIXERDATA	0x05	/* [RW] Mixer Data Register */
#define ESO_SB_RESET		0x06	/* [W]  Reset */
#define  ESO_SB_RESET_SW	0x01	 /* SW Reset */
#define  ESO_SB_RESET_FIFO	0x02	 /* FIFO Reset */
#define ESO_SB_STATUSFLAGS	0x06	/* [R]  Status Flags */
#define ESO_SB_PMR		0x07	/* [RW] Power Management Register */
#define ESO_SB_RDR		0x0a	/* [R]  Read Data Register */
#define  ESO_SB_RDR_RESETMAGIC	0xaa	 /* Indicates SW reset completion */
#define ESO_SB_WDR		0x0c	/* [W]  Write Data Register */
#define ESO_SB_RSR		0x0c	/* [R]  Read Status Register */
#define  ESO_SB_RSR_BUSY	0x80	 /* WDR not available or Solo-1 busy */
#define ESO_SB_RBSR		0x0e	/* [R]  Read Buffer Status Register */
#define  ESO_SB_RBSR_RDAV	0x80	 /* Data available in RDR */
#define ESO_SB_PIOAFR		0x0f	/* [RW] PIO Access to FIFO Register */

/* (Audio 1) DMAC Device ports */
#define ESO_DMAC_DMAA		0x00	/* [RW] DMA Current/Base Address */
#define ESO_DMAC_DMAC		0x04	/* [RW] DMA Current/Base Count */
#define ESO_DMAC_COMMAND	0x08	/* [W]  DMA Command */
#define  ESO_DMAC_COMMAND_ENB	0x04	 /* Controller enable */
#define  ESO_DMAC_COMMAND_DREQPOL 0x40	 /* DREQ signal polarity */
#define  ESO_DMAC_COMMAND_DACKPOL 0x80	 /* DACK signal polarity */
#define ESO_DMAC_STATUS		0x08	/* [R]  DMA Status */
#define ESO_DMAC_MODE		0x0b	/* [W]  DMA Mode */
#define ESO_DMAC_CLEAR		0x0d	/* [W]  DMA Master Clear */
#define ESO_DMAC_MASK		0x0f	/* [RW] DMA Mask */
#define  ESO_DMAC_MASK_MASK	0x01	/*  Mask DREQ */

/* Controller commands */
#define ESO_CMD_RCR		0xc0	/* Read ext. controller registers */
#define ESO_CMD_EXTENB		0xc6	/* Enable Solo-1 Extension commands */
#define ESO_CMD_EXTDIS		0xc7	/* Disable Solo-1 Extension commands */

/* Mixer registers */
#define ESO_MIXREG_RESET	0x00	/* Reset mixer registers */
#define  ESO_MIXREG_RESET_RESET	0x00	 /* Any value will do */
#define ESO_MIXREG_PVR_MIC	0x1a	/* Playback mixer: Microphone */
#define ESO_MIXREG_ERS		0x1c	/* Extended record source */
#define  ESO_MIXREG_ERS_MIC	0x00	 /* Microphone */
#define  ESO_MIXREG_ERS_CD	0x02	 /* CD */
#define  ESO_MIXREG_ERS_MIC2	0x04	 /* Microphone (again?) */
#define  ESO_MIXREG_ERS_MIXER	0x05	 /* Record mixer */
#define  ESO_MIXREG_ERS_LINE	0x06	 /* Line */
#define  ESO_MIXREG_ERS_MUTE	0x10	 /* Mutes input to filters for rec */
#define ESO_MIXREG_PCSVR	0x3c	/* PC Speaker Volume Register */
#define ESO_MIXREG_PCSVR_RESV	0xf8	 /* Reserved */
#define ESO_MIXREG_PVR_SYNTH	0x36	/* Playback mixer: FM/Synth */
#define ESO_MIXREG_PVR_CD	0x38	/* Playback mixer: AuxA/CD */
#define ESO_MIXREG_PVR_AUXB	0x3a	/* Playback mixer: AuxB */
#define ESO_MIXREG_PCSPKR_VOL	0x3c	/* PC speaker volume */ 
#define ESO_MIXREG_PVR_LINE	0x3e	/* Playback mixer: Line */
#define ESO_MIXREG_SPAT		0x50	/* Spatializer Enable and Mode */
#define  ESO_MIXREG_SPAT_MONO	0x02	/* 0 = Stereo in, 1 = Mono in */
#define  ESO_MIXREG_SPAT_RSTREL	0x04	/* 0 = reset, 1 = release from reset */
#define  ESO_MIXREG_SPAT_ENB	0x08	/* Spatializer Enable */
#define ESO_MIXREG_SPATLVL	0x52	/* Spatializer Level */
#define ESO_MIXREG_LMVM		0x60	/* Left Master Volume and Mute */
#define  ESO_MIXREG_LMVM_MUTE	0x40	 /* Mute enable */
#define ESO_MIXREG_LHVCC	0x61	/* Left Hardware Volume Control Ctr */
#define ESO_MIXREG_RMVM		0x62	/* Right Master Volume and Mute */
#define  ESO_MIXREG_RMVM_MUTE	0x40	 /* Mute enable */
#define ESO_MIXREG_RHVCC	0x63	/* Left Hardware Volume Control Ctr */
#define ESO_MIXREG_MVCTL	0x64	/* Master Volume Control */
#define  ESO_MIXREG_MVCTL_HVIRQM 0x02	 /* Hardware Volume Control intr mask */
#define  ESO_MIXREG_MVCTL_MPUIRQM 0x40	 /* MPU-401 interrupt unmask */
#define  ESO_MIXREG_MVCTL_SPLIT 0x80	 /* Split xHVCC/xMVM registers */
#define ESO_MIXREG_CHVIR	0x66	/* Clear Hardware Volume IRQ */
#define ESO_MIXREG_CHVIR_CHVIR	0x00	 /* Any value will do */
#define ESO_MIXREG_RVR_MIC	0x68	/* Record mixer: Microphone */
#define ESO_MIXREG_RVR_A2	0x69	/* Record mixer: Audio 2 */
#define ESO_MIXREG_RVR_CD	0x6a	/* Record mixer: AuxA/CD */
#define ESO_MIXREG_RVR_SYNTH	0x6b	/* Record mixer: FM/Synth */
#define ESO_MIXREG_RVR_AUXB	0x6c	/* Record mixer: AuxB */
#define ESO_MIXREG_PVR_MONO	0x6d	/* Playback mixer: Mono In */
#define ESO_MIXREG_RVR_LINE	0x6e	/* Record mixer: Line */
#define ESO_MIXREG_RVR_MONO	0x6f	/* Record mixer: Mono In */
#define ESO_MIXREG_A2SRG	0x70	/* Audio 2 Sample Rate Generator */
#define ESO_MIXREG_A2MODE	0x71	/* Audio 2 Mode */
#define  ESO_MIXREG_A2MODE_ASYNC 0x02	 /* A2 SRG and FLTDIV async wrt A1 */
#define  ESO_MIXREG_A2MODE_NEWA1 0x20	 /* New-style SRG for Audio 1 */
#define ESO_MIXREG_A2FLTDIV	0x72	/* Audio 2 Filter Rate Divider */
#define ESO_MIXREG_A2TCRLO	0x74	/* Audio 2 Transfer Count Reload LO */
#define ESO_MIXREG_A2TCRHI	0x76	/* Audio 2 Transfer Count Reload HI */
#define ESO_MIXREG_A2C1		0x78	/* Audio 2 Control 1 */
#define  ESO_MIXREG_A2C1_FIFOENB 0x01	 /* FIFO enable */
#define  ESO_MIXREG_A2C1_DMAENB	0x02	 /* DMA enable */
#define  ESO_MIXREG_A2C1_RESV0	0xcc	 /* Reserved, always write 0 XXXb5? */
#define  ESO_MIXREG_A2C1_AUTO	0x10	 /* Auto-initialize mode */
#define ESO_MIXREG_A2C2		0x7a	/* Audio 2 Control 2 */
#define  ESO_MIXREG_A2C2_16BIT	0x01	 /* 1 = 16-bit, 0 = 8-bit */
#define  ESO_MIXREG_A2C2_STEREO	0x02	 /* 1 = Stereo, 0 = Mono */
#define  ESO_MIXREG_A2C2_SIGNED	0x04	 /* 1 = Signed data, 0 = unsigned */
#define  ESO_MIXREG_A2C2_RESV0	0x38	 /* Reserved, always write 0 */
#define  ESO_MIXREG_A2C2_IRQM	0x40	 /* IRQ mask */
#define  ESO_MIXREG_A2C2_IRQ	0x80	 /* IRQ latch */
#define ESO_MIXREG_PVR_A2	0x7c	/* Playback mixer: Audio 2 */
#define ESO_MIXREG_MPM		0x7d	/* Microphone Preamp, Mono In/Out */
#define  ESO_MIXREG_MPM_MIBYPASS 0x01	 /* MONO_IN mixer bypass */
#define  ESO_MIXREG_MPM_MOMASK  0x06	 /* MONO_OUT value mask */
#define  ESO_MIXREG_MPM_MOMUTE  0x00	  /* MONO_OUT mute */
#define  ESO_MIXREG_MPM_MOCINR  0x02	  /* MONO_OUT source CIN_R */
#define  ESO_MIXREG_MPM_MOA2R	0x04	  /* MONO_OUT source Audio 2 Right */
#define  ESO_MIXREG_MPM_MOREC	0x06	  /* MONO_OUT source record stage */
#define  ESO_MIXREG_MPM_PREAMP	0x08	 /* Preamp enable */
#define  ESO_MIXREG_MPM_RESV0	0xf0	 /* Reserved, always write 0 */

/* Controller registers */
#define ESO_CTLREG_SRG		0xa1	/* Sample Rate Generator */
#define ESO_CTLREG_FLTDIV	0xa2	/* Filter Rate Divider */
#define ESO_CTLREG_A1TCRLO	0xa4	/* Audio 1 Transfer Count Reload LO */
#define ESO_CTLREG_A1TCRHI	0xa5	/* Audio 1 Transfer Count Reload HI */
#define ESO_CTLREG_ACTL		0xa8	/* Analog Control */
#define  ESO_CTLREG_ACTL_STEREO	0x01	 /* DMA converters stereo */
#define  ESO_CTLREG_ACTL_MONO	0x02	 /* DMA converters mono */
#define  ESO_CTLREG_ACTL_RESV1	0x10	 /* Reserved, always write 1 */
#define  ESO_CTLREG_ACTL_RESV0	0xe4	 /* Reserved, always write 0 */
#define  ESO_CTLREG_ACTL_RECMON	0x08	 /* Record Monitor enable */
#define ESO_CTLREG_LAIC		0xb1	/* Legacy Audio Interrupt Control */
#define  ESO_CTLREG_LAIC_PINENB	0x10	 /* Interrupt pin enable */
#define  ESO_CTLREG_LAIC_EXTENB	0x40	 /* Extended mode IRQ enable */
#define ESO_CTLREG_DRQCTL	0xb2	/* DRQ Control */
#define  ESO_CTLREG_DRQCTL_ENB1	0x10	 /* Supposedly no function, but ... */
#define  ESO_CTLREG_DRQCTL_EXTENB 0x40	 /* Extended mode DRQ enable */
#define ESO_CTLREG_RECLVL	0xb4	/* Record Level */
#define ESO_CTLREG_A1C1		0xb7	/* Audio 1 Control 1 */
#define  ESO_CTLREG_A1C1_LOAD	0x01	 /* Generate load signal */
#define  ESO_CTLREG_A1C1_RESV0	0x02	 /* Reserved, always write 0 */
#define  ESO_CTLREG_A1C1_16BIT	0x04	 /* 1 = 16-bit, 0 = 8-bit */
#define  ESO_CTLREG_A1C1_STEREO	0x08	 /* DMA FIFO Stereo */
#define  ESO_CTLREG_A1C1_RESV1	0x10	 /* Reserved, always write 1 */
#define  ESO_CTLREG_A1C1_SIGNED	0x20	 /* 1 = Signed data, 0 = unsigned */
#define  ESO_CTLREG_A1C1_MONO	0x40	 /* DMA FIFO Mono */
#define  ESO_CTLREG_A1C1_FIFOENB 0x80	 /* DMA FIFO enable */
#define ESO_CTLREG_A1C2		0xb8	/* Audio 1 Control 2 */
#define  ESO_CTLREG_A1C2_DMAENB	0x01	 /* DMA enable */
#define  ESO_CTLREG_A1C2_READ	0x02	 /* 1 = DMA read/ADC, 0 = write/DAC */
#define  ESO_CTLREG_A1C2_AUTO	0x04	 /* Auto-initialize DMA enable */
#define  ESO_CTLREG_A1C2_ADC	0x08	 /* 1 = ADC mode, 0 = DAC mode */
#define ESO_CTLREG_A1TT		0xb9	/* Audio 1 Transfer Type */
#define  ESO_CTLREG_A1TT_SINGLE	0x00	 /* Single-Transfer */
#define  ESO_CTLREG_A1TT_DEMAND2 0x01	 /* Demand-Transfer, 2 bytes/DREQ */
#define  ESO_CTLREG_A1TT_DEMAND4 0x02	 /* Demand-Transfer, 4 bytes/DREQ */

/*
 * Sample rate related constants.
 * Note: the use of these clock sources must be explicitly enabled for Audio 1.
 */
#define ESO_MINRATE		6000
#define ESO_MAXRATE		48000
#define ESO_CLK0		793800L	/* Clock source 0 frequency */
#define ESO_CLK1		768000L	/* Clock source 1 frequency */
#define ESO_CLK1_SELECT		0x80	/* MSb of divider selects clock src */

/*
 * Upper bounds on several polling loop iterations.
 */
#define ESO_RESET_TIMEOUT	5000
#define ESO_RDR_TIMEOUT		5000
#define ESO_WDR_TIMEOUT		5000

/*
 * Mixer state data conversions.
 */
/* Truncate MI 8-bit precision gain values to the width of chip registers. */
#define ESO_GAIN_TO_3BIT(x)		((x) & 0xe0)
#define ESO_GAIN_TO_4BIT(x)		((x) & 0xf0)
#define ESO_GAIN_TO_6BIT(x)		((x) & 0xfc)
/* Convert two 4-bit gain values to standard mixer stereo register layout. */
#define ESO_4BIT_GAIN_TO_STEREO(l,r)	((l) | ((r) >> 4))

#endif /* !_DEV_PCI_ESOREG_H_ */
