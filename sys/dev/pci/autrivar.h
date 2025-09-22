/*	$OpenBSD: autrivar.h,v 1.4 2010/08/27 18:50:56 deraadt Exp $	*/

/*
 * Copyright (c) 2001 SOMEYA Yoshihiko and KUROSAWA Takahiro.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_PCI_AUTRIVAR_H_
#define	_DEV_PCI_AUTRIVAR_H_

/*
 * softc
 */
struct autri_dma {
	bus_dmamap_t		map;
	caddr_t			addr;		/* VA */
	bus_dma_segment_t	segs[1];
	int			nsegs;
	size_t			size;
	struct autri_dma	*next;
};

struct autri_codec_softc {
	struct device		sc_dev;		/* base device */
	struct autri_softc	*sc;
	int			id;
	int			status_data;
	int			status_addr;
	struct ac97_host_if	host_if;
	struct ac97_codec_if	*codec_if;
	int			flags;
};

struct autri_chstatus {
	void		(*intr)(void *); /* rint/pint */
	void		*intr_arg;	/* arg for intr */
	u_int		offset;		/* filled up to here */
	u_int		blksize;
	u_int		factor;		/* byte per sample */
	u_int		length;		/* ring buffer length */
	struct autri_dma *dma;		/* DMA handle for ring buf */

	int		ch;
	int		ch_intr;
#if 0
	u_int		csoint;
	u_int		count;
#endif
};

struct autri_softc {
	struct device		sc_dev;		/* base device */
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_pt;
	pcireg_t		sc_devid;
	void			*sc_ih;		/* interrupt vectoring */
	bus_space_tag_t		memt;
	bus_space_handle_t	memh;
	bus_space_tag_t		iot;
	bus_space_handle_t	ioh;
	bus_dma_tag_t		sc_dmatag;	/* DMA tag */
	u_int			sc_flags;

	struct autri_codec_softc sc_codec;
	struct autri_dma	*sc_dmas;	/* List of DMA handles */

	u_int32_t		sc_class;
	int			sc_revision;

	/*
	 * Play/record status
	 */
	struct autri_chstatus	sc_play, sc_rec;

#if NMIDI > 0
	void	(*sc_iintr)(void *, int);	/* midi input ready handler */
	void	(*sc_ointr)(void *);		/* midi output ready handler */
	void	*sc_arg;
#endif

};

#endif /* _DEV_PCI_AUTRIVAR_H_ */

