/* $OpenBSD: auixpvar.h,v 1.5 2020/06/27 00:33:59 jsg Exp $ */
/* $NetBSD: auixpvar.h,v 1.3 2005/01/12 15:54:34 kent Exp $*/

/*
 * Copyright (c) 2004, 2005 Reinoud Zandijk <reinoud@netbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


/*
 * NetBSD audio driver for ATI IXP-{150,200,...} audio driver hardware.
 */

#define DMA_DESC_CHAIN	255

/* audio format structure describing our hardware capabilities */
/* XXX min and max sample rates are for AD1888 codec XXX */
#define AUIXP_NFORMATS 6

#define AUIXP_MINRATE  7000
#define AUIXP_MAXRATE 48000

/* auixp structures; used to record alloced DMA space */
struct auixp_dma {
	/* bus mappings */
	bus_dmamap_t		 map;
	caddr_t			 addr;
	bus_dma_segment_t	 segs[1];
	int			 nsegs;
	size_t			 size;

	/* audio feeder */
	void			 (*intr)(void *);
	void			*intrarg;

	/* status and setup bits */
	int			 running;
	u_int32_t		 linkptr;
	u_int32_t		 dma_enable_bit;

	/* linked list of all mapped area's */
	SLIST_ENTRY(auixp_dma)	 dma_chain;
};

struct auixp_codec {
	struct auixp_softc	*sc;

	int			 present;
	int			 codec_nr;

	struct ac97_codec_if	*codec_if;
	struct ac97_host_if	 host_if;
	enum ac97_host_flags	 codec_flags;
};

struct auixp_softc {
	struct device		sc_dev;
	void			*sc_ih;

	/* card properties */
	int			has_4ch, has_6ch, is_fixed, has_spdif;

	/* bus tags */
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_addr_t		sc_iob;
	bus_size_t		sc_ios;

	pcitag_t		sc_tag;
	pci_chipset_tag_t	sc_pct;

	bus_dma_tag_t		sc_dmat;

	/* DMA business */
	struct auixp_dma	*sc_output_dma;
	struct auixp_dma	*sc_input_dma;

	/* list of allocated DMA pieces */
	SLIST_HEAD(auixp_dma_list, auixp_dma) sc_dma_list;

	/* codec */
	struct auixp_codec	sc_codec;
	int			sc_codec_not_ready_bits;

	/* last set audio parameters */
	struct audio_params	sc_play_params;
	struct audio_params	sc_rec_params;
};
