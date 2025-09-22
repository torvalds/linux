/*	$OpenBSD: gusvar.h,v 1.14 2024/05/29 00:48:14 jsg Exp $	*/
/*	$NetBSD: gus.c,v 1.51 1998/01/25 23:48:06 mycroft Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ken Hornstein and John Kohl.
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

/*
 *
 * TODO:
 *	. figure out why mixer activity while sound is playing causes problems
 *	  (phantom interrupts?)
 *	. figure out a better deinterleave strategy that avoids sucking up
 *	  CPU, memory and cache bandwidth.  (Maybe a special encoding?
 *	  Maybe use the double-speed sampling/hardware deinterleave trick
 *	  from the GUS SDK?)  A 486/33 isn't quite fast enough to keep
 *	  up with 44.1kHz 16-bit stereo output without some drop-outs.
 *	. use CS4231 for 16-bit sampling, for a-law and mu-law playback.
 *	. actually test full-duplex sampling(recording) and playback.
 */

/*
 * Gravis UltraSound driver
 *
 * For more detailed information, see the GUS developers' kit
 * available on the net at:
 *
 * ftp://freedom.nmsu.edu/pub/ultrasound/gravis/util/
 *	gusdkXXX.zip (developers' kit--get rev 2.22 or later)
 *		See ultrawrd.doc inside--it's MS Word (ick), but it's the bible
 *
 */

/*
 * The GUS Max has a slightly strange set of connections between the CS4231
 * and the GF1 and the DMA interconnects.  It's set up so that the CS4231 can
 * be playing while the GF1 is loading patches from the system.
 *
 * Here's a recreation of the DMA interconnect diagram:
 *
 *       GF1
 *   +---------+				 digital
 *   |         |  record			 ASIC
 *   |         |--------------+
 *   |         |              |		       +--------+
 *   |         | play (dram)  |      +----+    |	|
 *   |         |--------------(------|-\  |    |   +-+  |
 *   +---------+              |      |  >-|----|---|C|--|------  dma chan 1
 *                            |  +---|-/  |    |   +-+	|
 *                            |  |   +----+    |    |   |
 *                            |	 |   +----+    |    |   |
 *   +---------+        +-+   +--(---|-\  |    |    |   |
 *   |         | play   |8|      |   |  >-|----|----+---|------  dma chan 2
 *   | ---C----|--------|/|------(---|-/  |    |        |
 *   |    ^    |record  |1|      |   +----+    |	|
 *   |    |    |   /----|6|------+	       +--------+
 *   | ---+----|--/     +-+
 *   +---------+
 *     CS4231	8-to-16 bit bus conversion, if needed
 *
 *
 * "C" is an optional combiner.
 *
 */

/*
 * Software state of a single "voice" on the GUS
 */
struct gus_voice {

	/*
	 * Various control bits
	 */

	unsigned char voccntl;	/* State of voice control register */
	unsigned char volcntl;	/* State of volume control register */
	unsigned char pan_pos;	/* Position of volume panning (4 bits) */
	int rate;		/* Sample rate of voice being played back */

	/*
	 * Address of the voice data into the GUS's DRAM.  20 bits each
	 */

	u_long start_addr;	/* Starting address of voice data loop area */
	u_long end_addr;	/* Ending address of voice data loop */
	u_long current_addr;	/* Beginning address of voice data
				   (start playing here) */

	/*
	 * linear volume values for the GUS's volume ramp.  0-511 (9 bits).
	 * These values must be translated into the logarithmic values using
	 * gus_log_volumes[]
	 */

	int start_volume;	/* Starting position of volume ramp */
	int current_volume;	/* Current position of volume on volume ramp */
	int end_volume;		/* Ending position of volume on volume ramp */
};

/*
 * Software state of GUS
 */
struct gus_softc {
	struct device sc_dev;		/* base device */
	struct device *sc_isa;		/* pointer to ISA parent */
	void *sc_ih;			/* interrupt vector */
	struct timeout sc_dma_tmo;
	bus_space_tag_t sc_iot;		/* tag */
	bus_space_handle_t sc_ioh1;	/* handle */
	bus_space_handle_t sc_ioh2;	/* handle */
	bus_space_handle_t sc_ioh3;	/* ICS2101 handle */
	bus_space_handle_t sc_ioh4;	/* MIDI handle */

	int sc_iobase;			/* I/O base address */
	int sc_irq;			/* IRQ used */
	int sc_drq;			/* DMA channel for play */
	int sc_recdrq;			/* DMA channel for recording */

	int sc_flags;			/* Various flags about the GUS */
#define GUS_MIXER_INSTALLED	0x01	/* An ICS mixer is installed */
#define GUS_LOCKED		0x02	/* GUS is busy doing multi-phase DMA */
#define GUS_CODEC_INSTALLED	0x04	/* CS4231 installed/MAX */
#define GUS_PLAYING		0x08	/* GUS is playing a voice */
#define GUS_DMAOUT_ACTIVE	0x10	/* GUS is busy doing audio DMA */
#define GUS_DMAIN_ACTIVE	0x20	/* GUS is busy sampling  */
#define GUS_OPEN		0x100	/* GUS is open */
	int sc_dsize;			/* Size of GUS DRAM */
	int sc_voices;			/* Number of active voices */
	u_char sc_revision;		/* Board revision of GUS */
	u_char sc_mixcontrol;		/* Value of GUS_MIX_CONTROL register */

	u_long sc_orate;		/* Output sampling rate */
	u_long sc_irate;		/* Input sampling rate */

	int sc_encoding;		/* Current data encoding type */
	int sc_precision;		/* # of bits of precision */
	int sc_channels;		/* Number of active channels */
	int sc_blocksize;		/* Current blocksize */
	int sc_chanblocksize;		/* Current blocksize for each in-use
					   channel */
	short sc_nbufs;			/* how many on-GUS bufs per-channel */
	short sc_bufcnt;		/* how many need to be played */
	void *sc_deintr_buf;		/* deinterleave buffer for stereo */

	int sc_ogain;			/* Output gain control */
	u_char sc_out_port;		/* Current out port (generic only) */
	u_char sc_in_port;		/* keep track of it when no codec */

	void (*sc_dmaoutintr)(void *);	/* DMA completion intr handler */
	void *sc_outarg;		/* argument for sc_dmaoutintr() */
	u_char *sc_dmaoutaddr;		/* for isadma_done */
	u_long sc_gusaddr;		/* where did we just put it? */
	int sc_dmaoutcnt;		/* for isadma_done */

	void (*sc_dmainintr)(void *);	/* DMA completion intr handler */
	void *sc_inarg;			/* argument for sc_dmaoutintr() */
	u_char *sc_dmainaddr;		/* for isadma_done */
	int sc_dmaincnt;		/* for isadma_done */

	struct stereo_dma_intr {
		void (*intr)(void *);
		void *arg;
		u_char *buffer;
		u_long dmabuf;
		int size;
		int flags;
	} sc_stereo;

	/*
	 * State information for linear audio layer
	 */

	int sc_dmabuf;			/* Which ring buffer we're DMA'ing to */
	int sc_playbuf;			/* Which ring buffer we're playing */

	/*
	 * Voice information array.  All voice-specific information is stored
	 * here
	 */

	struct gus_voice sc_voc[32];	/* Voice data for each voice */
	union {
		struct ics2101_softc sc_mixer_u;
		struct ad1848_softc sc_codec_u;
	} u;
#define sc_mixer u.sc_mixer_u
#define sc_codec u.sc_codec_u
};

struct ics2101_volume {
	u_char left;
	u_char right;
};

#define HAS_CODEC(sc) ((sc)->sc_flags & GUS_CODEC_INSTALLED)
#define HAS_MIXER(sc) ((sc)->sc_flags & GUS_MIXER_INSTALLED)

/*
 * Mixer devices for ICS2101
 */
/* MIC IN mute, line in mute, line out mute are first since they can be done
   even if no ICS mixer. */
#define GUSICS_MIC_IN_MUTE		0
#define GUSICS_LINE_IN_MUTE		1
#define GUSICS_MASTER_MUTE		2
#define GUSICS_CD_MUTE			3
#define GUSICS_DAC_MUTE			4
#define GUSICS_MIC_IN_LVL		5
#define GUSICS_LINE_IN_LVL		6
#define GUSICS_CD_LVL			7
#define GUSICS_DAC_LVL			8
#define GUSICS_MASTER_LVL		9

#define GUSICS_RECORD_SOURCE		10

/* Classes */
#define GUSICS_INPUT_CLASS		11
#define GUSICS_OUTPUT_CLASS		12
#define GUSICS_RECORD_CLASS		13

/*
 * Mixer & MUX devices for CS4231
 */
#define GUSMAX_MONO_LVL			0 /* mic input to MUX;
					     also mono mixer input */
#define GUSMAX_DAC_LVL			1 /* input to MUX; also mixer input */
#define GUSMAX_LINE_IN_LVL		2 /* input to MUX; also mixer input */
#define GUSMAX_CD_LVL			3 /* mixer input only */
#define GUSMAX_MONITOR_LVL		4 /* digital mix (?) */
#define GUSMAX_OUT_LVL			5 /* output level. (?) */
#define GUSMAX_SPEAKER_LVL		6 /* pseudo-device for mute */
#define GUSMAX_LINE_IN_MUTE		7 /* pre-mixer */
#define GUSMAX_DAC_MUTE			8 /* pre-mixer */
#define GUSMAX_CD_MUTE			9 /* pre-mixer */
#define GUSMAX_MONO_MUTE		10 /* pre-mixer--microphone/mono */
#define GUSMAX_MONITOR_MUTE		11 /* post-mixer level/mute */
#define GUSMAX_SPEAKER_MUTE		12 /* speaker mute */

#define GUSMAX_REC_LVL			13 /* post-MUX gain */

#define GUSMAX_RECORD_SOURCE		14

/* Classes */
#define GUSMAX_INPUT_CLASS		15
#define GUSMAX_RECORD_CLASS		16
#define GUSMAX_MONITOR_CLASS		17
#define GUSMAX_OUTPUT_CLASS		18

#ifdef AUDIO_DEBUG
#define GUSPLAYDEBUG	/*XXX*/
#define DPRINTF(x)	if (gusdebug) printf x
#define DMAPRINTF(x)	if (gusdmadebug) printf x
extern int	gusdebug;
extern int	gusdmadebug;
#else
#define DPRINTF(x)
#define DMAPRINTF(x)
#endif
extern int	gus_dostereo;

#define NDMARECS 2048
#ifdef GUSPLAYDEBUG
extern int	gusstats;
struct dma_record {
    struct timeval tv;
    u_long gusaddr;
    caddr_t bsdaddr;
    u_short count;
    u_char channel;
    u_char direction;
};

extern struct dma_record dmarecords[NDMARECS];

extern int dmarecord_index;
#endif

/*
 * local routines
 */

int	gusopen(void *, int);
void	gusclose(void *);
void	gusmax_close(void *);
int	gusintr(void *);
int	gus_set_in_gain(caddr_t, u_int, u_char);
int	gus_get_in_gain(caddr_t);
int	gus_get_out_gain(caddr_t);
int	gus_set_params(void *, int, int, struct audio_params *, struct audio_params *);
int	gusmax_set_params(void *, int, int, struct audio_params *, struct audio_params *);
int	gus_round_blocksize(void *, int);
int	gus_commit_settings(void *);
int	gus_dma_output(void *, void *, int, void (*)(void *), void *);
int	gus_dma_input(void *, void *, int, void (*)(void *), void *);
int	gus_halt_out_dma(void *);
int	gus_halt_in_dma(void *);
int	gus_speaker_ctl(void *, int);
int	gusmaxopen(void *, int);
int	gusmax_round_blocksize(void *, int);
int	gusmax_commit_settings(void *);
int	gusmax_dma_output(void *, void *, int, void (*)(void *), void *);
int	gusmax_dma_input(void *, void *, int, void (*)(void *), void *);
int	gusmax_halt_out_dma(void *);
int	gusmax_halt_in_dma(void *);

void	gus_deinterleave(struct gus_softc *, void *, int);

int	gus_mic_ctl(void *, int);
int	gus_linein_ctl(void *, int);
int		gus_test_iobase(bus_space_tag_t, int);
void	guspoke(bus_space_tag_t, bus_space_handle_t, long, u_char);
void	gusdmaout(struct gus_softc *, int, u_long, caddr_t, int);
int	gus_init_cs4231(struct gus_softc *);
void	gus_init_ics2101(struct gus_softc *);

void	gus_set_chan_addrs(struct gus_softc *);
void	gusreset(struct gus_softc *, int);
void	gus_set_voices(struct gus_softc *, int);
void	gus_set_volume(struct gus_softc *, int, int);
void	gus_set_samprate(struct gus_softc *, int, int);
void	gus_set_recrate(struct gus_softc *, u_long);
void	gus_start_voice(struct gus_softc *, int, int);
void	gus_stop_voice(struct gus_softc *, int, int);
void	gus_set_endaddr(struct gus_softc *, int, u_long);
#ifdef GUSPLAYDEBUG
void	gus_set_curaddr(struct gus_softc *, int, u_long);
u_long	gus_get_curaddr(struct gus_softc *, int);
#endif
int	gus_dmaout_intr(struct gus_softc *);
void	gus_dmaout_dointr(struct gus_softc *);
void	gus_dmaout_timeout(void *);
int	gus_dmain_intr(struct gus_softc *);
int	gus_voice_intr(struct gus_softc *);
void	gus_start_playing(struct gus_softc *, int);
int	gus_continue_playing(struct gus_softc *, int);
u_char guspeek(bus_space_tag_t, bus_space_handle_t, u_long);
u_long convert_to_16bit(u_long);
int	gus_mixer_set_port(void *, mixer_ctrl_t *);
int	gus_mixer_get_port(void *, mixer_ctrl_t *);
int	gusmax_mixer_set_port(void *, mixer_ctrl_t *);
int	gusmax_mixer_get_port(void *, mixer_ctrl_t *);
int	gus_mixer_query_devinfo(void *, mixer_devinfo_t *);
int	gusmax_mixer_query_devinfo(void *, mixer_devinfo_t *);
void   *gus_malloc(void *, int, size_t, int, int);
void	gus_free(void *, void *, int);
size_t	gus_round(void *, int, size_t);

void	gusics_master_mute(struct ics2101_softc *, int);
void	gusics_dac_mute(struct ics2101_softc *, int);
void	gusics_mic_mute(struct ics2101_softc *, int);
void	gusics_linein_mute(struct ics2101_softc *, int);
void	gusics_cd_mute(struct ics2101_softc *, int);

void	stereo_dmaintr(void *);

extern const int gus_irq_map[];
extern const int gus_drq_map[];
extern const int gus_base_addrs[];
extern const int gus_addrs;

#define SELECT_GUS_REG(iot,ioh1,x) bus_space_write_1(iot,ioh1,GUS_REG_SELECT,x)
#define ADDR_HIGH(x) (unsigned int) ((x >> 7L) & 0x1fffL)
#define ADDR_LOW(x) (unsigned int) ((x & 0x7fL) << 9L)

#define GUS_MIN_VOICES 14	/* Minimum possible number of voices */
#define GUS_MAX_VOICES 32	/* Maximum possible number of voices */
#define GUS_VOICE_LEFT 0	/* Voice used for left (and mono) playback */
#define GUS_VOICE_RIGHT 1	/* Voice used for right playback */
#define GUS_MEM_OFFSET 32	/* Offset into GUS memory to begin of buffer */
#define GUS_BUFFER_MULTIPLE 1024	/* Audio buffers are multiples of this */
#define	GUS_MEM_FOR_BUFFERS	131072	/* use this many bytes on-GUS */
#define	GUS_LEFT_RIGHT_OFFSET	(sc->sc_nbufs * sc->sc_chanblocksize + GUS_MEM_OFFSET)

#define GUS_PREC_BYTES (sc->sc_precision >> 3) /* precision to bytes */

/* splgus() must be splaudio() */

#define splgus splaudio

extern const struct audio_hw_if gus_hw_if;

#define FLIP_REV	5		/* This rev has flipped mixer chans */

void gus_subattach(struct gus_softc *, struct isa_attach_args *);
