/*	$OpenBSD: cmpcivar.h,v 1.7 2010/10/08 14:01:07 jakemsr Exp $	*/
/*	$NetBSD: cmpcivar.h,v 1.9 2005/12/11 12:22:48 christos Exp $	*/

/*
 * Copyright (c) 2000, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Takuya SHIOZAKI <tshiozak@NetBSD.org> .
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by ITOH Yasufumi.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/* C-Media CMI8x38 Audio Chip Support */

#ifndef _DEV_PCI_CMPCIVAR_H_
#define _DEV_PCI_CMPCIVAR_H_


/*
 * DMA pool
 */
struct cmpci_dmanode {
	bus_dma_tag_t		cd_tag;
	int			cd_nsegs;
	bus_dma_segment_t	cd_segs[1];
	bus_dmamap_t		cd_map;
	caddr_t			cd_addr;
	size_t			cd_size;
	struct cmpci_dmanode	*cd_next;
};

typedef struct cmpci_dmanode *cmpci_dmapool_t;
#define KVADDR(dma)  ((void *)(dma)->cd_addr)
#define DMAADDR(dma) ((dma)->cd_map->dm_segs[0].ds_addr)


/*
 * Mixer device
 *
 * Note that cmpci_query_devinfo() is optimized depending on
 * the order of this.  Be careful if you change the values.
 */
#define CMPCI_DAC_VOL			0	/* inputs.dac */
#define CMPCI_FM_VOL			1	/* inputs.fmsynth */
#define CMPCI_CD_VOL			2	/* inputs.cd */
#define CMPCI_LINE_IN_VOL		3	/* inputs.line */
#define CMPCI_AUX_IN_VOL		4	/* inputs.aux */
#define CMPCI_MIC_VOL			5	/* inputs.mic */

#define CMPCI_DAC_MUTE			6	/* inputs.dac.mute */
#define CMPCI_FM_MUTE			7	/* inputs.fmsynth.mute */
#define CMPCI_CD_MUTE			8	/* inputs.cd.mute */
#define CMPCI_LINE_IN_MUTE		9	/* inputs.line.mute */
#define CMPCI_AUX_IN_MUTE		10	/* inputs.aux.mute */
#define CMPCI_MIC_MUTE			11	/* inputs.mic.mute */

#define CMPCI_MIC_PREAMP		12	/* inputs.mic.preamp */
#define CMPCI_PCSPEAKER			13	/* inputs.speaker */

#define CMPCI_RECORD_SOURCE		14	/* record.source */
#define CMPCI_MIC_RECVOL		15	/* record.mic */

#define CMPCI_PLAYBACK_MODE		16	/* playback.mode */
#define CMPCI_SPDIF_IN_SELECT		17	/* spdif.input */
#define CMPCI_SPDIF_IN_PHASE		18	/* spdif.input.phase */
#define CMPCI_SPDIF_LOOP		19	/* spdif.output */
#define CMPCI_SPDIF_OUT_PLAYBACK	20	/* spdif.output.playback */
#define CMPCI_SPDIF_OUT_VOLTAGE		21	/* spdif.output.voltage */
#define CMPCI_MONITOR_DAC		22	/* spdif.monitor */

#define CMPCI_MASTER_VOL		23	/* outputs.master */
#define CMPCI_REAR			24	/* outputs.rear */
#define CMPCI_INDIVIDUAL		25	/* outputs.rear.individual */
#define CMPCI_REVERSE			26	/* outputs.rear.reverse */
#define CMPCI_SURROUND			27	/* outputs.surround */

#define CMPCI_NDEVS			28

#define CMPCI_INPUT_CLASS		28
#define CMPCI_OUTPUT_CLASS		29
#define CMPCI_RECORD_CLASS		30
#define CMPCI_PLAYBACK_CLASS		31
#define CMPCI_SPDIF_CLASS		32

#define CmpciNspdif			"spdif"
#define CmpciCspdif			"spdif"
#define CmpciNspdin			"spdin"
#define CmpciNspdin1			"spdin1"
#define CmpciNspdin2			"spdin2"
#define CmpciNspdout			"spdout"
#define CmpciNplayback			"playback"
#define CmpciCplayback			"playback"
#define CmpciNlegacy			"legacy"
#define CmpciNvoltage			"voltage"
#define CmpciNphase			"phase"
#define CmpciNpositive			"positive"
#define CmpciNnegative			"negative"
#define CmpciNrear			"rear"
#define CmpciNindividual		"individual"
#define CmpciNreverse			"reverse"
#define CmpciNhigh_v			"5V"
#define CmpciNlow_v			"0.5V"
#define CmpciNsurround			"surround"

/* record.source bitmap (see cmpci_set_in_ports()) */
#define CMPCI_RECORD_SOURCE_MIC		CMPCI_SB16_MIXER_MIC_SRC    /* mic */
#define CMPCI_RECORD_SOURCE_CD		CMPCI_SB16_MIXER_CD_SRC_R   /* cd */
#define CMPCI_RECORD_SOURCE_LINE_IN	CMPCI_SB16_MIXER_LINE_SRC_R /* line */
#define CMPCI_RECORD_SOURCE_AUX_IN	(1 << 8)		    /* aux */
#define CMPCI_RECORD_SOURCE_WAVE	(1 << 9)		    /* wave */
#define CMPCI_RECORD_SOURCE_FM		CMPCI_SB16_MIXER_FM_SRC_R   /* fmsynth*/
#define CMPCI_RECORD_SOURCE_SPDIF	(1 << 10)		    /* spdif */

/* playback.mode */
#define CMPCI_PLAYBACK_MODE_WAVE	0		/* dac */
#define CMPCI_PLAYBACK_MODE_SPDIF	1		/* spdif */

/* spdif.input */
#define CMPCI_SPDIFIN_SPDIFIN2	0x01
#define CMPCI_SPDIFIN_SPDIFOUT	0x02
#define CMPCI_SPDIF_IN_SPDIN1	0			/* spdin1 */
#define CMPCI_SPDIF_IN_SPDIN2	CMPCI_SPDIFIN_SPDIFIN2	/* spdin2 */
#define CMPCI_SPDIF_IN_SPDOUT	(CMPCI_SPDIFIN_SPDIFIN2|CMPCI_SPDIFIN_SPDIFOUT)
							/* spdout */
/* spdif.input.phase */
#define CMPCI_SPDIF_IN_PHASE_POSITIVE	0		/* positive */
#define CMPCI_SPDIF_IN_PHASE_NEGATIVE	1		/* negative */

/* spdif.output */
#define CMPCI_SPDIF_LOOP_OFF		0		/* playback */
#define CMPCI_SPDIF_LOOP_ON		1		/* spdin */

/* spdif.output.playback */
#define CMPCI_SPDIF_OUT_PLAYBACK_WAVE	0		/* wave */
#define CMPCI_SPDIF_OUT_PLAYBACK_LEGACY	1		/* legacy */

/* spdif.output.voltage */
#define CMPCI_SPDIF_OUT_VOLTAGE_HIGH	0		/* 5V */
#define CMPCI_SPDIF_OUT_VOLTAGE_LOW	1		/* 0.5V */

/* spdif.monitor */
#define CMPCI_MONDAC_ENABLE	0x01
#define CMPCI_MONDAC_SPDOUT	0x02
#define CMPCI_MONITOR_DAC_OFF	 0			/* off */
#define CMPCI_MONITOR_DAC_SPDIN  CMPCI_MONDAC_ENABLE	/* spdin */
#define CMPCI_MONITOR_DAC_SPDOUT (CMPCI_MONDAC_ENABLE | CMPCI_MONDAC_SPDOUT)
							/* spdout */

/*
 * softc
 */

	/* each channel */
struct cmpci_channel {
	void		(*intr)(void *);
	void		*intr_arg;
	int		md_divide;
	int		bps;
	int		blksize;
	int		nblocks;
	int		swpos;
};

struct cmpci_softc {
	struct device		sc_dev;

	/* model/rev */
	uint32_t		sc_id;
	uint32_t		sc_class;
	uint32_t		sc_capable;
#define CMPCI_CAP_SPDIN			0x00000001
#define CMPCI_CAP_SPDOUT		0x00000002
#define CMPCI_CAP_SPDLOOP		0x00000004
#define CMPCI_CAP_SPDLEGACY		0x00000008
#define CMPCI_CAP_SPDIN_MONITOR		0x00000010
#define CMPCI_CAP_XSPDOUT		0x00000020
#define CMPCI_CAP_SPDOUT_VOLTAGE	0x00000040
#define CMPCI_CAP_SPDOUT_48K		0x00000080
#define CMPCI_CAP_SURROUND		0x00000100
#define CMPCI_CAP_REAR			0x00000200
#define CMPCI_CAP_INDIVIDUAL_REAR	0x00000400
#define CMPCI_CAP_REVERSE_FR		0x00000800
#define CMPCI_CAP_SPDIN_PHASE		0x00001000
#define CMPCI_CAP_2ND_SPDIN		0x00002000
#define CMPCI_CAP_4CH			0x00004000
#define CMPCI_CAP_6CH			0x00008000
#define CMPCI_CAP_8CH			0x00010000

#define CMPCI_CAP_CMI8338	(CMPCI_CAP_SPDIN | CMPCI_CAP_SPDOUT | \
				CMPCI_CAP_SPDLOOP | CMPCI_CAP_SPDLEGACY)

#define CMPCI_CAP_CMI8738	(CMPCI_CAP_CMI8338 | \
				CMPCI_CAP_SPDIN_MONITOR | \
				CMPCI_CAP_XSPDOUT | \
				CMPCI_CAP_SPDOUT_VOLTAGE | \
				CMPCI_CAP_SPDOUT_48K | CMPCI_CAP_SURROUND |\
				CMPCI_CAP_REAR | \
				CMPCI_CAP_INDIVIDUAL_REAR | \
				CMPCI_CAP_REVERSE_FR | \
				CMPCI_CAP_SPDIN_PHASE | \
				CMPCI_CAP_2ND_SPDIN /* XXX 6ch only */)
#define CMPCI_ISCAP(sc, name)	(sc->sc_capable & CMPCI_CAP_ ## name)

	/* I/O Base device */
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	bus_space_handle_t	sc_mpu_ioh;
	struct device		*sc_mpudev;

	/* intr handle */
	pci_intr_handle_t	*sc_ih;

	/* DMA */
	bus_dma_tag_t		sc_dmat;
	cmpci_dmapool_t		sc_dmap;

	/* each channel */
	struct cmpci_channel	sc_ch0, sc_ch1;

	/* which channel is used for playback */
	uint32_t		sc_play_channel;

	/* value of CMPCI_REG_MISC register */
	uint32_t		sc_reg_misc;

	/* chip version */
	uint32_t		sc_version;

	/* mixer */
	uint8_t			sc_gain[CMPCI_NDEVS][2];
#define CMPCI_LEFT	0
#define CMPCI_RIGHT	1
#define CMPCI_LR	0
	uint16_t		sc_in_mask;
};


#endif /* _DEV_PCI_CMPCIVAR_H_ */

/* end of file */
