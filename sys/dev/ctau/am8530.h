/*-
 * AMD Am83C30 serial communication controller registers.
 *
 * Copyright (C) 1996 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: am8530.h,v 1.1.2.2 2003/11/12 17:31:21 rik Exp $
 * $FreeBSD$
 */

/*
 * Read/write registers.
 */
#define AM_IVR		2	/* rw2 - interrupt vector register */
#define AM_DAT		8	/* rw8 - data buffer register */
#define AM_TCL		12	/* rw12 - time constant low */
#define AM_TCH		13	/* rw13 - time constant high */
#define AM_SICR		15	/* rw15 - status interrupt control reg */

/*
 * Write only registers.
 */
#define AM_CR		0	/* w0 - command register */
#define AM_IMR		1	/* w1 - interrupt mode register */
#define AM_RCR		3	/* w3 - receive control register */
#define AM_PMR		4	/* w4 - tx/rx parameters and modes reg */
#define AM_TCR		5	/* w5 - transmit control register */
#define AM_SAF		6	/* w6 - sync address field */
#define AM_SFR		7	/* w7 - sync flag register */
#define AM_MICR		9	/* w9 - master interrupt control reg */
#define AM_MCR		10	/* w10 - misc control register */
#define AM_CMR		11	/* w11 - clock mode register */
#define AM_BCR		14	/* w14 - baud rate control register */

/*
 * Read only registers.
 */
#define AM_SR		0	/* r0 - status register */
#define AM_RSR		1	/* r1 - receive status register */
#define AM_IPR		3	/* r3 - interrupt pending register */
#define AM_MSR		10	/* r10 - misc status register */

/*
 * Enhanced mode registers.
 * In enhanced mode registers PMR(w4), TCR(w5) become readable.
 */
#define AM_FBCL		6	/* r6 - frame byte count low */
#define AM_FBCH		7	/* r7 - frame byte count high */
#define AM_RCR_R	9	/* r9 - read RCR(w3) */
#define AM_MCR_R	11	/* r11 - read MCR(w10) */
#define AM_SFR_R	14	/* r14 - read SFR(w7') */

#define AM_A		32	/* channel A offset */

/*
 * Interrupt vector register
 */
#define IVR_A		0x08	/* channel A status */
#define IVR_REASON	0x06	/* interrupt reason mask */
#define IVR_TXRDY	0x00	/* transmit buffer empty */
#define IVR_STATUS	0x02	/* external status interrupt */
#define IVR_RX		0x04	/* receive character available */
#define IVR_RXERR	0x06	/* special receive condition */

/*
 * Interrupt mask register
 */
#define IMR_EXT		0x01	/* ext interrupt enable */
#define IMR_TX		0x02	/* ext interrupt enable */
#define IMR_PARITY	0x04	/* ext interrupt enable */

#define IMR_RX_FIRST	0x08	/* ext interrupt enable */
#define IMR_RX_ALL	0x10	/* ext interrupt enable */
#define IMR_RX_ERR	0x18	/* ext interrupt enable */

#define IMR_WD_RX	0x20	/* wait/request follows receiver fifo */
#define IMR_WD_REQ	0x40	/* wait/request function as request */
#define IMR_WD_ENABLE	0x80	/* wait/request pin enable */

/*
 * Master interrupt control register
 */
#define MICR_VIS	0x01	/* vector includes status */
#define MICR_NV		0x02	/* no interrupt vector */
#define MICR_DLC	0x04	/* disable lower chain */
#define MICR_MIE	0x08	/* master interrupt enable */
#define MICR_HIGH	0x10	/* status high */
#define MICR_NINTACK	0x20	/* interrupt masking without INTACK */

#define MICR_RESET_A	0x80	/* channel reset A */
#define MICR_RESET_B	0x40	/* channel reset B */
#define MICR_RESET_HW	0xc0	/* force hardware reset */

/*
 * Receive status register
 */
#define RSR_FRME	0x10	/* framing error */
#define RSR_RXOVRN	0x20	/* rx overrun error */

/*
 * Command register
 */
#define CR_RST_EXTINT	0x10	/* reset external/status irq */
#define CR_TX_ABORT	0x18	/* send abort (SDLC) */
#define CR_RX_NXTINT	0x20	/* enable irq on next rx character */
#define CR_RST_TXINT	0x28	/* reset tx irq pending */
#define CR_RST_ERROR	0x30	/* error reset */
#define CR_RST_HIUS	0x38	/* reset highest irq under service */
