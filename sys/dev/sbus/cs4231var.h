/*	$OpenBSD: cs4231var.h,v 1.9 2006/06/02 20:00:56 miod Exp $	*/

/*
 * Copyright (c) 1999 Jason L. Wright (jason@thought.net)
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

/*
 * Driver for CS4231 based audio found in some sun4m systems
 */

/*
 * List of device memory allocations (see cs4231_malloc/cs4231_free).
 */
struct cs_dma {
	struct cs_dma *		next;
	caddr_t			addr;
	bus_dmamap_t		dmamap;
	bus_dma_segment_t	segs[1];
	int			nsegs;
	size_t			size;
};

struct cs_volume {
	u_int8_t	left;
	u_int8_t	right;
};

struct cs_channel {
	void		(*cs_intr)(void *);	/* interrupt handler */
	void		*cs_arg;		/* interrupt arg */
	struct cs_dma	*cs_curdma;		/* current dma block */
	u_int32_t	cs_cnt;			/* current block count */
	u_int32_t	cs_blksz;		/* current block size */
	u_int32_t	cs_segsz;		/* current segment size */
	int		cs_locked;		/* channel locked? */
};

struct cs4231_softc {
	struct	device sc_dev;		/* base device */
	struct	intrhand sc_ih;		/* interrupt vectoring */
	bus_dma_tag_t sc_dmatag;
	bus_space_tag_t	sc_bustag;	/* CS4231/APC register tag */
	bus_space_handle_t sc_regs;	/* CS4231/APC register handle */
	int	sc_burst;		/* XXX: DMA burst size in effect */
	int	sc_open;		/* already open? */

	struct cs_channel sc_playback, sc_capture;

	char		sc_mute[9];	/* which devs are muted */
	u_int8_t	sc_out_port;	/* output port */
	u_int8_t	sc_in_port;	/* input port */
	struct	cs_volume sc_volume[9];	/* software volume */
	struct	cs_volume sc_adc;	/* adc volume */

	int sc_format_bits;
	int sc_speed_bits;
	int sc_precision;
	int sc_need_commit;
	int sc_channels;
	struct cs_dma	*sc_dmas;	/* dma list */
};
