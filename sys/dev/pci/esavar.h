/* $OpenBSD: esavar.h,v 1.3 2024/10/22 21:50:02 jsg Exp $ */
/* $NetBSD: esavar.h,v 1.4 2002/03/16 14:34:01 jmcneill Exp $ */

/*
 * Copyright (c) 2001, 2002 Jared D. McNeill <jmcneill@invisible.yi.org>
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
 * ESS Allegro-1 / Maestro3 Audio Driver
 * 
 * Based on the FreeBSD maestro3 driver
 *
 */

/*
 * Number of simultaneous voices
 *
 * NOTE: The current code attaches audio0 thru audioESA_NUM_VOICES-1
 *       to this driver, and a lot of people probably don't want that.
 *       So, we'll default to 1 but we'll allow for the possibility of
 *       more.
 *
 * The current MINISRC image limits us to a maximum of 4 simultaneous voices.
 */
#ifndef ESA_NUM_VOICES
#define	ESA_NUM_VOICES		1
#endif

#define KERNADDR(p)	((void *)((p)->addr))
#define	DMAADDR(p)	((p)->map->dm_segs[0].ds_addr)

#define ESA_MINRATE	8000
#define ESA_MAXRATE	48000

struct esa_list {
	int			currlen;
	int			mem_addr;
	int			max;
	int			indexmap[ESA_NUM_VOICES * 2];
};

struct esa_dma {
	bus_dmamap_t		map;
	caddr_t			addr;
	bus_dma_segment_t	segs[1];
	int			nsegs;
	size_t			size;
	struct esa_dma		*next;
};

struct esa_channel {
	int			active;
	int			data_offset;
	int			index;
	size_t			bufsize;
	int			blksize;
	int			pos;
	void			*buf;
	u_int32_t		start;
	u_int32_t		count;

	/* mode settings */
	struct audio_params	mode;
	
	void			(*intr)(void *);
	void			*arg;
};

struct esa_voice {
	struct device		*parent;	/* pointer to our parent */
	struct esa_channel	play;
	struct esa_channel	rec;
	struct esa_dma		*dma;
	int			inlist;
	int			index;	/* 0: play, 1: record */
};

struct esa_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_addr_t		sc_iob;
	bus_size_t		sc_ios;

	pcitag_t		sc_tag;
	pci_chipset_tag_t	sc_pct;
	bus_dma_tag_t		sc_dmat;
	pcireg_t		sc_pcireg;

	void			*sc_ih;

	struct ac97_codec_if	*codec_if;
	struct ac97_host_if	host_if;
	enum ac97_host_flags	codec_flags;

	struct device		*sc_audiodev[ESA_NUM_VOICES];

	struct esa_voice	voice[ESA_NUM_VOICES];
	struct esa_dma		*sc_dmas;
	int			count;

	/* timer management */
	int			sc_ntimers;

	/* packed list structures */
	struct esa_list		mixer_list;
	struct esa_list		adc1_list;
	struct esa_list		dma_list;
	struct esa_list		msrc_list;

	int			type;		/* Allegro-1 or Maestro 3? */
	int			delay1, delay2;

	u_int16_t		*savemem;
};
