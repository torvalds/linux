/*	$OpenBSD: am7930reg.h,v 1.6 2011/09/03 20:03:29 miod Exp $	*/
/* $NetBSD: am7930reg.h,v 1.7 2005/12/11 12:21:25 christos Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)bsd_audioreg.h	8.1 (Berkeley) 6/11/93
 */

/*
 * Am79C30A direct registers
 */

#define AM7930_DREG_CR		0	/* command register (wo) */
#define AM7930_DREG_IR		0	/* interrupt register (ro) */
#define		AM7930_IR_DTTHRSH	0x01	/* D-channel TX empty */
#define		AM7930_IR_DRTHRSH	0x02	/* D-channel RX avail */
#define		AM7930_IR_DSRI		0x04	/* D-channel packet status */
#define		AM7930_IR_DERI		0x08	/* D-channel error */
#define		AM7930_IR_BBUFF		0x10	/* Bb or Bc byte avail/empty */
#define		AM7930_IR_LSRI		0x20	/* LIU status */
#define		AM7930_IR_DSR2I		0x40	/* D-channel buffer status */
#define		AM7930_IR_PPMF		0x80	/* Multiframe or PP */
#define AM7930_DREG_DR		1	/* data register (rw) */
#define AM7930_DREG_DSR1	2	/* D-channel status register 1 (ro) */
#define AM7930_DREG_DER		3	/* D-channel error register (ro) */
#define AM7930_DREG_DCTB	4	/* D-channel transmit register (wo) */
#define AM7930_DREG_DCRB	4	/* D-channel receive register (ro) */
#define AM7930_DREG_BBTB	5	/* Bb-channel transmit register (wo) */
#define AM7930_DREG_BBRB	5	/* Bb-channel receive register (ro) */
#define AM7930_DREG_BCTB	6	/* Bc-channel transmit register (wo) */
#define AM7930_DREG_BCRB	6	/* Bc-channel receive register (ro) */
#define AM7930_DREG_DSR2	7	/* D-channel status register 2 (ro) */

#define	AM7930_DREG_SIZE	8

/*
 * Am79C30A indirect registers
 */

/* Initialisation registers */

#define AM7930_IREG_INIT	0x21
#define AM7930_IREG_INIT2	0x20
/* power mode selection */
#define		AM7930_INIT_PMS_IDLE		0x00
#define		AM7930_INIT_PMS_ACTIVE		0x01
#define		AM7930_INIT_PMS_ACTIVE_DATA	0x02
#define		AM7930_INIT_PMS_MASK		0x03
/* interrupt selection */
#define		AM7930_INIT_INT_ENABLE		0x00
#define		AM7930_INIT_INT_DISABLE		0x04
#define		AM7930_INIT_INT_MASK		0x04
/* clock divider selection */
#define		AM7930_INIT_CDS_DIV2		0x00
#define		AM7930_INIT_CDS_DIV1		0x08
#define		AM7930_INIT_CDS_DIV4		0x10
#define		AM7930_INIT_CDS_DIV3		0x20
#define		AM7930_INIT_CDS_MASK		0x38
/* abort selection */
#define		AM7930_INIT_AS_RX		0x40
#define		AM7930_INIT_AS_NRX		0x00
#define		AM7930_INIT_AS_TX		0x80
#define		AM7930_INIT_AS_NTX		0x00
#define		AM7930_INIT_AS_MASK		0xc0

/* Line Interface Unit registers */

#define AM7930_IREG_LIU_LSR	0xa1	/* LIU status (ro) */
#define AM7930_IREG_LIU_LPR	0xa2	/* LIU priority (rw) */
#define AM7930_IREG_LIU_LMR1	0xa3	/* LIU mode register 1 (rw) */
#define AM7930_IREG_LIU_LMR2	0xa4	/* LIU mode register 2 (rw) */
#define AM7930_IREG_LIU_2_4	0xa5
#define AM7930_IREG_LIU_MF	0xa6	/* Multiframe (rw) */
#define AM7930_IREG_LIU_MFSB	0xa7	/* Multiframe S-bit/status (ro) */
#define AM7930_IREG_LIU_MFQB	0xa8	/* Multiframe Q-bit buffer (wo) */

/* Multiplexer registers */

#define AM7930_IREG_MUX_MCR1	0x41	/* MUX command register 1 (rw) */
#define AM7930_IREG_MUX_MCR2	0x42	/* MUX command register 2 (rw) */
#define AM7930_IREG_MUX_MCR3	0x43	/* MUX command register 3 (rw) */
#define		AM7930_MCRCHAN_NC		0x00
#define		AM7930_MCRCHAN_B1		0x01
#define		AM7930_MCRCHAN_B2		0x02
#define		AM7930_MCRCHAN_BA		0x03
#define		AM7930_MCRCHAN_BB		0x04
#define		AM7930_MCRCHAN_BC		0x05
#define		AM7930_MCRCHAN_BD		0x06
#define		AM7930_MCRCHAN_BE		0x07
#define		AM7930_MCRCHAN_BF		0x08
#define AM7930_IREG_MUX_MCR4	0x44	/* MUX command register 4 (rw) */
#define		AM7930_MCR4_INT_ENABLE		(1 << 3)
#define		AM7930_MCR4_SWAPBB		(1 << 4)
#define		AM7930_MCR4_SWAPBC		(1 << 5)
#define AM7930_IREG_MUX_1_4	0x45

/* Main Audio Processor registers */

#define AM7930_IREG_MAP_X	0x61	/* X filter coefficient (rw) */
#define AM7930_IREG_MAP_R	0x62	/* R filter coefficient (rw) */
#define AM7930_IREG_MAP_GX	0x63	/* GX gain coefficient (rw) */
#define AM7930_IREG_MAP_GR	0x64	/* GR gain coefficient (rw) */
#define AM7930_IREG_MAP_GER	0x65	/* GER gain coefficient (rw) */
#define AM7930_IREG_MAP_STG	0x66	/* Sidetone gain coefficient (rw) */
#define AM7930_IREG_MAP_FTGR	0x67	/* Frequency tone generator 1,2 (rw) */
#define AM7930_IREG_MAP_ATGR	0x68	/* Amplitude tone generator 1,2 (rw) */
#define AM7930_IREG_MAP_MMR1	0x69	/* MAP mode register 1 (rw) */
#define		AM7930_MMR1_ALAW	0x01
#define		AM7930_MMR1_GX		0x02
#define		AM7930_MMR1_GR		0x04
#define		AM7930_MMR1_GER		0x08
#define		AM7930_MMR1_X		0x10
#define		AM7930_MMR1_R		0x20
#define		AM7930_MMR1_STG		0x40
#define		AM7930_MMR1_LOOP	0x80
#define AM7930_IREG_MAP_MMR2	0x6a	/* MAP mode register 2 (rw) */
#define		AM7930_MMR2_AINB	0x01
#define		AM7930_MMR2_LS		0x02
#define		AM7930_MMR2_DTMF	0x04
#define		AM7930_MMR2_GEN		0x08
#define		AM7930_MMR2_RNG		0x10
#define		AM7930_MMR2_DIS_HPF	0x20
#define		AM7930_MMR2_DIS_AZ	0x40
#define AM7930_IREG_MAP_1_10	0x6b
#define AM7930_IREG_MAP_MMR3	0x6c	/* MAP mode register 3 (rw) */
#define		AM7930_MMR3_BOTH	0x02
#define		AM7930_MMR3_MBZ		0x01
#define		AM7930_MMR3_GA		0x70
#define		AM7930_MMR3_GA0		0x00
#define		AM7930_MMR3_GA6		0x10
#define		AM7930_MMR3_GA12	0x20
#define		AM7930_MMR3_GA18	0x30
#define		AM7930_MMR3_GA24	0x40
#define		AM7930_MMR3_MUTE	0x08
#define		AM7930_MMR3_STR		0x01
#define AM7930_IREG_MAP_STRA	0x6d	/* Second tone ringer amplitude (rw) */
#define AM7930_IREG_MAP_STRF	0x6e	/* Second tone ringer frequency (rw) */

/* Data Link Controller registers */

#define AM7930_IREG_DLC_FRAR123	0x81	/* First rcvd byte address 123 (rw) */
#define AM7930_IREG_DLC_SRAR123 0x82	/* Second rcvd byte address 123 (rw) */
#define AM7930_IREG_DLC_TAR	0x83	/* Transmit address (rw) */
#define AM7930_IREG_DLC_DRLR	0x84	/* D-channel receive byte limit (rw) */
#define AM7930_IREG_DLC_DTCR	0x85	/* D-channel transmit byte count (rw)*/
#define AM7930_IREG_DLC_DMR1	0x86	/* D-channel mode register 1 (rw) */
#define AM7930_IREG_DLC_DMR2	0x87	/* D-channel mode register 2 (rw) */
#define AM7930_IREG_DLC_1_7	0x88
#define AM7930_IREG_DLC_DRCR	0x89	/* D-channel receive byte count (ro) */
#define AM7930_IREG_DLC_RNGR1	0x8a	/* Random number generator LSB (rw) */
#define AM7930_IREG_DLC_RNGR2	0x8b	/* Random number generator MSB (rw) */
#define AM7930_IREG_DLC_FRAR4	0x8c	/* First rcvd byte address 4 (rw) */
#define AM7930_IREG_DLC_SRAR4	0x8d	/* Second rcvd byte address 4 (rw) */
#define AM7930_IREG_DLC_DMR3	0x8e	/* D-channel mode register 3 (rw) */
#define AM7930_IREG_DLC_DMR4	0x8f	/* D-channel mode register 4 (rw) */
#define AM7930_IREG_DLC_12_15	0x90
#define AM7930_IREG_DLC_ASR	0x91	/* Address status register (ro) */
#define AM7930_IREG_DLC_EFCR	0x92	/* Extended FIFO control (rw) */

/* Peripheral Port registers */

#define AM7930_IREG_PP_PPCR1	0xc0	/* Peripheral port control 1 (rw) */
#define		AM7930_PPCR1_DISABLE	0x00
#define		AM7930_PPCR1_SBP	0x01
#define		AM7930_PPCR1_IOM2SL	0x10
#define		AM7930_PPCR1_IOM2MA	0x11
#define AM7930_IREG_PP_PPSR	0xc1	/* Peripheral port control 2 (ro) */
#define AM7930_IREG_PP_PPIER	0xc2	/* Peripheral port intr enable (rw) */
#define AM7930_IREG_PP_MTDR	0xc3	/* monitor transmit data (wo) */
#define AM7930_IREG_PP_MRDR	0xc3	/* monitor receive data (ro) */
#define AM7930_IREG_PP_CITDR0	0xc4	/* C/I transmit data register 0 (wo) */
#define AM7930_IREG_PP_CIRDR0	0xc4	/* C/I receive data register 0 (ro) */
#define AM7930_IREG_PP_CITDR1	0xc5	/* C/I transmit data register 1 (wo) */
#define AM7930_IREG_PP_CIRDR1	0xc5	/* C/I receive data register 1 (ro) */
#define AM7930_IREG_PP_PPCR2	0xc8	/* Peripheral port control 2 (rw) */
