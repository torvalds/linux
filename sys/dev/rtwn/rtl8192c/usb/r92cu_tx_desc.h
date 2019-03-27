/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef R92CU_TX_DESC_H
#define R92CU_TX_DESC_H

#include <dev/rtwn/rtl8192c/r92c_tx_desc.h>

/* Tx MAC descriptor (USB). */
struct r92cu_tx_desc {
	uint16_t 	pktlen;
	uint8_t		offset;
	uint8_t		flags0;

	uint32_t	txdw1;
	uint32_t	txdw2;
	uint16_t	txdw3;
	uint16_t	txdseq;

	uint32_t	txdw4;
	uint32_t	txdw5;
	uint32_t	txdw6;

	uint16_t	txdsum;
	uint16_t	pad;
} __packed __attribute__((aligned(4)));

#endif	/* R92CU_TX_DESC_H */