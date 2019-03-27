/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 * $FreeBSD$
 */
#ifndef _ATH_AH_DECODE_H_
#define _ATH_AH_DECODE_H_
/*
 * Register tracing support.
 *
 * Setting hw.ath.hal.alq=1 enables tracing of all register reads and
 * writes to the file /tmp/ath_hal.log.  The file format is a simple
 * fixed-size array of records.  When done logging set hw.ath.hal.alq=0
 * and then decode the file with the arcode program (that is part of the
 * HAL).  If you start+stop tracing the data will be appended to an
 * existing file.
 */
struct athregrec {
	uint32_t	threadid;
	uint32_t	op	: 8,
			reg	: 24;
	uint32_t	val;
};

enum {
	OP_READ		= 0,		/* register read */
	OP_WRITE	= 1,		/* register write */
	OP_DEVICE	= 2,		/* device identification */
	OP_MARK		= 3,		/* application marker */
};

enum {
	AH_MARK_RESET,			/* ar*Reset entry, bChannelChange */
	AH_MARK_RESET_LINE,		/* ar*_reset.c, line %d */
	AH_MARK_RESET_DONE,		/* ar*Reset exit, error code */
	AH_MARK_CHIPRESET,		/* ar*ChipReset, channel num */
	AH_MARK_PERCAL,			/* ar*PerCalibration, channel num */
	AH_MARK_SETCHANNEL,		/* ar*SetChannel, channel num */
	AH_MARK_ANI_RESET,		/* ar*AniReset, opmode */
	AH_MARK_ANI_POLL,		/* ar*AniReset, listen time */
	AH_MARK_ANI_CONTROL,		/* ar*AniReset, cmd */
	AH_MARK_RX_CTL,			/* RX DMA control */
	AH_MARK_CHIP_POWER,		/* chip power control, mode */
	AH_MARK_CHIP_POWER_DONE,	/* chip power control done, status */
};

enum {
	AH_MARK_RX_CTL_PCU_START,
	AH_MARK_RX_CTL_PCU_STOP,
	AH_MARK_RX_CTL_DMA_START,
	AH_MARK_RX_CTL_DMA_STOP,
	AH_MARK_RX_CTL_DMA_STOP_ERR,
	AH_MARK_RX_CTL_DMA_STOP_OK,
};

#endif /* _ATH_AH_DECODE_H_ */
