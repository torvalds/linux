/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 *
 * $FreeBSD$
 */

#ifndef _DEV_IC_Z8530_H_
#define	_DEV_IC_Z8530_H_

/*
 * Channel B control:	0
 * Channel B data:	1
 * Channel A control:	2
 * Channel A data:	3
 */

/* The following apply when using a device-scoped bus handle */
#define	CHAN_A		2
#define	CHAN_B		0

#define	REG_CTRL	0
#define	REG_DATA	1

/* Write registers. */
#define	WR_CR		0	/* Command Register. */
#define	WR_IDT		1	/* Interrupt and Data Transfer Mode. */
#define	WR_IV		2	/* Interrupt Vector (shared). */
#define	WR_RPC		3	/* Receive Parameters and Control. */
#define	WR_MPM		4	/* Miscellaneous Parameters and Modes. */
#define	WR_TPC		5	/* Transmit Parameters and Control. */
#define	WR_SCAF		6	/* Sync Character or (SDLC) Address Field. */
#define	WR_SCF		7	/* Sync Character or (SDCL) Flag. */
#define	WR_EFC		7	/* Extended Feature and FIFO Control. */
#define	WR_TB		8	/* Transmit Buffer. */
#define	WR_MIC		9	/* Master Interrupt Control (shared). */
#define	WR_MCB1		10	/* Miscellaneous Control Bits (part 1 :-). */
#define	WR_CMC		11	/* Clock Mode Control. */
#define	WR_TCL		12	/* BRG Time Constant Low. */
#define	WR_TCH		13	/* BRG Time Constant High. */
#define	WR_MCB2		14	/* Miscellaneous Control Bits (part 2 :-). */
#define	WR_IC		15	/* Interrupt Control. */

/* Read registers. */
#define	RR_BES		0	/* Buffer and External Status. */
#define	RR_SRC		1	/* Special Receive Condition. */
#define	RR_IV		2	/* Interrupt Vector. */
#define	RR_IP		3	/* Interrupt Pending (ch A only). */
#define	RR_MPM		4	/* Miscellaneous Parameters and Modes. */
#define	RR_TPC		5	/* Transmit Parameters and Control. */
#define	RR_BCL		6	/* Byte Count Low. */
#define	RR_BCH		7	/* Byte Count High. */
#define	RR_RB		8	/* Receive Buffer. */
#define	RR_RPC		9	/* Receive Parameters and Control. */
#define	RR_MSB		10	/* Miscellaneous Status Bits. */
#define	RR_MCB1		11	/* Miscellaneous Control Bits (part 1). */
#define	RR_TCL		12	/* BRG Time Constant Low. */
#define	RR_TCH		13	/* BRG Time Constant High. */
#define	RR_EFC		14	/* Extended Feature and FIFO Control. */
#define	RR_IC		15	/* Interrupt Control. */

/* Buffer and External Status (RR0). */
#define	BES_BRK		0x80	/* Break (Abort). */
#define	BES_TXU		0x40	/* Tx Underrun (EOM). */
#define	BES_CTS		0x20	/* CTS. */
#define	BES_SYNC	0x10	/* Sync. */
#define	BES_DCD		0x08	/* DCD. */
#define	BES_TXE		0x04	/* Tx Empty. */
#define	BES_ZC		0x02	/* Zero Count. */
#define	BES_RXA		0x01	/* Rx Available. */

/* Clock Mode Control (WR11). */
#define	CMC_XTAL	0x80	/* -RTxC connects to quartz crystal. */
#define	CMC_RC_DPLL	0x60	/* Rx Clock from DPLL. */
#define	CMC_RC_BRG	0x40	/* Rx Clock from BRG. */
#define	CMC_RC_TRXC	0x20	/* Rx Clock from -TRxC. */
#define	CMC_RC_RTXC	0x00	/* Rx Clock from -RTxC. */
#define	CMC_TC_DPLL	0x18	/* Tx Clock from DPLL */
#define	CMC_TC_BRG	0x10	/* Tx Clock from BRG */
#define	CMC_TC_TRXC	0x08	/* Tx Clock from -TRxC. */
#define	CMC_TC_RTXC	0x00	/* Tx Clock from -RTxC. */
#define	CMC_TRXC_OUT	0x04	/* -TRxC is output. */
#define	CMC_TRXC_DPLL	0x03	/* -TRxC from DPLL */
#define	CMC_TRXC_BRG	0x02	/* -TRxC from BRG */
#define	CMC_TRXC_XMIT	0x01	/* -TRxC from Tx clock. */
#define	CMC_TRXC_XTAL	0x00	/* -TRxC from XTAL. */

/* Command Register (WR0). */
#define	CR_RSTTXU	0xc0	/* Reset Tx. Underrun/EOM. */
#define	CR_RSTTXCRC	0x80	/* Reset Tx. CRC. */
#define	CR_RSTRXCRC	0x40	/* Reset Rx. CRC. */
#define	CR_RSTIUS	0x38	/* Reset Int. Under Service. */
#define	CR_RSTERR	0x30	/* Error Reset. */
#define	CR_RSTTXI	0x28	/* Reset Tx. Int. */
#define	CR_ENARXI	0x20	/* Enable Rx. Int. */
#define	CR_ABORT	0x18	/* Send Abort. */
#define	CR_RSTXSI	0x10	/* Reset Ext/Status Int. */

/* Extended Feature and FIFO Control (WR7 prime). */
#define	EFC_ERE		0x40	/* Extended Read Enable. */
#define	EFC_FE		0x20	/* Transmit FIFO Empty. */
#define	EFC_RQT		0x10	/* Request Timing. */
#define	EFC_FHF		0x08	/* Receive FIFO Half Full. */
#define	EFC_RTS		0x04	/* Auto RTS Deactivation. */
#define	EFC_EOM		0x02	/* Auto EOM Reset. */
#define	EFC_FLAG	0x01	/* Auto SDLC Flag on Tx. */

/* Interrupt Control (WR15). */
#define	IC_BRK		0x80	/* Break (Abort) IE. */
#define	IC_TXU		0x40	/* Tx Underrun IE. */
#define	IC_CTS		0x20	/* CTS IE. */
#define	IC_SYNC		0x10	/* Sync IE. */
#define	IC_DCD		0x08	/* DCD IE. */
#define	IC_FIFO		0x04	/* SDLC FIFO Enable. */
#define	IC_ZC		0x02	/* Zero Count IE. */
#define	IC_EF		0x01	/* Extended Feature Enable. */

/* Interrupt and Data Transfer Mode (WR1). */
#define	IDT_WRE		0x80	/* Wait/DMA Request Enable. */
#define	IDT_REQ		0x40	/* DMA Request. */
#define	IDT_WRR		0x20	/* Wait/DMA Reuest on Receive. */
#define	IDT_RISC	0x18	/* Rx Int. on Special Condition Only. */
#define	IDT_RIA		0x10	/* Rx Int. on All Characters. */
#define	IDT_RIF		0x08	/* Rx Int. on First Character. */
#define	IDT_PSC		0x04	/* Parity is Special Condition. */
#define	IDT_TIE		0x02	/* Tx Int. Enable. */
#define	IDT_XIE		0x01	/* Ext. Int. Enable. */

/* Interrupt Pending (RR3). */
#define	IP_RIA		0x20	/* Rx. Int. ch. A. */
#define	IP_TIA		0x10	/* Tx. Int. ch. A. */ 
#define	IP_SIA		0x08	/* Ext/Status Int. ch. A. */
#define	IP_RIB		0x04	/* Rx. Int. ch. B. */
#define	IP_TIB		0x02	/* Tx. Int. ch. B. */
#define	IP_SIB		0x01	/* Ext/Status Int. ch. B. */

/* Interrupt Vector Status Low (RR2). */
#define	IV_SCA		0x0e	/* Special Condition ch. A. */
#define	IV_RAA		0x0c	/* Receive Available ch. A. */
#define	IV_XSA		0x0a	/* External/Status Change ch. A. */
#define	IV_TEA		0x08	/* Transmitter Empty ch. A. */
#define	IV_SCB		0x06	/* Special Condition ch. B. */
#define	IV_RAB		0x04	/* Receive Available ch. B. */
#define	IV_XSB		0x02	/* External/Status Change ch. B. */
#define	IV_TEB		0x00	/* Transmitter Empty ch. B. */

/* Miscellaneous Control Bits part 1 (WR10). */
#define	MCB1_CRC1	0x80	/* CRC presets to 1. */
#define	MCB1_FM0	0x60	/* FM0 Encoding. */
#define	MCB1_FM1	0x40	/* FM1 Encoding. */
#define	MCB1_NRZI	0x20	/* NRZI Encoding. */
#define	MCB1_NRZ	0x00	/* NRZ Encoding. */
#define	MCB1_AOP	0x10	/* Active On Poll. */
#define	MCB1_MI		0x08	/* Mark Idle. */
#define	MCB1_AOU	0x04	/* Abort On Underrun. */
#define	MCB1_LM		0x02	/* Loop Mode. */
#define	MCB1_SIX	0x01	/* 6 or 12 bit SYNC. */

/* Miscellaneous Control Bits part 2 (WR14). */
#define	MCB2_NRZI	0xe0	/* DPLL - NRZI mode. */
#define	MCB2_FM		0xc0	/* DPLL - FM mode. */
#define	MCB2_RTXC	0xa0	/* DPLL - Clock from -RTxC. */
#define	MCB2_BRG	0x80	/* DPLL - Clock from BRG. */
#define	MCB2_OFF	0x60	/* DPLL - Disable. */
#define	MCB2_RMC	0x40	/* DPLL - Reset Missing Clock. */
#define	MCB2_ESM	0x20	/* DPLL - Enter Search Mode. */
#define	MCB2_LL		0x10	/* Local Loopback. */
#define	MCB2_AE		0x08	/* Auto Echo. */
#define	MCB2_REQ	0x04	/* Request Function. */
#define	MCB2_PCLK	0x02	/* BRG source is PCLK. */
#define	MCB2_BRGE	0x01	/* BRG enable. */

/* Master Interrupt Control (WR9). */
#define	MIC_FHR		0xc0	/* Force Hardware Reset. */
#define	MIC_CRA		0x80	/* Channel Reset A. */
#define	MIC_CRB		0x40	/* Channel Reset B. */
#define	MIC_SIE		0x20	/* Software INTACK Enable. */
#define	MIC_SH		0x10	/* Status High. */
#define	MIC_MIE		0x08	/* Master Interrupt Enable. */
#define	MIC_DLC		0x04	/* Disable Lower Chain. */
#define	MIC_NV		0x02	/* No Vector. */
#define	MIC_VIS		0x01	/* Vector Includes Status. */

/* Transmit/Receive Miscellaneous Parameters and Modes (WR4). */
#define	MPM_CM64	0xc0	/* X64 Clock Mode. */
#define	MPM_CM32	0x80	/* X32 Clock Mode. */
#define	MPM_CM16	0x40	/* X16 Clock Mode. */
#define	MPM_CM1		0x00	/* X1 Clock Mode. */
#define	MPM_EXT		0x30	/* External Sync Mode. */
#define	MPM_SDLC 	0x20	/* SDLC mode. */
#define	MPM_BI		0x10	/* 16-bit Sync (bi-sync). */
#define	MPM_MONO	0x00	/* 8-bit Sync (mono-sync). */
#define	MPM_SB2 	0x0c	/* Async mode: 2 stopbits. */
#define	MPM_SB15 	0x08	/* Async mode: 1.5 stopbits. */
#define	MPM_SB1 	0x04	/* Async mode: 1 stopbit. */
#define	MPM_SYNC	0x00	/* Sync Mode Enable. */
#define	MPM_EVEN 	0x02	/* Async mode: even parity. */
#define	MPM_PE		0x01	/* Async mode: parity enable. */

/* Receive Parameters and Control (WR3). */
#define	RPC_RB8		0xc0	/* 8 databits. */
#define	RPC_RB6		0x80	/* 6 databits. */
#define	RPC_RB7		0x40	/* 7 databits. */
#define	RPC_RB5		0x00	/* 5 databits. */
#define	RPC_AE		0x20	/* Auto Enable. */
#define	RPC_EHM		0x10	/* Enter Hunt Mode. */
#define	RPC_CRC		0x08	/* CRC Enable. */
#define	RPC_ASM		0x04	/* Address Search Mode. */
#define	RPC_LI		0x02	/* SYNC Character Load Inhibit */
#define	RPC_RXE		0x01	/* Receiver Enable */

/* Special Receive Condition (RR1). */
#define	SRC_EOF		0x80	/* End Of Frame. */
#define	SRC_FE		0x40	/* Framing Error. */
#define	SRC_OVR		0x20	/* Rx. Overrun. */
#define	SRC_PE		0x10	/* Parity Error. */
#define	SRC_RC0		0x08	/* Residue Code 0. */
#define	SRC_RC1		0x04	/* Residue Code 1. */
#define	SRC_RC2		0x02	/* Residue Code 2. */
#define	SRC_AS		0x01	/* All Sent. */

/* Transmit Parameter and Control (WR5). */
#define	TPC_DTR		0x80	/* DTR. */
#define	TPC_TB8		0x60	/* 8 databits. */
#define	TPC_TB6		0x40	/* 6 databits. */
#define	TPC_TB7		0x20	/* 7 databits. */
#define	TPC_TB5		0x00	/* 5 or fewer databits. */
#define	TPC_BRK		0x10	/* Send break. */
#define	TPC_TXE		0x08	/* Transmitter Enable. */
#define	TPC_CRC16	0x04	/* CRC16. */
#define	TPC_RTS		0x02	/* RTS. */
#define	TPC_CRC		0x01	/* CRC Enable. */

#endif /* _DEV_IC_Z8530_H_ */
