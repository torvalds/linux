/*	$OpenBSD: rtsxvar.h,v 1.6 2017/10/09 16:12:20 stsp Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2012 Stefan Sperling <stsp@openbsd.org>
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

#ifndef _RTSXVAR_H_
#define _RTSXVAR_H_

#include <machine/bus.h>

/* Number of registers to save for suspend/resume in terms of their ranges. */
#define RTSX_NREG ((0XFDAE - 0XFDA0) + (0xFD69 - 0xFD32) + (0xFE34 - 0xFE20))

struct rtsx_softc {
	struct device	sc_dev;
	struct device	*sdmmc;		/* generic SD/MMC device */
	bus_space_tag_t	iot;		/* host register set tag */
	bus_space_handle_t ioh;		/* host register set handle */
	bus_dma_tag_t	dmat;		/* DMA tag from attachment driver */
	bus_dmamap_t	dmap_cmd;	/* DMA map for command transfer */
	bus_dmamap_t	dmap_data;	/* DMA map for data transfer */
	bus_dmamap_t	dmap_adma;	/* DMA map for ADMA SG descriptors */
	caddr_t		admabuf;	/* buffer for ADMA SG descriptors */
	bus_dma_segment_t adma_segs[1];	/* segments for ADMA SG buffer */
	int		flags;
	u_int32_t 	intr_status;	/* soft interrupt status */
	u_int8_t	regs[RTSX_NREG];/* host controller state */
	u_int32_t	regs4[6];	/* host controller state */
};

/* Host controller functions called by the attachment driver. */
int	rtsx_attach(struct rtsx_softc *, bus_space_tag_t,
	    bus_space_handle_t, bus_size_t, bus_dma_tag_t, int);
int	rtsx_activate(struct device *, int);
int	rtsx_intr(void *);

/* flag values */
#define	RTSX_F_CARD_PRESENT	0x01
#define	RTSX_F_SDIO_SUPPORT	0x02
#define	RTSX_F_5209		0x04
#define	RTSX_F_5229		0x08
#define	RTSX_F_5229_TYPE_C	0x10
#define	RTSX_F_525A		0x20

#endif
