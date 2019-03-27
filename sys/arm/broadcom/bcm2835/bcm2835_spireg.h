/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2013 Luiz Otavio O Souza <loos@freebsd.org>
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

#ifndef	_BCM2835_SPIREG_H_
#define	_BCM2835_SPIREG_H_

#define	SPI_CORE_CLK	250000000U
#define	SPI_CS		0x00
#define	SPI_CS_LEN_LONG		(1 << 25)
#define	SPI_CS_DMA_LEN		(1 << 24)
#define	SPI_CS_CSPOL2		(1 << 23)
#define	SPI_CS_CSPOL1		(1 << 22)
#define	SPI_CS_CSPOL0		(1 << 21)
#define	SPI_CS_RXF		(1 << 20)
#define	SPI_CS_RXR		(1 << 19)
#define	SPI_CS_TXD		(1 << 18)
#define	SPI_CS_RXD		(1 << 17)
#define	SPI_CS_DONE		(1 << 16)
#define	SPI_CS_LEN		(1 << 13)
#define	SPI_CS_REN		(1 << 12)
#define	SPI_CS_ADCS		(1 << 11)
#define	SPI_CS_INTR		(1 << 10)
#define	SPI_CS_INTD		(1 << 9)
#define	SPI_CS_DMAEN		(1 << 8)
#define	SPI_CS_TA		(1 << 7)
#define	SPI_CS_CSPOL		(1 << 6)
#define	SPI_CS_CLEAR_RXFIFO	(1 << 5)
#define	SPI_CS_CLEAR_TXFIFO	(1 << 4)
#define	SPI_CS_CPOL		(1 << 3)
#define	SPI_CS_CPHA		(1 << 2)
#define	SPI_CS_MASK		0x3
#define	SPI_FIFO	0x04
#define	SPI_CLK		0x08
#define	SPI_CLK_MASK		0xffff
#define	SPI_DLEN	0x0c
#define	SPI_DLEN_MASK		0xffff
#define	SPI_LTOH	0x10
#define	SPI_LTOH_MASK		0xf
#define	SPI_DC		0x14
#define	SPI_DC_RPANIC_SHIFT	24
#define	SPI_DC_RPANIC_MASK	(0xff << SPI_DC_RPANIC_SHIFT)
#define	SPI_DC_RDREQ_SHIFT	16
#define	SPI_DC_RDREQ_MASK	(0xff << SPI_DC_RDREQ_SHIFT)
#define	SPI_DC_TPANIC_SHIFT	8
#define	SPI_DC_TPANIC_MASK	(0xff << SPI_DC_TPANIC_SHIFT)
#define	SPI_DC_TDREQ_SHIFT	0
#define	SPI_DC_TDREQ_MASK	0xff

#endif	/* _BCM2835_SPIREG_H_ */
