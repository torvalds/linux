/*-
 * Copyright (c) 2010 Aleksandr Rybalko.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_MTKUART_H
#define	_MTKUART_H

#undef	uart_getreg
#undef	uart_setreg
#define	uart_getreg(bas, reg)		\
	bus_space_read_4((bas)->bst, (bas)->bsh, reg)
#define	uart_setreg(bas, reg, value)	\
	bus_space_write_4((bas)->bst, (bas)->bsh, reg, value)

/* UART registers */
#define	UART_RX_REG	0x00
#define	UART_TX_REG	0x04

#define	UART_IER_REG	0x08
#define		UART_IER_EDSSI		(1<<3) /* Only full UART */
#define		UART_IER_ELSI		(1<<2)
#define		UART_IER_ETBEI		(1<<1)
#define		UART_IER_ERBFI		(1<<0)

#define	UART_IIR_REG	0x0c
#define		UART_IIR_RXFIFO		(1<<7)
#define		UART_IIR_TXFIFO		(1<<6)
#define		UART_IIR_ID_MST		0
#define		UART_IIR_ID_THRE	1
#define		UART_IIR_ID_DR		2
#define		UART_IIR_ID_LINESTATUS	3
#define		UART_IIR_ID_DR2		6
#define		UART_IIR_ID_SHIFT	1
#define		UART_IIR_ID_MASK	0x0000000e
#define		UART_IIR_INTP		(1<<0)

#define	UART_FCR_REG	0x10
#define		UART_FCR_RXTGR_1	(0<<6)
#define		UART_FCR_RXTGR_4	(1<<6)
#define		UART_FCR_RXTGR_8	(2<<6)
#define		UART_FCR_RXTGR_12	(3<<6)
#define		UART_FCR_TXTGR_1	(0<<4)
#define		UART_FCR_TXTGR_4	(1<<4)
#define		UART_FCR_TXTGR_8	(2<<4)
#define		UART_FCR_TXTGR_12	(3<<4)
#define		UART_FCR_DMA		(1<<3)
#define		UART_FCR_TXRST		(1<<2)
#define		UART_FCR_RXRST		(1<<1)
#define		UART_FCR_FIFOEN		(1<<0)

#define	UART_LCR_REG	0x14
#define		UART_LCR_DLAB	(1<<7)
#define		UART_LCR_BRK	(1<<6)
#define		UART_LCR_FPAR	(1<<5)
#define		UART_LCR_EVEN	(1<<4)
#define		UART_LCR_PEN	(1<<3)
#define		UART_LCR_STB_15	(1<<2)
#define		UART_LCR_5B	0
#define		UART_LCR_6B	1
#define		UART_LCR_7B	2
#define		UART_LCR_8B	3

#define	UART_MCR_REG	0x18
#define		UART_MCR_LOOP	(1<<4)
#define		UART_MCR_OUT2_L	(1<<3) /* Only full UART */
#define		UART_MCR_OUT1_L	(1<<2) /* Only full UART */
#define		UART_MCR_RTS_L	(1<<1) /* Only full UART */
#define		UART_MCR_DTR_L	(1<<0) /* Only full UART */

#define	UART_LSR_REG	0x1c
#define		UART_LSR_ERINF	(1<<7)
#define		UART_LSR_TEMT	(1<<6)
#define		UART_LSR_THRE	(1<<5)
#define		UART_LSR_BI	(1<<4)
#define		UART_LSR_FE	(1<<3)
#define		UART_LSR_PE	(1<<2)
#define		UART_LSR_OE	(1<<1)
#define		UART_LSR_DR	(1<<0)

#define	UART_MSR_REG	0x20 	/* Only full UART */
#define		UART_MSR_DCD	(1<<7) /* Only full UART */
#define		UART_MSR_RI	(1<<6) /* Only full UART */
#define		UART_MSR_DSR	(1<<5) /* Only full UART */
#define		UART_MSR_CTS	(1<<4) /* Only full UART */
#define		UART_MSR_DDCD	(1<<3) /* Only full UART */
#define		UART_MSR_TERI	(1<<2) /* Only full UART */
#define		UART_MSR_DDSR	(1<<1) /* Only full UART */
#define		UART_MSR_DCTS	(1<<0) /* Only full UART */

#define	UART_CDDL_REG	0x28
#define	UART_CDDLL_REG	0x2c
#define	UART_CDDLH_REG	0x30

#define	UART_IFCTL_REG	0x34
#define		UART_IFCTL_IFCTL	(1<<0)

int	uart_cnattach(void);
#endif	/* _MTKUART_H */
