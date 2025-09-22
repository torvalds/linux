/*	$OpenBSD: z8530reg.h,v 1.8 2017/10/19 19:07:48 jmc Exp $	*/
/*	$NetBSD: z8530reg.h,v 1.12 2005/12/11 12:21:29 christos Exp $ */

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
 *	@(#)zsreg.h	8.1 (Berkeley) 6/11/93
 */

/*
 * Zilog SCC registers, as implemented on the Sun-4c.
 *
 * Each Z8530 implements two channels (called `a' and `b').
 *
 * The damnable chip was designed to fit on Z80 I/O ports, and thus
 * has everything multiplexed out the wazoo.  We have to select
 * a register, then read or write the register, and so on.  Worse,
 * the parameter bits are scattered all over the register space.
 * This thing is full of `miscellaneous' control registers.
 *
 * Worse yet, the registers have incompatible functions on read
 * and write operations.  We describe the registers below according
 * to whether they are `read registers' (RR) or `write registers' (WR).
 * As if this were not enough, some of the channel B status bits show
 * up in channel A, and vice versa.  The blasted thing shares write
 * registers 2 and 9 across both channels, and reads registers 2 and 3
 * differently for the two channels.  We can, however, ignore this much
 * of the time.
 *
 * This file also includes flags for the Z85C30 and Z85230 enhanced scc.
 * The CMOS 8530 includes extra SDLC functionality, and is used in a
 * number of Macs (often in the Z85C80, an 85C30 combined w/ a SCSI
 * controller). -wrs
 *
 * Some of the names in this files were chosen to make the hsis driver
 * work unchanged (which means that they will match some in SunOS).
 *
 * `S.C.' stands for Special Condition, which is any of these:
 *	receiver overrun	(aka silo overflow)
 *	framing error		(missing stop bit, etc)
 *	end of frame		(in synchronous modes)
 *	parity error		(when `parity error is S.C.' is set)
 *
 * Registers with only a single `numeric value' get a name.
 * Other registers hold bits and are only numbered; the bit
 * definitions imply the register number (see below).
 *
 * We never use the receive and transmit data registers as
 * indirects (choosing instead the zc_data register), so they
 * are not defined here.
 */
#define	ZSRR_IVEC	2	/* interrupt vector (channel 0) */
#define	ZSRR_IPEND	3	/* interrupt pending (ch. 0 only) */
#define	ZSRR_TXSYNC	6	/* sync transmit char (monosync mode) */
#define	ZSRR_RXSYNC	7	/* sync receive char (monosync mode) */
#define	ZSRR_SYNCLO	6	/* sync low byte (bisync mode) */
#define	ZSRR_SYNCHI	7	/* sync high byte (bisync mode) */
#define	ZSRR_SDLC_ADDR	6	/* SDLC address (SDLC mode) */
#define	ZSRR_SDLC_FLAG	7	/* SDLC flag 0x7E (SDLC mode) */
#define	ZSRR_BAUDLO	12	/* baud rate generator (low half) */
#define	ZSRR_BAUDHI	13	/* baud rate generator (high half) */
#define	ZSRR_ENHANCED	14	/* read address of WR7' - yes, it's not 7!*/

#define	ZSWR_IVEC	2	/* interrupt vector (shared) */
#define	ZSWR_TXSYNC	6	/* sync transmit char (monosync mode) */
#define	ZSWR_RXSYNC	7	/* sync receive char (monosync mode) */
#define	ZSWR_SYNCLO	6	/* sync low byte (bisync mode) */
#define	ZSWR_SYNCHI	7	/* sync high byte (bisync mode) */
#define	ZSWR_SDLC_ADDR	6	/* SDLC address (SDLC mode) */
#define	ZSWR_SDLC_FLAG	7	/* SDLC flag 0x7E (SDLC mode) */
#define	ZSWR_BAUDLO	12	/* baud rate generator (low half) */
#define	ZSWR_BAUDHI	13	/* baud rate generator (high half) */
#define	ZSWR_ENHANCED	7	/* write address of WR7' */

/*
 * Registers 0 through 7 may be written with any one of the 8 command
 * modifiers, and/or any one of the 4 reset modifiers, defined below.
 * To write registers 8 through 15, however, the command modifier must
 * always be `point high'.  Rather than track this bizzareness all over
 * the driver, we try to avoid using any modifiers, ever (but they are
 * defined here if you want them).
 */
#define	ZSM_RESET_TXUEOM	0xc0	/* reset xmit underrun / eom latch */
#define	ZSM_RESET_TXCRC		0x80	/* reset xmit crc generator */
#define	ZSM_RESET_RXCRC		0x40	/* reset recv crc checker */
#define	ZSM_NULL		0x00	/* nothing special */

#define	ZSM_RESET_IUS		0x38	/* reset interrupt under service */
#define	ZSM_RESET_ERR		0x30	/* reset error cond */
#define	ZSM_RESET_TXINT		0x28	/* reset xmit interrupt pending */
#define	ZSM_EI_NEXTRXC		0x20	/* enable int. on next rcvd char */
#define	ZSM_SEND_ABORT		0x18	/* send abort (SDLC) */
#define	ZSM_RESET_STINT		0x10	/* reset external/status interrupt */
#define	ZSM_POINTHIGH		0x08	/* `point high' (use r8-r15) */
#define	ZSM_NULL		0x00	/* nothing special */

/*
 * Commands for Write Register 0 (`Command Register').
 * These are just the command modifiers or'ed with register number 0
 * (which of course equals the command modifier).
 */
#define	ZSWR0_RESET_EOM		ZSM_RESET_TXUEOM
#define	ZSWR0_RESET_TXCRC	ZSM_RESET_TXCRC
#define	ZSWR0_RESET_RXCRC	ZSM_RESET_RXCRC
#define	ZSWR0_CLR_INTR		ZSM_RESET_IUS
#define	ZSWR0_RESET_ERRORS	ZSM_RESET_ERR
#define	ZSWR0_EI_NEXTRXC	ZSM_EI_NEXTRXC
#define	ZSWR0_SEND_ABORT	ZSM_SEND_ABORT
#define	ZSWR0_RESET_STATUS	ZSM_RESET_STINT
#define	ZSWR0_RESET_TXINT	ZSM_RESET_TXINT

/*
 * Bits in Write Register 1 (`Transmit/Receive Interrupt and Data
 * Transfer Mode Definition').  Note that bits 3 and 4 are taken together
 * as a single unit, and bits 5 and 6 are useful only if bit 7 is set.
 */
#define	ZSWR1_REQ_WAIT		0x80	/* WAIT*-REQ* pin gives WAIT* */
#define	ZSWR1_REQ_REQ		0xc0	/* WAIT*-REQ* pin gives REQ* */
#define	ZSWR1_REQ_TX		0x00	/* WAIT*-REQ* pin follows xmit buf */
#define	ZSWR1_REQ_RX		0x20	/* WAIT*-REQ* pin follows recv buf */

#define	ZSWR1_RIE_NONE		0x00	/* disable rxint entirely */
#define	ZSWR1_RIE_FIRST		0x08	/* rxint on first char & on S.C. */
#define	ZSWR1_RIE		0x10	/* rxint per char & on S.C. */
#define	ZSWR1_RIE_SPECIAL_ONLY	0x18	/* rxint on S.C. only */

#define	ZSWR1_PE_SC		0x04	/* parity error is special condition */
#define	ZSWR1_TIE		0x02	/* transmit interrupt enable */
#define	ZSWR1_SIE		0x01	/* external/status interrupt enable */

#define	ZSWR1_IMASK 	0x1F	/* mask of all itr. enable bits. */

/* HSIS compat */
#define	ZSWR1_REQ_ENABLE	(ZSWR1_REQ_WAIT | ZSWR1_REQ_TX)

/*
 * Bits in Write Register 3 (`Receive Parameters and Control').
 * Bits 7 and 6 are taken as a unit.  Note that the receive bits
 * per character ordering is insane.
 *
 * Here `hardware flow control' means CTS enables the transmitter
 * and DCD enables the receiver.  The latter is neither interesting
 * nor useful, and gets in our way, making it almost unusable.
 */
#define	ZSWR3_RX_5		0x00	/* receive 5 bits per char */
#define	ZSWR3_RX_7		0x40	/* receive 7 bits per char */
#define	ZSWR3_RX_6		0x80	/* receive 6 bits per char */
#define	ZSWR3_RX_8		0xc0	/* receive 8 bits per char */
#define	ZSWR3_RXSIZE		0xc0	/* receive char size mask */

#define	ZSWR3_HFC		0x20	/* hardware flow control */
#define	ZSWR3_HUNT		0x10	/* enter hunt mode */
#define	ZSWR3_RXCRC_ENABLE	0x08	/* enable recv crc calculation */
#define	ZSWR3_ADDR_SEARCH_MODE	0x04	/* address search mode (SDLC only) */
#define	ZSWR3_SDLC_SHORT_ADDR	0x02	/* short address mode (SDLC only) */
#define	ZSWR3_SYNC_LOAD_INH	0x02	/* sync character load inhibit */
#define	ZSWR3_RX_ENABLE		0x01	/* receiver enable */

/*
 * Bits in Write Register 4 (`Transmit/Receive Miscellaneous Parameters
 * and Modes').  Bits 7&6, 5&4, and 3&2 are taken as units.
 */
#define	ZSWR4_CLK_X1		0x00	/* clock divisor = 1 */
#define	ZSWR4_CLK_X16		0x40	/* clock divisor = 16 */
#define	ZSWR4_CLK_X32		0x80	/* clock divisor = 32 */
#define	ZSWR4_CLK_X64		0xc0	/* clock divisor = 64 */
#define	ZSWR4_CLK_MASK		0xc0	/* clock divisor mask */

#define	ZSWR4_MONOSYNC		0x00	/* 8 bit sync char (sync only) */
#define	ZSWR4_BISYNC		0x10	/* 16 bit sync char (sync only) */
#define	ZSWR4_SDLC  		0x20	/* SDLC mode */
#define	ZSWR4_EXTSYNC		0x30	/* external sync mode */
#define	ZSWR4_SYNC_MASK		0x30	/* sync mode bit mask */

#define	ZSWR4_SYNCMODE		0x00	/* no stop bit (sync mode only) */
#define	ZSWR4_ONESB 		0x04	/* 1 stop bit */
#define	ZSWR4_1P5SB 		0x08	/* 1.5 stop bits (clk cannot be 1x) */
#define	ZSWR4_TWOSB 		0x0c	/* 2 stop bits */
#define	ZSWR4_SBMASK		0x0c	/* mask of all stop bits */

#define	ZSWR4_EVENP 		0x02	/* check for even parity */
#define	ZSWR4_PARENB		0x01	/* enable parity checking */
#define	ZSWR4_PARMASK		0x03	/* mask of all parity bits */

/*
 * Bits in Write Register 5 (`Transmit Parameter and Controls').
 * Bits 6 and 5 are taken as a unit; the ordering is, as with RX
 * bits per char, not sensible.
 */
#define	ZSWR5_DTR		0x80	/* assert (set to -12V) DTR */

#define	ZSWR5_TX_5		0x00	/* transmit 5 or fewer bits */
#define	ZSWR5_TX_7		0x20	/* transmit 7 bits */
#define	ZSWR5_TX_6		0x40	/* transmit 6 bits */
#define	ZSWR5_TX_8		0x60	/* transmit 8 bits */
#define	ZSWR5_TXSIZE		0x60	/* transmit char size mask */

#define	ZSWR5_BREAK		0x10	/* send break (continuous 0s) */
#define	ZSWR5_TX_ENABLE		0x08	/* enable transmitter */
#define	ZSWR5_CRC16		0x04	/* use CRC16 (off => use SDLC) */
#define	ZSWR5_RTS		0x02	/* assert RTS */
#define	ZSWR5_TXCRC_ENABLE	0x01	/* enable xmit crc calculation */

#ifdef not_done_here
/*
 * Bits in Write Register 7 when the chip is in SDLC mode.
 */
#define	ZSWR7_SDLCFLAG		0x7e	/* this value makes SDLC mode work */
#endif

/*
 * Bits in Write Register 7' (ZSWR_ENHANCED above). This register is
 * only available on the 85230. Despite the fact it contains flags
 * and not a single value, the register was named as it is read
 * via RR14. Weird.
 */
			/*	0x80	unused */
#define	ZSWR7P_EXTEND_READ	0x40	/* modify read map; make most regs readable */
#define	ZSWR7P_TX_FIFO		0x20	/* change level for Tx FIFO empty int */
#define	ZSWR7P_DTR_TIME		0x10	/* modifies deact. speed of /DTR//REQ */
#define	ZSWR7P_RX_FIFO		0x08	/* Rx FIFO int on 1/2 full? */
#define	ZSWR7P_RTS_DEACT	0x04	/* automatically deassert RTS */
#define	ZSWR7P_AUTO_EOM_RESET	0x02	/* automatically reset EMO/Tx Underrun */
#define	ZSWR7P_AUTO_TX_FLAG	0x01	/* Auto send SDLC flag at transmit start */

/*
 * Bits in Write Register 9 (`Master Interrupt Control').  Bits 7 & 6
 * are taken as a unit and indicate the type of reset; 00 means no reset
 * (and is not defined here).
 */
#define	ZSWR9_HARD_RESET	0xc0	/* force hardware reset */
#define	ZSWR9_A_RESET		0x80	/* reset channel A (0) */
#define	ZSWR9_B_RESET		0x40	/* reset channel B (1) */
#define	ZSWR9_SOFT_INTAC	0x20	/* Not in NMOS version */

#define	ZSWR9_STATUS_HIGH	0x10	/* status in high bits of intr vec */
#define	ZSWR9_MASTER_IE		0x08	/* master interrupt enable */
#define	ZSWR9_DLC		0x04	/* disable lower chain */
#define	ZSWR9_NO_VECTOR		0x02	/* no vector */
#define	ZSWR9_VECTOR_INCL_STAT	0x01	/* vector includes status */

/*
 * Bits in Write Register 10 (`Miscellaneous Transmitter/Receiver Control
 * Bits').  Bits 6 & 5 are taken as a unit, and some of the bits are
 * meaningful only in certain modes.  Bleah.
 */
#define	ZSWR10_PRESET_ONES	0x80	/* preset CRC to all 1 (else all 0) */

#define	ZSWR10_NRZ		0x00	/* NRZ encoding */
#define	ZSWR10_NRZI		0x20	/* NRZI encoding */
#define	ZSWR10_FM1		0x40	/* FM1 encoding */
#define	ZSWR10_FM0		0x60	/* FM0 encoding */

#define	ZSWR10_GA_ON_POLL	0x10	/* go active on poll (loop mode) */
#define	ZSWR10_MARK_IDLE	0x08	/* all 1s (vs flag) when idle (SDLC) */
#define	ZSWR10_ABORT_ON_UNDERRUN 0x4	/* abort on xmit underrun (SDLC) */
#define	ZSWR10_LOOP_MODE	0x02	/* loop mode (SDLC) */
#define	ZSWR10_6_BIT_SYNC	0x01	/* 6 bits per sync char (sync modes) */

/*
 * Bits in Write Register 11 (`Clock Mode Control').  Bits 6&5, 4&3, and
 * 1&0 are taken as units.  Various bits depend on other bits in complex
 * ways; see the Zilog manual.
 */
#define	ZSWR11_XTAL		0x80	/* have xtal between RTxC* and SYNC* */
					/* (else have TTL oscil. on RTxC*) */
#define	ZSWR11_RXCLK_RTXC	0x00	/* recv clock taken from RTxC* pin */
#define	ZSWR11_RXCLK_TRXC	0x20	/* recv clock taken from TRxC* pin */
#define	ZSWR11_RXCLK_BAUD	0x40	/* recv clock taken from BRG */
#define	ZSWR11_RXCLK_DPLL	0x60	/* recv clock taken from DPLL */

#define	ZSWR11_TXCLK_RTXC	0x00	/* xmit clock taken from RTxC* pin */
#define	ZSWR11_TXCLK_TRXC	0x08	/* xmit clock taken from TRxC* pin */
#define	ZSWR11_TXCLK_BAUD	0x10	/* xmit clock taken from BRG */
#define	ZSWR11_TXCLK_DPLL	0x18	/* xmit clock taken from DPLL */

#define	ZSWR11_TRXC_OUT_ENA	0x04	/* TRxC* pin will be an output */
					/* (unless it is being used above) */
#define	ZSWR11_TRXC_XTAL	0x00	/* TRxC output from xtal oscillator */
#define	ZSWR11_TRXC_XMIT	0x01	/* TRxC output from xmit clock */
#define	ZSWR11_TRXC_BAUD	0x02	/* TRxC output from BRG */
#define	ZSWR11_TRXC_DPLL	0x03	/* TRxC output from DPLL */

/*
 * Formula for Write Registers 12 and 13 (`Lower Byte of Baud Rate
 * Generator Time Constant' and `Upper Byte of ...').  Inputs:
 *
 *	f	BRG input clock frequency (in Hz) AFTER division
 *		by 1, 16, 32, or 64 (per clock divisor in WR4)
 *	bps	desired rate in bits per second (9600, etc)
 *
 * We want
 *
 *	  f
 *	----- + 0.5 - 2
 *	2 bps
 *
 * rounded down to an integer.  This can be computed entirely
 * in integer arithmetic as:
 *
 *	f + bps
 *	------- - 2
 *	 2 bps
 */
#define	BPS_TO_TCONST(f, bps)	((((f) + (bps)) / (2 * (bps))) - 2)

/* inverse of above: given a BRG Time Constant, return Bits Per Second */
#define	TCONST_TO_BPS(f, tc)	((f) / 2 / ((tc) + 2))

/*
 * Bits in Write Register 14 (`Miscellaneous Control Bits').
 * Bits 7 through 5 are taken as a unit and make up a `DPLL command'.
 */
#define	ZSWR14_DPLL_NOOP	0x00	/* leave DPLL alone */
#define	ZSWR14_DPLL_SEARCH	0x20	/* enter search mode */
#define	ZSWR14_DPLL_RESET_CM	0x40	/* reset `clock missing' in RR10 */
#define	ZSWR14_DPLL_DISABLE	0x60	/* disable DPLL (continuous search) */
#define	ZSWR14_DPLL_SRC_BAUD	0x80	/* set DPLL src = BRG */
#define	ZSWR14_DPLL_SRC_RTXC	0xa0	/* set DPLL src = RTxC* or xtal osc */
#define	ZSWR14_DPLL_FM		0xc0	/* operate in FM mode */
#define	ZSWR14_DPLL_NRZI	0xe0	/* operate in NRZI mode */

#define	ZSWR14_LOCAL_LOOPBACK	0x10	/* set local loopback mode */
#define	ZSWR14_AUTO_ECHO	0x08	/* set auto echo mode */
#define	ZSWR14_DTR_REQ		0x04	/* DTR* / REQ* pin gives REQ* */
#define	ZSWR14_BAUD_FROM_PCLK	0x02	/* BRG clock taken from PCLK */
					/* (else from RTxC* pin or xtal osc) */
#define	ZSWR14_BAUD_ENA		0x01	/* enable BRG countdown */

/*
 * Bits in Write Register 15 (`External/Status Interrupt Control').
 * Most of these cause status interrupts whenever the corresponding
 * bit or pin changes state (i.e., any rising or falling edge).
 *
 * NOTE: ZSWR15_SDLC_FIFO & ZSWR15_ENABLE_ENHANCED should not be
 * set on an NMOS 8530. Also, ZSWR15_ENABLE_ENHANCED is only
 * available on the 85230.
 */
#define	ZSWR15_BREAK_IE		0x80	/* enable break/abort status int */
#define	ZSWR15_TXUEOM_IE	0x40	/* enable TX underrun/EOM status int */
#define	ZSWR15_CTS_IE		0x20	/* enable CTS* pin status int */
#define	ZSWR15_SYNCHUNT_IE	0x10	/* enable SYNC* pin/hunt status int */
#define	ZSWR15_DCD_IE		0x08	/* enable DCD* pin status int */
#define	ZSWR15_SDLC_FIFO	0x04	/* enable SDLC FIFO enhancements */
#define	ZSWR15_ZERO_COUNT_IE	0x02	/* enable BRG-counter = 0 status int */
#define	ZSWR15_ENABLE_ENHANCED	0x01	/* enable writing WR7' at reg 7 */

/*
 * Bits in Read Register 0 (`Transmit/Receive Buffer Status and External
 * Status').
 */
#define	ZSRR0_BREAK		0x80	/* break/abort detected */
#define	ZSRR0_TXUNDER		0x40	/* transmit underrun/EOM (sync) */
#define	ZSRR0_CTS		0x20	/* clear to send */
#define	ZSRR0_SYNC_HUNT		0x10	/* sync/hunt (sync mode) */
#define	ZSRR0_DCD		0x08	/* data carrier detect */
#define	ZSRR0_TX_READY		0x04	/* transmit buffer empty */
#define	ZSRR0_ZERO_COUNT	0x02	/* zero count in baud clock */
#define	ZSRR0_RX_READY		0x01	/* received character ready */

/*
 * Bits in Read Register 1 (the Zilog book does not name this one).
 */
#define	ZSRR1_EOF		0x80	/* end of frame (SDLC mode) */
#define	ZSRR1_FE		0x40	/* CRC/framing error */
#define	ZSRR1_DO		0x20	/* data (receiver) overrun */
#define	ZSRR1_PE		0x10	/* parity error */
#define	ZSRR1_RC0		0x08	/* residue code 0 (SDLC mode) */
#define	ZSRR1_RC1		0x04	/* residue code 1 (SDLC mode) */
#define	ZSRR1_RC2		0x02	/* residue code 2 (SDLC mode) */
#define	ZSRR1_ALL_SENT		0x01	/* all chars out of xmitter (async) */

/*
 * Read Register 2 in B channel contains status bits if VECTOR_INCL_STAT
 * is set.
 */

/*
 * Bits in Read Register 3 (`Interrupt Pending').  Only channel A
 * has an RR3.
 */
			/*	0x80	   unused, returned as 0 */
			/*	0x40	   unused, returned as 0 */
#define	ZSRR3_IP_A_RX		0x20	/* channel A recv int pending */
#define	ZSRR3_IP_A_TX		0x10	/* channel A xmit int pending */
#define	ZSRR3_IP_A_STAT		0x08	/* channel A status int pending */
#define	ZSRR3_IP_B_RX		0x04	/* channel B recv int pending */
#define	ZSRR3_IP_B_TX		0x02	/* channel B xmit int pending */
#define	ZSRR3_IP_B_STAT		0x01	/* channel B status int pending */

/*
 * Bits in Read Register 10 (`contains some miscellaneous status bits').
 */
#define	ZSRR10_1_CLOCK_MISSING	0x80	/* 1 clock edge missing (FM mode) */
#define	ZSRR10_2_CLOCKS_MISSING	0x40	/* 2 clock edges missing (FM mode) */
			/*	0x20	   unused */
#define	ZSRR10_LOOP_SENDING	0x10	/* xmitter controls loop (SDLC loop) */
			/*	0x08	   unused */
			/*	0x04	   unused */
#define	ZSRR10_ON_LOOP		0x02	/* SCC is on loop (SDLC/X.21 modes) */

/*
 * Bits in Read Register 15.  This register is one of the few that
 * simply reads back the corresponding Write Register.
 */
#define	ZSRR15_BREAK_IE		0x80	/* break/abort status int enable */
#define	ZSRR15_TXUEOM_IE	0x40	/* TX underrun/EOM status int enable */
#define	ZSRR15_CTS_IE		0x20	/* CTS* pin status int enable */
#define	ZSRR15_SYNCHUNT_IE	0x10	/* SYNC* pin/hunt status int enable */
#define	ZSRR15_DCD_IE		0x08	/* DCD* pin status int enable */
			/*	0x04	   unused, returned as zero */
#define	ZSRR15_ZERO_COUNT_IE	0x02	/* BRG-counter = 0 status int enable */
			/*	0x01	   unused, returned as zero */
