/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Adrian Chadd <adrian@FreeBSD.org>
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

/*
 *  Atheros AR933x SoC UART registers
 */
#ifndef	__AR933X_UART_H__
#define	__AR933X_UART_H__

#define	AR933X_UART_REGS_SIZE		20
#define	AR933X_UART_FIFO_SIZE		16

#define	AR933X_UART_DATA_REG		0x00
#define	AR933X_UART_CS_REG		0x04
#define	AR933X_UART_CLOCK_REG		0x08
#define	AR933X_UART_INT_REG		0x0c
#define	AR933X_UART_INT_EN_REG		0x10

#define	AR933X_UART_DATA_TX_RX_MASK	0xff
#define	AR933X_UART_DATA_RX_CSR		(1 << 8)
#define	AR933X_UART_DATA_TX_CSR		(1 << 9)

#define	AR933X_UART_CS_PARITY_S		0
#define	AR933X_UART_CS_PARITY_M		0x3
#define	  AR933X_UART_CS_PARITY_NONE	0
#define	  AR933X_UART_CS_PARITY_ODD	1
#define	  AR933X_UART_CS_PARITY_EVEN	2
#define	AR933X_UART_CS_IF_MODE_S	2
#define	AR933X_UART_CS_IF_MODE_M	0x3
#define	  AR933X_UART_CS_IF_MODE_NONE	0
#define	  AR933X_UART_CS_IF_MODE_DTE	1
#define	  AR933X_UART_CS_IF_MODE_DCE	2
#define	AR933X_UART_CS_FLOW_CTRL_S	4
#define	AR933X_UART_CS_FLOW_CTRL_M	0x3
#define	AR933X_UART_CS_DMA_EN		(1 << 6)
#define	AR933X_UART_CS_TX_READY_ORIDE	(1 << 7)
#define	AR933X_UART_CS_RX_READY_ORIDE	(1 << 8)
#define	AR933X_UART_CS_TX_READY		(1 << 9)
#define	AR933X_UART_CS_RX_BREAK		(1 << 10)
#define	AR933X_UART_CS_TX_BREAK		(1 << 11)
#define	AR933X_UART_CS_HOST_INT		(1 << 12)
#define	AR933X_UART_CS_HOST_INT_EN	(1 << 13)
#define	AR933X_UART_CS_TX_BUSY		(1 << 14)
#define	AR933X_UART_CS_RX_BUSY		(1 << 15)

#define	AR933X_UART_CLOCK_SCALE_M	0xff
#define	AR933X_UART_CLOCK_SCALE_S	16
#define	AR933X_UART_CLOCK_STEP_M	0xffff
#define	AR933X_UART_CLOCK_STEP_S	0

#define	AR933X_UART_MAX_SCALE		0xff
#define	AR933X_UART_MAX_STEP		0xffff

#define	AR933X_UART_INT_RX_VALID	(1 << 0)
#define	AR933X_UART_INT_TX_READY	(1 << 1)
#define	AR933X_UART_INT_RX_FRAMING_ERR	(1 << 2)
#define	AR933X_UART_INT_RX_OFLOW_ERR	(1 << 3)
#define	AR933X_UART_INT_TX_OFLOW_ERR	(1 << 4)
#define	AR933X_UART_INT_RX_PARITY_ERR	(1 << 5)
#define	AR933X_UART_INT_RX_BREAK_ON	(1 << 6)
#define	AR933X_UART_INT_RX_BREAK_OFF	(1 << 7)
#define	AR933X_UART_INT_RX_FULL		(1 << 8)
#define	AR933X_UART_INT_TX_EMPTY	(1 << 9)
#define	AR933X_UART_INT_ALLINTS		0x3ff

#endif /* __AR933X_UART_H__ */
