/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under DARPA contract HR0011-18-C-0016 ("ECATS"), as part of the
 * DARPA SSITH research programme.
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

#ifndef	_UART_DEV_LOWRISC_H_
#define	_UART_DEV_LOWRISC_H_

#define	UART_DR				0x0000
#define	 DR_DATA_S			0
#define	 DR_DATA_M			0xff
#define	 DR_RX_ERR			(1 << 8)
#define	 DR_RX_FIFO_EMPTY		(1 << 9)
#define	 DR_TX_FIFO_FULL		(1 << 10)
#define	 DR_RX_FIFO_FULL		(1 << 11)
#define	UART_INT_STATUS			0x1000
#define	 INT_STATUS_ACK			1
#define	UART_BAUD			0x2000	/* write-only */
#define	 BAUD_115200			108
#define	UART_STAT_RX			0x2000	/* read-only */
#define	 STAT_RX_FIFO_RD_COUNT_S	0
#define	 STAT_RX_FIFO_RD_COUNT_M	(0xffff << STAT_RX_FIFO_RD_COUNT_S)
#define	 STAT_RX_FIFO_WR_COUNT_S	16
#define	 STAT_RX_FIFO_WR_COUNT_M	(0xffff << STAT_RX_FIFO_WR_COUNT_S)
#define	UART_STAT_TX			0x2004
#define	 STAT_TX_FIFO_RD_COUNT_S	0
#define	 STAT_TX_FIFO_RD_COUNT_M	(0xffff << STAT_TX_FIFO_RD_COUNT_S)
#define	 STAT_TX_FIFO_WR_COUNT_S	16
#define	 STAT_TX_FIFO_WR_COUNT_M	(0xffff << STAT_TX_FIFO_WR_COUNT_S)

#define	GETREG(bas, reg)						\
    bus_space_read_2((bas)->bst, (bas)->bsh, (reg))
#define	SETREG(bas, reg, value)						\
    bus_space_write_2((bas)->bst, (bas)->bsh, (reg), (value))

#endif	/* _UART_DEV_LOWRISC_H_ */
