/*-
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * Copyright (c) 2011, Aleksandr Rybalko <ray@FreeBSD.org>
 * Copyright (c) 2013, Alexander A. Mityaev <sansan@adm.ua>
 * Copyright (c) 2016, Stanislav Galabov <sgalabov@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef _MTK_SPIVAR_H_
#define _MTK_SPIVAR_H_

/* SPI controller interface */

#define MTK_SPISTAT		0x00
/* SPIBUSY is alias for SPIBUSY, because SPISTAT have only BUSY bit*/
#define MTK_SPIBUSY		MTK_SPISTAT

#define MTK_SPICFG		0x10
#define		MSBFIRST		(1<<8)
#define		SPICLKPOL		(1<<6)
#define		CAPT_ON_CLK_FALL	(1<<5)
#define		TX_ON_CLK_FALL		(1<<4)
#define		HIZSPI			(1<<3)	/* Set SPI pins to Tri-state */
#define		SPI_CLK_SHIFT		0	/* SPI clock divide control */
#define		SPI_CLK_MASK		0x00000007
#define		SPI_CLK_DIV2		0
#define		SPI_CLK_DIV4		1
#define		SPI_CLK_DIV8		2
#define		SPI_CLK_DIV16		3
#define		SPI_CLK_DIV32		4
#define		SPI_CLK_DIV64		5
#define		SPI_CLK_DIV128		6
#define		SPI_CLK_DISABLED	7

#define MTK_SPICTL		0x14
#define		HIZSMOSI		(1<<3)
#define		START_WRITE		(1<<2)
#define		START_READ		(1<<1)
#define		CS_HIGH			(1<<0)

#define MTK_SPIDATA		0x20
#define		SPIDATA_MASK		0x000000ff

#define MTK_SPI_WRITE		1
#define MTK_SPI_READ		0

#endif /* _MTK_SPIVAR_H_ */
