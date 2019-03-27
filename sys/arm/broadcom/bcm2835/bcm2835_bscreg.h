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

#ifndef	_BCM2835_BSCREG_H_
#define	_BCM2835_BSCREG_H_

#define	BCM_BSC_CORE_CLK	150000000U
#define	BCM_BSC_CTRL		0x00
#define	BCM_BSC_CTRL_I2CEN		(1 << 15)
#define	BCM_BSC_CTRL_INTR		(1 << 10)
#define	BCM_BSC_CTRL_INTT		(1 << 9)
#define	BCM_BSC_CTRL_INTD		(1 << 8)
#define	BCM_BSC_CTRL_ST			(1 << 7)
#define	BCM_BSC_CTRL_CLEAR1		(1 << 5)
#define	BCM_BSC_CTRL_CLEAR0		(1 << 4)
#define	BCM_BSC_CTRL_READ		(1 << 0)
#define	BCM_BSC_CTRL_INT_ALL \
    (BCM_BSC_CTRL_INTR | BCM_BSC_CTRL_INTT | BCM_BSC_CTRL_INTD)

#define	BCM_BSC_STATUS		0x04
#define	BCM_BSC_STATUS_CLKT		(1 << 9)
#define	BCM_BSC_STATUS_ERR		(1 << 8)
#define	BCM_BSC_STATUS_RXF		(1 << 7)
#define	BCM_BSC_STATUS_TXE		(1 << 6)
#define	BCM_BSC_STATUS_RXD		(1 << 5)
#define	BCM_BSC_STATUS_TXD		(1 << 4)
#define	BCM_BSC_STATUS_RXR		(1 << 3)
#define	BCM_BSC_STATUS_TXW		(1 << 2)
#define	BCM_BSC_STATUS_DONE		(1 << 1)
#define	BCM_BSC_STATUS_TA		(1 << 0)
#define	BCM_BSC_STATUS_ERRBITS \
    (BCM_BSC_STATUS_CLKT | BCM_BSC_STATUS_ERR)

#define	BCM_BSC_DLEN		0x08
#define	BCM_BSC_SLAVE		0x0c
#define	BCM_BSC_DATA		0x10
#define	BCM_BSC_CLOCK		0x14
#define	BCM_BSC_DELAY		0x18
#define	BCM_BSC_CLKT		0x1c

#endif	/* _BCM2835_BSCREG_H_ */
