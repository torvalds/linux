/*	$OpenBSD: pdu.c,v 1.1 2025/05/02 06:12:53 dlg Exp $ */

/*
 * Copyright (c) 2024 David Gwynne <dlg@openbsd.org>
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
 */

#include <stdlib.h>
#include <stdint.h>

#include "pdu.h"

uint16_t
pdu_u16(const uint8_t *buf)
{
	uint16_t u16;

	u16 = (uint16_t)buf[0] << 8;
	u16 |= (uint16_t)buf[1];

	return (u16);
}

uint32_t
pdu_u32(const uint8_t *buf)
{
	uint32_t u32;

	u32 = (uint32_t)buf[0] << 24;
	u32 |= (uint32_t)buf[1] << 16;
	u32 |= (uint32_t)buf[2] << 8;
	u32 |= (uint32_t)buf[3];

	return (u32);
}

static int
tlv_read(struct tlv *tlv, const uint8_t *buf, size_t len, unsigned int offset)
{
	unsigned int hdr;
	unsigned int plen;

	len -= offset;
	if (len == 0)
		return (0);
	if (len < 2)
		return (-1);

	buf += offset;
	hdr = pdu_u16(buf);

	/* End of LLDPDU TLV */
	if (hdr == 0)
		return (0);

	plen = hdr & 0x1ff;

	len -= 2;
	if (len < plen)
		return (-1);

	tlv->tlv_payload = buf + 2;
	tlv->tlv_type = hdr >> 9;
	tlv->tlv_len = plen;
	tlv->tlv_offset = offset;

	return (1);
}

int
tlv_first(struct tlv *tlv, const void *pdu, size_t len)
{
	return (tlv_read(tlv, pdu, len, 0));
}

int
tlv_next(struct tlv *tlv, const void *pdu, size_t len)
{
	unsigned int offset = tlv->tlv_offset + 2 + tlv->tlv_len;

	return (tlv_read(tlv, pdu, len, offset));
}
