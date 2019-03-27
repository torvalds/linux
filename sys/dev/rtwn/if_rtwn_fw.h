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

#ifndef IF_RTWN_FW_H
#define IF_RTWN_FW_H

/*
 * Firmware base address.
 */
#define R92C_FW_START_ADDR		0x1000
#define R92C_FW_PAGE_SIZE		4096
#define R92C_FW_MAX_BLOCK_SIZE		196

/*
 * Firmware image header.
 */
struct r92c_fw_hdr {
	/* QWORD0 */
	uint16_t	signature;
	uint8_t		category;
	uint8_t		function;
	uint16_t	version;
	uint16_t	subversion;
	/* QWORD1 */
	uint8_t		month;
	uint8_t		date;
	uint8_t		hour;
	uint8_t		minute;
	uint16_t	ramcodesize;
	uint16_t	reserved2;
	/* QWORD2 */
	uint32_t	svnidx;
	uint32_t	reserved3;
	/* QWORD3 */
	uint32_t	reserved4;
	uint32_t	reserved5;
} __packed;


int		rtwn_load_firmware(struct rtwn_softc *);

#endif	/* IF_RTWN_FW_H */
