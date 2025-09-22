/*	$OpenBSD: emuxkivar.h,v 1.11 2016/09/19 06:46:44 ratchov Exp $	*/
/*	$NetBSD: emuxkivar.h,v 1.1 2001/10/17 18:39:41 jdolecek Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Yannick Montulet.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_PCI_EMU10K1VAR_H_
#define _DEV_PCI_EMU10K1VAR_H_

#define	EMU_PCI_CBIO		0x10
#define	EMU_SUBSYS_APS		0x40011102

/*
 * dma memory management
 */

struct dmamem {
	bus_dma_tag_t   dmat;
	bus_size_t      size;
	bus_size_t      align;
	bus_size_t      bound;
	bus_dma_segment_t *segs;
	int             nsegs;
	int             rsegs;
	caddr_t         kaddr;
	bus_dmamap_t    map;
};

#define	KERNADDR(ptr)		((void *)((ptr)->kaddr))
#define	DMASEGADDR(ptr, segno)	((ptr)->segs[segno].ds_addr)
#define	DMAADDR(ptr)		DMASEGADDR(ptr, 0)
#define DMASIZE(ptr)		((ptr)->size)

/*
 * Emu10k1 hardware limits
 */

#define	EMU_PTESIZE		4096
#define	EMU_MAXPTE ((EMU_CHAN_PSST_LOOPSTARTADDR_MASK + 1) /	\
			EMU_PTESIZE)
#define EMU_NUMCHAN		64
#define EMU_NUMRECSRCS	3

#define	EMU_DMA_ALIGN	4096
#define	EMU_DMAMEM_NSEG	1

/*
 * Emu10k1 memory management
 */

struct emuxki_mem {
	LIST_ENTRY(emuxki_mem) next;
	struct dmamem  *dmamem;
	u_int16_t       ptbidx;
#define	EMU_RMEM		0xFFFF		/* recording memory */
};

/*
 * Emu10k1 play channel params
 */

struct emuxki_chanparms_fxsend {
	struct {
		u_int8_t        level, dest;
	} a, b, c, d, e, f, g, h;
};

struct emuxki_chanparms_pitch {
	u_int16_t       initial;/* 4 bits of octave, 12 bits of fractional
				 * octave */
	u_int16_t       current;/* 0x4000 == unity pitch shift */
	u_int16_t       target;	/* 0x4000 == unity pitch shift */
	u_int8_t        envelope_amount;	/* Signed 2's complement, +/-
						 * one octave peak extremes */
};

struct emuxki_chanparms_envelope {
	u_int16_t       current_state;	/* 0x8000-n == 666*n usec delay */
	u_int8_t        hold_time;	/* 127-n == n*(volume ? 88.2 :
					 * 42)msec */
	u_int8_t        attack_time;	/* 0 = infinite, 1 = (volume ? 11 :
					 * 10.9) msec, 0x7f = 5.5msec */
	u_int8_t        sustain_level;	/* 127 = full, 0 = off, 0.75dB
					 * increments */
	u_int8_t        decay_time;	/* 0 = 43.7msec, 1 = 21.8msec, 0x7f =
					 * 22msec */
};

struct emuxki_chanparms_volume {
	u_int16_t current, target;
	struct emuxki_chanparms_envelope envelope;
};

struct emuxki_chanparms_filter {
	u_int16_t       initial_cutoff_frequency;
	/*
	 * 6 most  significant bits are semitones, 2 least significant bits
	 * are fractions
	 */
	u_int16_t       current_cutoff_frequency;
	u_int16_t       target_cutoff_frequency;
	u_int8_t        lowpass_resonance_height;
	u_int8_t        interpolation_ROM;	/* 1 = full band, 7 = low
						 * pass */
	u_int8_t        envelope_amount;	/* Signed 2's complement, +/-
						 * six octaves peak extremes */
	u_int8_t        LFO_modulation_depth;	/* Signed 2's complement, +/-
						 * three octave extremes */
};

struct emuxki_chanparms_loop {
	u_int32_t       start;	/* index in the PTB (in samples) */
	u_int32_t       end;	/* index in the PTB (in samples) */
};

struct emuxki_chanparms_modulation {
	struct emuxki_chanparms_envelope envelope;
	u_int16_t       LFO_state;	/* 0x8000-n = 666*n usec delay */
};

struct emuxki_chanparms_vibrato_LFO {
	u_int16_t       state;		/* 0x8000-n == 666*n usec delay */
	u_int8_t        modulation_depth;	/* Signed 2's complement, +/-
						 * one octave extremes */
	u_int8_t        vibrato_depth;	/* Signed 2's complement, +/- one
					 * octave extremes */
	u_int8_t        frequency;	/* 0.039Hz steps, maximum of 9.85 Hz */
};

struct emuxki_channel {
	u_int8_t        num;	/* voice number */
	struct emuxki_voice *voice;
	struct emuxki_chanparms_fxsend fxsend;
	struct emuxki_chanparms_pitch pitch;
	u_int16_t       initial_attenuation;	/* 0.375dB steps */
	struct emuxki_chanparms_volume volume;
	struct emuxki_chanparms_filter filter;
	struct emuxki_chanparms_loop loop;
	struct emuxki_chanparms_modulation modulation;
	struct emuxki_chanparms_vibrato_LFO vibrato_LFO;
	u_int8_t        tremolo_depth;
};

/*
 * Voices, streams
 */

typedef enum {
	EMU_RECSRC_MIC = 0,
	EMU_RECSRC_ADC,
	EMU_RECSRC_FX,
	EMU_RECSRC_NOTSET
} emuxki_recsrc_t;

struct emuxki_voice {
	struct emuxki_softc *sc;	/* our softc */

	u_int8_t        use;
#define	EMU_VOICE_USE_PLAY		(1 << 0)
	u_int8_t        state;
#define EMU_VOICE_STATE_STARTED	(1 << 0)
	u_int8_t        stereo;
#define	EMU_VOICE_STEREO_NOTSET	0xFF
	u_int8_t        b16;
	u_int32_t       sample_rate;
	union {
		struct emuxki_channel *chan[2];
		emuxki_recsrc_t source;
	} dataloc;
	struct emuxki_mem *buffer;
	u_int16_t       blksize;/* in samples */
	u_int16_t       trigblk;/* blk on which to trigger inth */
	u_int16_t       blkmod;	/* Modulo value to wrap trigblk */
	u_int16_t       timerate;
	void            (*inth) (void *);
	void           *inthparam;
	                LIST_ENTRY(emuxki_voice) next;
};

#if 0 /* Not yet */
/*
 * I intend this to be able to manage things like AC-3
 */
struct emuxki_stream {
	struct emu10k1			*emu;
	u_int8_t			nmono;
	u_int8_t			nstereo;
	struct emuxki_voice		*mono;
	struct emuxki_voice		*stereo;
	LIST_ENTRY(emuxki_stream)	next;
};
#endif /* Not yet */

struct emuxki_softc {
	struct device   sc_dev;

	/* Autoconfig parameters */
	bus_space_tag_t 	sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_addr_t		sc_iob;
	bus_size_t		sc_ios;
	pci_chipset_tag_t	sc_pc;		/* PCI tag */
	bus_dma_tag_t		sc_dmat;
	void			*sc_ih;		/* interrupt handler */

	/* EMU10k1 device structures */
	LIST_HEAD(, emuxki_mem) mem;

	struct dmamem		*ptb;
	struct dmamem		*silentpage;

	struct emuxki_channel	*channel[EMU_NUMCHAN];
	struct emuxki_voice	*recsrc[EMU_NUMRECSRCS];

	LIST_HEAD(, emuxki_voice) voices;
	/* LIST_HEAD(, emuxki_stream)	streams; */

	u_int8_t		timerstate;
#define	EMU_TIMER_STATE_ENABLED	1

	struct ac97_host_if	hostif;
	struct ac97_codec_if	*codecif;
	struct device		*sc_audev;

	struct emuxki_voice	*pvoice, *rvoice, *lvoice;

	int			sc_flags;
#define EMUXKI_SBLIVE		0x0001
#define EMUXKI_AUDIGY		0x0002
#define EMUXKI_AUDIGY2		0x0004
#define EMUXKI_SBLIVE51		0x0008
#define EMUXKI_APS		0x0010
#define EMUXKI_CA0108		0x0020
#define EMUXKI_CA0151		0x0040
};

#endif				/* !_DEV_PCI_EMU10K1VAR_H_ */
