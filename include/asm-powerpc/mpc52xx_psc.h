/*
 * include/asm-ppc/mpc52xx_psc.h
 *
 * Definitions of consts/structs to drive the Freescale MPC52xx OnChip
 * PSCs. Theses are shared between multiple drivers since a PSC can be
 * UART, AC97, IR, I2S, ... So this header is in asm-ppc.
 *
 *
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Based/Extracted from some header of the 2.4 originally written by
 * Dale Farnsworth <dfarnsworth@mvista.com>
 *
 * Copyright (C) 2004 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 MontaVista, Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_MPC52xx_PSC_H__
#define __ASM_MPC52xx_PSC_H__

#include <asm/types.h>

/* Max number of PSCs */
#define MPC52xx_PSC_MAXNUM	6

/* Programmable Serial Controller (PSC) status register bits */
#define MPC52xx_PSC_SR_CDE	0x0080
#define MPC52xx_PSC_SR_RXRDY	0x0100
#define MPC52xx_PSC_SR_RXFULL	0x0200
#define MPC52xx_PSC_SR_TXRDY	0x0400
#define MPC52xx_PSC_SR_TXEMP	0x0800
#define MPC52xx_PSC_SR_OE	0x1000
#define MPC52xx_PSC_SR_PE	0x2000
#define MPC52xx_PSC_SR_FE	0x4000
#define MPC52xx_PSC_SR_RB	0x8000

/* PSC Command values */
#define MPC52xx_PSC_RX_ENABLE		0x0001
#define MPC52xx_PSC_RX_DISABLE		0x0002
#define MPC52xx_PSC_TX_ENABLE		0x0004
#define MPC52xx_PSC_TX_DISABLE		0x0008
#define MPC52xx_PSC_SEL_MODE_REG_1	0x0010
#define MPC52xx_PSC_RST_RX		0x0020
#define MPC52xx_PSC_RST_TX		0x0030
#define MPC52xx_PSC_RST_ERR_STAT	0x0040
#define MPC52xx_PSC_RST_BRK_CHG_INT	0x0050
#define MPC52xx_PSC_START_BRK		0x0060
#define MPC52xx_PSC_STOP_BRK		0x0070

/* PSC TxRx FIFO status bits */
#define MPC52xx_PSC_RXTX_FIFO_ERR	0x0040
#define MPC52xx_PSC_RXTX_FIFO_UF	0x0020
#define MPC52xx_PSC_RXTX_FIFO_OF	0x0010
#define MPC52xx_PSC_RXTX_FIFO_FR	0x0008
#define MPC52xx_PSC_RXTX_FIFO_FULL	0x0004
#define MPC52xx_PSC_RXTX_FIFO_ALARM	0x0002
#define MPC52xx_PSC_RXTX_FIFO_EMPTY	0x0001

/* PSC interrupt mask bits */
#define MPC52xx_PSC_IMR_TXRDY		0x0100
#define MPC52xx_PSC_IMR_RXRDY		0x0200
#define MPC52xx_PSC_IMR_DB		0x0400
#define MPC52xx_PSC_IMR_IPC		0x8000

/* PSC input port change bit */
#define MPC52xx_PSC_CTS			0x01
#define MPC52xx_PSC_DCD			0x02
#define MPC52xx_PSC_D_CTS		0x10
#define MPC52xx_PSC_D_DCD		0x20

/* PSC mode fields */
#define MPC52xx_PSC_MODE_5_BITS			0x00
#define MPC52xx_PSC_MODE_6_BITS			0x01
#define MPC52xx_PSC_MODE_7_BITS			0x02
#define MPC52xx_PSC_MODE_8_BITS			0x03
#define MPC52xx_PSC_MODE_BITS_MASK		0x03
#define MPC52xx_PSC_MODE_PAREVEN		0x00
#define MPC52xx_PSC_MODE_PARODD			0x04
#define MPC52xx_PSC_MODE_PARFORCE		0x08
#define MPC52xx_PSC_MODE_PARNONE		0x10
#define MPC52xx_PSC_MODE_ERR			0x20
#define MPC52xx_PSC_MODE_FFULL			0x40
#define MPC52xx_PSC_MODE_RXRTS			0x80

#define MPC52xx_PSC_MODE_ONE_STOP_5_BITS	0x00
#define MPC52xx_PSC_MODE_ONE_STOP		0x07
#define MPC52xx_PSC_MODE_TWO_STOP		0x0f

#define MPC52xx_PSC_RFNUM_MASK	0x01ff


/* Structure of the hardware registers */
struct mpc52xx_psc {
	u8		mode;		/* PSC + 0x00 */
	u8		reserved0[3];
	union {				/* PSC + 0x04 */
		u16	status;
		u16	clock_select;
	} sr_csr;
#define mpc52xx_psc_status	sr_csr.status
#define mpc52xx_psc_clock_select sr_csr.clock_select
	u16		reserved1;
	u8		command;	/* PSC + 0x08 */
	u8		reserved2[3];
	union {				/* PSC + 0x0c */
		u8	buffer_8;
		u16	buffer_16;
		u32	buffer_32;
	} buffer;
#define mpc52xx_psc_buffer_8	buffer.buffer_8
#define mpc52xx_psc_buffer_16	buffer.buffer_16
#define mpc52xx_psc_buffer_32	buffer.buffer_32
	union {				/* PSC + 0x10 */
		u8	ipcr;
		u8	acr;
	} ipcr_acr;
#define mpc52xx_psc_ipcr	ipcr_acr.ipcr
#define mpc52xx_psc_acr		ipcr_acr.acr
	u8		reserved3[3];
	union {				/* PSC + 0x14 */
		u16	isr;
		u16	imr;
	} isr_imr;
#define mpc52xx_psc_isr		isr_imr.isr
#define mpc52xx_psc_imr		isr_imr.imr
	u16		reserved4;
	u8		ctur;		/* PSC + 0x18 */
	u8		reserved5[3];
	u8		ctlr;		/* PSC + 0x1c */
	u8		reserved6[3];
	u16		ccr;		/* PSC + 0x20 */
	u8		reserved7[14];
	u8		ivr;		/* PSC + 0x30 */
	u8		reserved8[3];
	u8		ip;		/* PSC + 0x34 */
	u8		reserved9[3];
	u8		op1;		/* PSC + 0x38 */
	u8		reserved10[3];
	u8		op0;		/* PSC + 0x3c */
	u8		reserved11[3];
	u32		sicr;		/* PSC + 0x40 */
	u8		ircr1;		/* PSC + 0x44 */
	u8		reserved13[3];
	u8		ircr2;		/* PSC + 0x44 */
	u8		reserved14[3];
	u8		irsdr;		/* PSC + 0x4c */
	u8		reserved15[3];
	u8		irmdr;		/* PSC + 0x50 */
	u8		reserved16[3];
	u8		irfdr;		/* PSC + 0x54 */
	u8		reserved17[3];
};

struct mpc52xx_psc_fifo {
	u16		rfnum;		/* PSC + 0x58 */
	u16		reserved18;
	u16		tfnum;		/* PSC + 0x5c */
	u16		reserved19;
	u32		rfdata;		/* PSC + 0x60 */
	u16		rfstat;		/* PSC + 0x64 */
	u16		reserved20;
	u8		rfcntl;		/* PSC + 0x68 */
	u8		reserved21[5];
	u16		rfalarm;	/* PSC + 0x6e */
	u16		reserved22;
	u16		rfrptr;		/* PSC + 0x72 */
	u16		reserved23;
	u16		rfwptr;		/* PSC + 0x76 */
	u16		reserved24;
	u16		rflrfptr;	/* PSC + 0x7a */
	u16		reserved25;
	u16		rflwfptr;	/* PSC + 0x7e */
	u32		tfdata;		/* PSC + 0x80 */
	u16		tfstat;		/* PSC + 0x84 */
	u16		reserved26;
	u8		tfcntl;		/* PSC + 0x88 */
	u8		reserved27[5];
	u16		tfalarm;	/* PSC + 0x8e */
	u16		reserved28;
	u16		tfrptr;		/* PSC + 0x92 */
	u16		reserved29;
	u16		tfwptr;		/* PSC + 0x96 */
	u16		reserved30;
	u16		tflrfptr;	/* PSC + 0x9a */
	u16		reserved31;
	u16		tflwfptr;	/* PSC + 0x9e */
};

#define MPC512x_PSC_FIFO_RESET_SLICE	0x80
#define MPC512x_PSC_FIFO_ENABLE_SLICE	0x01
#define MPC512x_PSC_FIFO_ENABLE_DMA	0x04

#define MPC512x_PSC_FIFO_EMPTY		0x1
#define MPC512x_PSC_FIFO_FULL		0x2
#define MPC512x_PSC_FIFO_ALARM		0x4
#define MPC512x_PSC_FIFO_URERR		0x8
#define MPC512x_PSC_FIFO_ORERR		0x01
#define MPC512x_PSC_FIFO_MEMERROR	0x02

struct mpc512x_psc_fifo {
	u32		reserved1[10];
	u32		txcmd;		/* PSC + 0x80 */
	u32		txalarm;	/* PSC + 0x84 */
	u32		txsr;		/* PSC + 0x88 */
	u32		txisr;		/* PSC + 0x8c */
	u32		tximr;		/* PSC + 0x90 */
	u32		txcnt;		/* PSC + 0x94 */
	u32		txptr;		/* PSC + 0x98 */
	u32		txsz;		/* PSC + 0x9c */
	u32		reserved2[7];
	union {
		u8	txdata_8;
		u16	txdata_16;
		u32	txdata_32;
	} txdata; 			/* PSC + 0xbc */
#define txdata_8 txdata.txdata_8
#define txdata_16 txdata.txdata_16
#define txdata_32 txdata.txdata_32
	u32		rxcmd;		/* PSC + 0xc0 */
	u32		rxalarm;	/* PSC + 0xc4 */
	u32		rxsr;		/* PSC + 0xc8 */
	u32		rxisr;		/* PSC + 0xcc */
	u32		rximr;		/* PSC + 0xd0 */
	u32		rxcnt;		/* PSC + 0xd4 */
	u32		rxptr;		/* PSC + 0xd8 */
	u32		rxsz;		/* PSC + 0xdc */
	u32		reserved3[7];
	union {
		u8	rxdata_8;
		u16	rxdata_16;
		u32	rxdata_32;
	} rxdata; 			/* PSC + 0xfc */
#define rxdata_8 rxdata.rxdata_8
#define rxdata_16 rxdata.rxdata_16
#define rxdata_32 rxdata.rxdata_32
};

#endif  /* __ASM_MPC52xx_PSC_H__ */
