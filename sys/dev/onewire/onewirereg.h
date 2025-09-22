/*	$OpenBSD: onewirereg.h,v 1.2 2009/06/02 18:19:47 deraadt Exp $	*/

/*
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
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

#ifndef _DEV_ONEWIRE_ONEWIREREG_H_
#define _DEV_ONEWIRE_ONEWIREREG_H_

/*
 * 1-Wire bus protocol definitions.
 */

/*
 * 64-bit ROM section.
 */

/* Family code */
#define ONEWIRE_ROM_FAMILY(x)		((u_char)(x) & 0xff)
#define ONEWIRE_ROM_FAMILY_TYPE(x)	((u_char)(x) & 0x7f)
#define ONEWIRE_ROM_FAMILY_CUSTOM(x)	((u_char)((x) >> 7) & 0x1)

/* Serial number */
#define ONEWIRE_ROM_SN(x)		(((x) >> 8) & 0xffffffffffffULL)

/* CRC */
#define ONEWIRE_ROM_CRC(x)		(((x) >> 56) & 0xff)

/*
 * Command set.
 */

/* ROM commands */
#define ONEWIRE_CMD_READ_ROM		0x33
#define ONEWIRE_CMD_SKIP_ROM		0xcc
#define ONEWIRE_CMD_MATCH_ROM		0x55
#define ONEWIRE_CMD_SEARCH_ROM		0xf0
#define ONEWIRE_CMD_OVERDRIVE_SKIP_ROM	0x3c
#define ONEWIRE_CMD_OVERDRIVE_MATCH_ROM	0x69

/* Scratchpad commands */
#define ONEWIRE_CMD_READ_SCRATCHPAD	0xaa
#define ONEWIRE_CMD_WRITE_SCRATCHPAD	0x0f
#define ONEWIRE_CMD_COPY_SCRATCHPAD	0x55

/* Memory commands */
#define ONEWIRE_CMD_READ_MEMORY		0xf0
#define ONEWIRE_CMD_WRITE_MEMORY	0x0f
#define ONEWIRE_CMD_EXT_READ_MEMORY	0xa5

/* Password commands */
#define ONEWIRE_CMD_READ_SUBKEY		0x66
#define ONEWIRE_CMD_WRITE_SUBKEY	0x99
#define ONEWIRE_CMD_WRITE_PASSWORD	0x5a

/* Status commands */
#define ONEWIRE_CMD_READ_STATUS		0xaa
#define ONEWIRE_CMD_WRITE_STATUS	0x55

#endif	/* !_DEV_ONEWIRE_ONEWIREREG_H_ */
