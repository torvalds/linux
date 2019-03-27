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

#ifndef _MTK_SPI_NEWVAR_H_
#define _MTK_SPI_NEWVAR_H_

/* SPI controller interface */

#define MTK_SPITRANS		0x00
#define 	SPIBUSY			(1<<16)
#define		SPISTART		(1<<8)

#define MTK_SPIMASTER		0x28

#define MTK_SPIMOREBUF		0x2C

#define MTK_SPIOPCODE		0x04
#define MTK_SPIDATA		0x08
#define		SPIDATA_MASK		0x000000ff

#define MTK_SPI_WRITE		1
#define MTK_SPI_READ		0

#define MTK_SPIPOLAR		0x38

#endif /* _MTK_SPI_NEWVAR_H_ */
