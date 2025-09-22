/*	$OpenBSD: starfire.h,v 1.1 2008/03/16 22:18:53 kettenis Exp $	*/

/*
 * Copyright (c) 2008 Mark Kettenis
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

/*
 * Starfire-specific definitions.
 */

#define STARFIRE_IO_BASE	0x10000000000ULL

#define STARFIRE_UPS_MID_SHIFT	33
#define STARFIRE_UPS_BRD_SHIFT	36
#define STARFIRE_UPS_BUS_SHIFT	6

#define STARFIRE_PSI_BASE	0x100f8000000ULL
#define STARFIRE_PSI_PCREG_OFF	0x4000000ULL

#define STARFIRE_PC_PORT_ID	0x0000d0UL
#define STARFIRE_PC_INT_MAP	0x000200UL

#define STARFIRE_UPAID2HWMID(upaid) \
    (((upaid & 0x3c) << 1) | ((upaid & 0x40) >> 4) | (upaid & 0x3))

#define STARFIRE_UPAID2UPS(upaid) \
    (((u_int64_t)STARFIRE_UPAID2HWMID(upaid) << \
	STARFIRE_UPS_MID_SHIFT) | STARFIRE_IO_BASE)

void	starfire_pc_ittrans_init(int);
