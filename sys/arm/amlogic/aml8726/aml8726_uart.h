/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_ARM_AMLOGIC_AML8726_UART_H
#define	_ARM_AMLOGIC_AML8726_UART_H

#define	AML_UART_WFIFO_REG			0

#define	AML_UART_RFIFO_REG			4

#define	AML_UART_CONTROL_REG			8
#define	AML_UART_CONTROL_TX_INT_EN		(1 << 28)
#define	AML_UART_CONTROL_RX_INT_EN		(1 << 27)
#define	AML_UART_CONTROL_CLR_ERR		(1 << 24)
#define	AML_UART_CONTROL_RX_RST			(1 << 23)
#define	AML_UART_CONTROL_TX_RST			(1 << 22)
#define	AML_UART_CONTROL_DB_MASK		(3 << 20)
#define	AML_UART_CONTROL_8_DB			(0 << 20)
#define	AML_UART_CONTROL_7_DB			(1 << 20)
#define	AML_UART_CONTROL_6_DB			(2 << 20)
#define	AML_UART_CONTROL_5_DB			(3 << 20)
#define	AML_UART_CONTROL_P_MASK			(3 << 18)
#define	AML_UART_CONTROL_P_EN			(1 << 19)
#define	AML_UART_CONTROL_P_EVEN			(0 << 18)
#define	AML_UART_CONTROL_P_ODD			(1 << 18)
#define	AML_UART_CONTROL_SB_MASK		(3 << 16)
#define	AML_UART_CONTROL_1_SB			(0 << 16)
#define	AML_UART_CONTROL_2_SB			(1 << 16)
#define	AML_UART_CONTROL_TWO_WIRE_EN		(1 << 15)
#define	AML_UART_CONTROL_RX_EN			(1 << 13)
#define	AML_UART_CONTROL_TX_EN			(1 << 12)
#define	AML_UART_CONTROL_BAUD_MASK		0xfff
#define	AML_UART_CONTROL_BAUD_WIDTH		12

#define	AML_UART_STATUS_REG			12
#define	AML_UART_STATUS_RECV_BUSY		(1 << 26)
#define	AML_UART_STATUS_XMIT_BUSY		(1 << 25)
#define	AML_UART_STATUS_RX_FIFO_OVERFLOW	(1 << 24)
#define	AML_UART_STATUS_TX_FIFO_EMPTY		(1 << 22)
#define	AML_UART_STATUS_TX_FIFO_FULL		(1 << 21)
#define	AML_UART_STATUS_RX_FIFO_EMPTY		(1 << 20)
#define	AML_UART_STATUS_RX_FIFO_FULL		(1 << 19)
#define	AML_UART_STATUS_TX_FIFO_WRITE_ERR	(1 << 18)
#define	AML_UART_STATUS_FRAME_ERR		(1 << 17)
#define	AML_UART_STATUS_PARITY_ERR		(1 << 16)
#define	AML_UART_STATUS_TX_FIFO_CNT_MASK	(0x7f << 8)
#define	AML_UART_STATUS_TX_FIFO_CNT_SHIFT	8
#define	AML_UART_STATUS_RX_FIFO_CNT_MASK	(0x7f << 0)
#define	AML_UART_STATUS_RX_FIFO_CNT_SHIFT	0

#define	AML_UART_MISC_REG			16
#define	AML_UART_MISC_OLD_RX_BAUD		(1 << 30)
#define	AML_UART_MISC_BAUD_EXT_MASK		(0xf << 20)
#define	AML_UART_MISC_BAUD_EXT_SHIFT		20

/*
 * The documentation appears to be incorrect as the
 * IRQ is actually generated when TX FIFO count is
 * * equal to * or less than the selected threshold.
 */
#define	AML_UART_MISC_XMIT_IRQ_CNT_MASK		(0xff << 8)
#define	AML_UART_MISC_XMIT_IRQ_CNT_SHIFT	8

/*
 * The documentation appears to be incorrect as the
 * IRQ is actually generated when RX FIFO count is
 * * equal to * or greater than the selected threshold.
 */
#define	AML_UART_MISC_RECV_IRQ_CNT_MASK		0xff
#define	AML_UART_MISC_RECV_IRQ_CNT_SHIFT	0

/*
 * The new baud rate register is available on the
 * aml8726-m6 and later.
 */
#define	AML_UART_NEW_BAUD_REG			20
#define	AML_UART_NEW_BAUD_USE_XTAL_CLK		(1 << 24)
#define	AML_UART_NEW_BAUD_RATE_EN		(1 << 23)
#define	AML_UART_NEW_BAUD_RATE_MASK		(0x7fffff << 0)
#define	AML_UART_NEW_BAUD_RATE_SHIFT		0

#endif /* _ARM_AMLOGIC_AML8726_UART_H */
