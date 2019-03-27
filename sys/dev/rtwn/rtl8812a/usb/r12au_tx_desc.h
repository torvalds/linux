/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#ifndef R12AU_TX_DESC_H
#define R12AU_TX_DESC_H

#include <dev/rtwn/rtl8812a/r12a_tx_desc.h>

/* Tx MAC descriptor (USB). */
struct r12au_tx_desc {
	uint16_t	pktlen;
	uint8_t		offset;
	uint8_t		flags0;

	uint32_t	txdw1;
	uint32_t	txdw2;
	uint32_t	txdw3;
	uint32_t	txdw4;
	uint32_t	txdw5;
	uint32_t	txdw6;

	uint16_t	txdsum;
	uint16_t	flags7;
#define R12AU_FLAGS7_AGGNUM_M	0xff00
#define R12AU_FLAGS7_AGGNUM_S	8

	uint32_t	txdw8;
	uint32_t	txdw9;
} __packed __attribute__((aligned(4)));

#endif	/* R12AU_TX_DESC_H */
