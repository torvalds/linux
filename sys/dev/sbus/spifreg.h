/*	$OpenBSD: spifreg.h,v 1.5 2003/06/02 18:32:41 jason Exp $	*/

/*
 * Copyright (c) 1999-2002 Jason L. Wright (jason@thought.net)
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#define	PPC_IN_PDATA		0x000		/* input data */
#define	PPC_IN_PSTAT		0x001		/* input status */
#define	PPC_IN_CTRL		0x002		/* input control */
#define	PPC_IN_PWEIRD		0x003		/* input weird */
#define	PPC_OUT_PDATA		0x000		/* output data */
#define	PPC_OUT_PSTAT		0x001		/* output status */
#define	PPC_OUT_PCTRL		0x002		/* output control */
#define	PPC_OUT_PWEIRD		0x003		/* output weird */
#define	PPC_IACK_PDATA		0x1fc		/* iack data */
#define	PPC_IACK_PSTAT		0x1fd		/* iack status */
#define	PPC_IACK_PCTRL		0x1fe		/* iack control */
#define	PPC_IACK_PWEIRD		0x1ff		/* iack weird */

/* Parallel Status: read only */
#define	PPC_PSTAT_ERROR		0x08		/* error */
#define	PPC_PSTAT_SELECT	0x10		/* select */
#define	PPC_PSTAT_PAPER		0x20		/* paper out */
#define	PPC_PSTAT_ACK		0x40		/* ack */
#define	PPC_PSTAT_BUSY		0x80		/* busy */

/* Parallel Control: read/write */
#define	PPC_CTRL_STROBE		0x01		/* strobe, 1=drop strobe */
#define	PPC_CTRL_AFX		0x02		/* auto form-feed */
#define	PPC_CTRL_INIT		0x04		/* init, 1=enable printer */
#define	PPC_CTRL_SLCT		0x08		/* SLC, 1=select printer */
#define	PPC_CTRL_IRQE		0x10		/* IRQ, 1=enable intrs */
#define	PPC_CTRL_OUTPUT		0x20		/* direction: 1=ppc out */

/*
 * The 'stc' is a Cirrus Logic CL-CD180 (either revision B or revision C)
 */
#define	STC_CCR			0x01		/* channel command */		
#define	STC_SRER		0x02		/* service request enable */
#define	STC_COR1		0x03		/* channel option 1 */
#define	STC_COR2		0x04		/* channel option 2 */
#define	STC_COR3		0x05		/* channel option 3 */
#define	STC_CCSR		0x06		/* channel control status */
#define	STC_RDCR		0x07		/* rx data count */
#define	STC_SCHR1		0x09		/* special character 1 */
#define	STC_SCHR2		0x0a		/* special character 2 */
#define	STC_SCHR3		0x0b		/* special character 3 */
#define	STC_SCHR4		0x0c		/* special character 4 */
#define	STC_MCOR1		0x10		/* modem change option 1 */
#define	STC_MCOR2		0x11		/* modem change option 2 */
#define	STC_MCR			0x12		/* modem change */
#define	STC_RTPR		0x18		/* rx timeout period */
#define	STC_MSVR		0x28		/* modem signal value */
#define	STC_MSVRTS		0x29		/* modem signal value rts */
#define	STC_MSVDTR		0x2a		/* modem signal value dtr */
#define	STC_RBPRH		0x31		/* rx bit rate period high */
#define	STC_RBPRL		0x32		/* rx bit rate period low */
#define	STC_RBR			0x33		/* rx bit */
#define	STC_TBPRH		0x39		/* tx bit rate period high */
#define	STC_TBPRL		0x3a		/* tx bit rate period low */
#define	STC_GSVR		0x40		/* global service vector */
#define	STC_GSCR1		0x41		/* global service channel 1 */
#define	STC_GSCR2		0x42		/* global service channel 2 */
#define	STC_GSCR3		0x43		/* global service channel 3 */
#define	STC_MSMR		0x61		/* modem service match */
#define	STC_TSMR		0x62		/* tx service match */
#define	STC_RSMR		0x63		/* rx service match */
#define	STC_CAR			0x64		/* channel access */
#define	STC_SRSR		0x65		/* service request status */
#define	STC_SRCR		0x66		/* service request config */
#define	STC_GFRCR		0x6b		/* global firmware rev code */
#define	STC_PPRH		0x70		/* prescalar period high */
#define	STC_PPRL		0x71		/* prescalar period low */
#define	STC_MRAR		0x75		/* modem request ack */
#define	STC_TRAR		0x76		/* tx request ack */
#define	STC_RRAR		0x77		/* rx request ack */
#define	STC_RDR			0x78		/* rx data */
#define	STC_RCSR		0x7a		/* rx character status */
#define	STC_TDR			0x7b		/* tx data */
#define	STC_EOSRR		0x7f		/* end of service */

#define	STC_REGMAPSIZE		0x80

/* Global Firmware Revision Code Register (rw) */
#define	CD180_GFRCR_REV_B	0x81		/* CL-CD180B */
#define	CD180_GFRCR_REV_C	0x82		/* CL-CD180C */

/* Service Request Configuration Register (rw) (CD180C or higher) */
#define	CD180_SRCR_PKGTYP		0x80	/* pkg type,0=PLCC,1=PQFP */
#define	CD180_SRCR_REGACKEN		0x40	/* register ack enable */
#define	CD180_SRCR_DAISYEN		0x20	/* daisy chain enable */
#define	CD180_SRCR_GLOBPRI		0x10	/* global priority */
#define	CD180_SRCR_UNFAIR		0x08	/* use unfair interrupts */
#define	CD180_SRCR_AUTOPRI		0x02	/* automatic priority */
#define	CD180_SRCR_PRISEL		0x01	/* select rx/tx as high pri */

/* Prescalar Period Register High (rw) */
#define	CD180_PPRH	0xf0		/* high byte */
#define	CD180_PPRL	0x00		/* low byte */

/* Global Service Vector Register (rw) */
/* Modem Request Acknowledgement Register (ro) (and IACK equivalent) */
/* Receive Request Acknowledgement Register (ro) (and IACK equivalent) */
/* Transmit Request Acknowledgement Register (ro) (and IACK equivalent) */
#define	CD180_GSVR_USERMASK		0xf8	/* user defined bits */
#define	CD180_GSVR_IMASK		0x07	/* interrupt type mask */
#define	CD180_GSVR_NOREQUEST		0x00	/* no request pending */
#define	CD180_GSVR_STATCHG		0x01	/* modem signal change */
#define	CD180_GSVR_TXDATA		0x02	/* tx service request */
#define	CD180_GSVR_RXGOOD		0x03	/* rx service request */
#define	CD180_GSVR_reserved1		0x04	/* reserved */
#define	CD180_GSVR_reserved2		0x05	/* reserved */
#define	CD180_GSVR_reserved3		0x06	/* reserved */
#define	CD180_GSVR_RXEXCEPTION		0x07	/* rx exception request */

/* Service Request Status Register (ro) (CD180C and higher) */
#define	CD180_SRSR_MREQINT		0x01	/* modem request internal */
#define	CD180_SRSR_MREQEXT		0x02	/* modem request external */
#define	CD180_SRSR_TREQINT		0x04	/* tx request internal */
#define	CD180_SRSR_TREQEXT		0x08	/* tx request external */
#define	CD180_SRSR_RREQINT		0x10	/* rx request internal */
#define	CD180_SRSR_RREQEXT		0x20	/* rx request external */
#define	CD180_SRSR_ILV_MASK		0xc0	/* internal service context */
#define	CD180_SRSR_ILV_NONE		0x00	/* not in service context */
#define	CD180_SRSR_ILV_RX		0xc0	/* in rx service context */
#define	CD180_SRSR_ILV_TX		0x80	/* in tx service context */
#define	CD180_SRSR_ILV_MODEM		0x40	/* in modem service context */

/* Global Service Channel Register 1,2,3 (rw) */
#define	CD180_GSCR_CHANNEL(gscr)	(((gscr) >> 2) & 7)

/* Receive Data Count Register (ro) */
#define	CD180_RDCR_MASK			0x0f	/* mask for fifo length */

/* Receive Character Status Register (ro) */
#define	CD180_RCSR_TO			0x80	/* time out */
#define	CD180_RCSR_SCD2			0x40	/* special char detect 2 */
#define	CD180_RCSR_SCD1			0x20	/* special char detect 1 */
#define	CD180_RCSR_SCD0			0x10	/* special char detect 0 */
#define	CD180_RCSR_BE			0x08	/* break exception */
#define	CD180_RCSR_PE			0x04	/* parity exception */
#define	CD180_RCSR_FE			0x02	/* framing exception */
#define	CD180_RCSR_OE			0x01	/* overrun exception */

/* Service Request Enable Register (rw) */
#define	CD180_SRER_DSR			0x80	/* DSR service request */
#define	CD180_SRER_CD			0x40	/* CD service request */
#define	CD180_SRER_CTS			0x20	/* CTS service request */
#define	CD180_SRER_RXD			0x10	/* RXD service request */
#define	CD180_SRER_RXSCD		0x08	/* RX special char request */
#define	CD180_SRER_TXD			0x04	/* TX ready service request */
#define	CD180_SRER_TXE			0x02	/* TX empty service request */
#define	CD180_SRER_NNDT			0x01	/* No new data timeout req */

/* Channel Command Register (rw) */
/* Reset Channel Command */
#define	CD180_CCR_CMD_RESET		0x80	/* chip/channel reset */
#define CD180_CCR_RESETALL		0x01	/* global reset */
#define	CD180_CCR_RESETCHAN		0x00	/* current channel reset */
/* Channel Option Register Command */
#define	CD180_CCR_CMD_COR		0x40	/* channel opt reg changed */
#define	CD180_CCR_CORCHG1		0x02	/* cor1 has changed */
#define	CD180_CCR_CORCHG2		0x04	/* cor2 has changed */
#define	CD180_CCR_CORCHG3		0x08	/* cor3 has changed */
/* Send Special Character Command */
#define	CD180_CCR_CMD_SPC		0x20	/* send special chars changed */
#define	CD180_CCR_SSPC0			0x01	/* send special char 0 change */
#define	CD180_CCR_SSPC1			0x02	/* send special char 1 change */
#define	CD180_CCR_SSPC2			0x04	/* send special char 2 change */
/* Channel Control Command */
#define	CD180_CCR_CMD_CHAN		0x10	/* channel control command */
#define	CD180_CCR_CHAN_TXEN		0x08	/* enable channel tx */
#define	CD180_CCR_CHAN_TXDIS		0x04	/* disable channel tx */
#define	CD180_CCR_CHAN_RXEN		0x02	/* enable channel rx */
#define	CD180_CCR_CHAN_RXDIS		0x01	/* disable channel rx */

/* Channel Option Register 1 (rw) */
#define	CD180_COR1_EVENPAR		0x00	/* even parity */
#define	CD180_COR1_ODDPAR		0x80	/* odd parity */
#define	CD180_COR1_PARMODE_NO		0x00	/* no parity */
#define	CD180_COR1_PARMODE_FORCE	0x20	/* force (odd=1, even=0) */
#define CD180_COR1_PARMODE_NORMAL	0x40	/* normal parity mode */
#define	CD180_COR1_PARMODE_NA		0x60	/* notused */
#define	CD180_COR1_IGNPAR		0x10	/* ignore parity */
#define	CD180_COR1_STOP1		0x00	/* 1 stop bit */
#define	CD180_COR1_STOP15		0x04	/* 1.5 stop bits */
#define	CD180_COR1_STOP2		0x08	/* 2 stop bits */
#define	CD180_COR1_STOP25		0x0c	/* 2.5 stop bits */
#define	CD180_COR1_CS5			0x00	/* 5 bit characters */
#define	CD180_COR1_CS6			0x01	/* 6 bit characters */
#define	CD180_COR1_CS7			0x02	/* 7 bit characters */
#define	CD180_COR1_CS8			0x03	/* 8 bit characters */

/* Channel Option Register 2 (rw) */
#define	CD180_COR2_IXM			0x80	/* implied xon mode */
#define	CD180_COR2_TXIBE		0x40	/* tx in-band flow control */
#define	CD180_COR2_ETC			0x20	/* embedded tx command enbl */
#define	CD180_COR2_LLM			0x10	/* local loopback mode */
#define	CD180_COR2_RLM			0x08	/* remote loopback mode */
#define	CD180_COR2_RTSAO		0x04	/* RTS automatic output enbl */
#define	CD180_COR2_CTSAE		0x02	/* CTS automatic enable */
#define	CD180_COR2_DSRAE		0x01	/* DSR automatic enable */

/* Channel Option Register 3 (rw) */
#define	CD180_COR3_XON2			0x80	/* XON char in spc1&3 */
#define	CD180_COR3_XON1			0x00	/* XON char in spc1 */
#define	CD180_COR3_XOFF2		0x40	/* XOFF char in spc2&4 */
#define	CD180_COR3_XOFF1		0x00	/* XOFF char in spc2 */
#define	CD180_COR3_FCT			0x20	/* flow control transparency */
#define	CD180_COR3_SCDE			0x10	/* special char recognition */
#define	CD180_COR3_RXFIFO_MASK		0x0f	/* rx fifo threshold */

/* Channel Control Status Register (ro) */
#define	CD180_CCSR_RXEN			0x80	/* rx is enabled */
#define	CD180_CCSR_RXFLOFF		0x40	/* rx flow-off */
#define	CD180_CCSR_RXFLON		0x20	/* rx flow-on */
#define	CD180_CCSR_TXEN			0x08	/* tx is enabled */
#define	CD180_CCSR_TXFLOFF		0x04	/* tx flow-off */
#define	CD180_CCSR_TXFLON		0x02	/* tx flow-on */

/* Receiver Bit Register (ro) */
#define	CD180_RBR_RXD			0x40	/* state of RxD pin */
#define	CD180_RBR_STARTHUNT		0x20	/* looking for start bit */

/* Modem Change Register (rw) */
#define	CD180_MCR_DSR			0x80	/* DSR changed */
#define	CD180_MCR_CD			0x40	/* CD changed */
#define	CD180_MCR_CTS			0x20	/* CTS changed */

/* Modem Change Option Register 1 (rw) */
#define	CD180_MCOR1_DSRZD		0x80	/* catch 0->1 DSR changes */
#define	CD180_MCOR1_CDZD		0x40	/* catch 0->1 CD changes */
#define	CD180_MCOR1_CTSZD		0x40	/* catch 0->1 CTS changes */
#define	CD180_MCOR1_DTRTHRESH		0x0f	/* DTR threshold mask */

/* Modem Change Option Register 2 (rw) */
#define	CD180_MCOR2_DSROD		0x80	/* catch 1->0 DSR changes */
#define	CD180_MCOR2_CDOD		0x40	/* catch 1->0 CD changes */
#define	CD180_MCOR2_CTSOD		0x20	/* catch 1->0 CTS changes */

/* Modem Signal Value Register (rw) */
#define	CD180_MSVR_DSR			0x80	/* DSR input state */
#define	CD180_MSVR_CD			0x40	/* CD input state */
#define	CD180_MSVR_CTS			0x20	/* CTS input state */
#define	CD180_MSVR_DTR			0x02	/* DTR output state */
#define	CD180_MSVR_RTS			0x01	/* RTS output state */

/* Modem Signal Value Register - Request To Send (w) (CD180C and higher) */
#define	CD180_MSVRTS_RTS		0x01	/* RTS signal value */

/* Modem Signal Value Register - Data Terminal Ready (w) (CD180C and higher) */
#define	CD180_MSVDTR_DTR		0x02	/* DTR signal value */

/*
 * The register map for the SUNW,spif looks something like:
 *    Offset:		Function:
 *	0000 - 03ff	Boot ROM
 *	0400 - 0407	dtr latches (one per port)
 *	0409 - 07ff	unused
 *	0800 - 087f	CD180 registers (normal mapping)
 *	0880 - 0bff	unused
 *	0c00 - 0c7f	CD180 registers (*iack mapping)
 *	0c80 - 0dff	unused
 *	0e00 - 1fff	PPC registers
 *
 * One note about the DTR latches:  The values stored there are reversed.
 * By writing a 1 to the latch, DTR is lowered, and by writing a 0, DTR
 * is raised.  The latches cannot be read, and no other value can be written
 * there or the system will crash due to "excessive bus loading (see
 * SBus loading and capacitance spec)"
 *
 * The *iack registers are read/written with the IACK bit set.  When
 * the interrupt routine starts, it reads the MRAR, TRAR, and RRAR registers
 * from this mapping.  This signals an interrupt acknowledgement cycle.
 * (NOTE: these are not really the MRAR, TRAR, and RRAR... They are copies
 * of the GSVR, I just mapped them to the same location as the mrar, trar,
 * and rrar because it seemed appropriate).
 */
#define	DTR_REG_OFFSET		0x400		/* DTR latches */
#define	DTR_REG_LEN		0x8
#define	STC_REG_OFFSET		0x800		/* normal cd180 access */
#define	STC_REG_LEN		0x80
#define	ISTC_REG_OFFSET		0xc00		/* IACK cd180 access */
#define	ISTC_REG_LEN		STC_REG_LEN
#define	PPC_REG_OFFSET		0xe00		/* PPC registers */
#define	PPC_REG_LEN		0x200

/*
 * The mapping of minor device number -> card and port is done as
 * follows by default:
 *
 *  +---+---+---+---+---+---+---+---+
 *  | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
 *  +---+---+---+---+---+---+---+---+
 *    |   |   |   |   |   |   |   |
 *    |   |   |   |   |   +---+---+---> port number
 *    |   |   |   |   |
 *    |   |   |   |   +---------------> unused
 *    |   |   |   |
 *    |   |   |   +-------------------> dialout (on tty ports)
 *    |   |   |
 *    |   |   +-----------------------> unused
 *    |   |
 *    +---+---------------------------> card number
 *
 */
#define SPIF_MAX_CARDS		4
#define SPIF_MAX_TTY		8
#define SPIF_MAX_BPP		1

/*
 * device selectors
 */
#define SPIF_CARD(x)	((minor(x) >> 6) & 0x03)
#define SPIF_PORT(x)	(minor(x) & 0x07)
#define STTY_DIALOUT(x) (minor(x) & 0x10)

#define	STTY_RX_FIFO_THRESHOLD	4
#define	STTY_RX_DTR_THRESHOLD	7
#define	CD180_TX_FIFO_SIZE	8		/* 8 chars of fifo */

/*
 * These are the offsets of the MRAR, TRAR, and RRAR in *IACK space.
 * The high bit must be set as per specs for the MSMR, TSMR, and RSMR.
 */
#define	SPIF_MSMR	(0x80 | STC_MRAR)	/* offset of MRAR | 0x80 */
#define	SPIF_TSMR	(0x80 | STC_TRAR)	/* offset of TRAR | 0x80 */
#define	SPIF_RSMR	(0x80 | STC_RRAR)	/* offset of RRAR | 0x80 */

/*
 * "verosc" node tells which oscillator we have.
 */
#define	SPIF_OSC9	1		/* 9.8304 MHz */
#define	SPIF_OSC10	2		/* 10MHz */

/*
 * There are two interrupts, serial gets interrupt[0], and parallel
 * gets interrupt[1]
 */
#define	SERIAL_INTR	0
#define	PARALLEL_INTR	1

/*
 * spif tty flags
 */
#define	STTYF_CDCHG		0x01		/* carrier changed */
#define	STTYF_RING_OVERFLOW	0x02		/* ring buffer overflowed */
#define	STTYF_DONE		0x04		/* done... flush buffers */
#define	STTYF_SET_BREAK		0x08		/* set break signal */
#define	STTYF_CLR_BREAK		0x10		/* clear break signal */
#define	STTYF_STOP		0x20		/* stopped */

#define	STTY_RBUF_SIZE		(2 * 512)
