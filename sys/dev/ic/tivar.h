/*	$OpenBSD: tivar.h,v 1.2 2009/08/29 22:55:48 kettenis Exp $	*/

/*
 * Copyright (c) 2004 Theo de Raadt <deraadt@openbsd.org>
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

struct tigon_firmware {
	int		FwReleaseMajor;
	int		FwReleaseMinor;
	int		FwReleaseFix;
	u_int32_t	FwStartAddr;

	u_int32_t	FwTextAddr;
	int		FwTextLen;
	u_int32_t	FwRodataAddr;
	int		FwRodataLen;

	u_int32_t 	FwDataAddr;
	int		FwDataLen;
	u_int32_t	FwSbssAddr;
	int		FwSbssLen;

	u_int32_t	FwBssAddr;
	int		FwBssLen;

	int		FwTextOffset;
	int		FwRodataOffset;
	int		FwDataOffset;

	u_char		data[1];
};

struct ti_softc;

int	ti_attach(struct ti_softc *sc);
int	ti_intr(void *);
