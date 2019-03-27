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

#ifndef R88E_TX_DESC_H
#define R88E_TX_DESC_H

#include <dev/rtwn/rtl8192c/r92c_tx_desc.h>

/* Tx MAC descriptor defines (chip-specific). */
/* Tx dword 1. */
#define R88E_TXDW1_MACID_M	0x0000003f
#define R88E_TXDW1_MACID_S	0

/* Tx dword 2. */
#define R88E_TXDW2_AGGEN	0x00001000
#define R88E_TXDW2_AGGBK	0x00010000

/* Tx dword 3. */
#define R88E_TXDSEQ_HWSEQ_EN	0x8000

#endif	/* R88E_TX_DESC_H */
