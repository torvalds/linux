/*	$OpenBSD: esovar.h,v 1.7 2020/01/19 00:03:46 cheloha Exp $	*/
/*	$NetBSD: esovar.h,v 1.5 2004/05/25 21:38:11 kleink Exp $	*/

/*
 * Copyright (c) 1999, 2000, 2004 Klaus J. Klein
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
 * 3. The name of the author may not be used to endorse or promote products
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

#ifndef _DEV_PCI_ESOVAR_H_
#define _DEV_PCI_ESOVAR_H_

/*
 * Definitions exported for the purpose of sharing with attached
 * device drivers.
 */

/*
 * Mixer identifiers
 */
/* Identifiers that have a gain value associated with them */
#define	ESO_DAC_PLAY_VOL	0
#define	ESO_MIC_PLAY_VOL	1
#define	ESO_LINE_PLAY_VOL	2
#define	ESO_SYNTH_PLAY_VOL	3
#define	ESO_MONO_PLAY_VOL	4
#define	ESO_CD_PLAY_VOL		5	/* AuxA */
#define	ESO_AUXB_PLAY_VOL	6

#define	ESO_MASTER_VOL		7
#define	ESO_PCSPEAKER_VOL	8
#define	ESO_SPATIALIZER		9

#define	ESO_RECORD_VOL		10
#define	ESO_DAC_REC_VOL		11
#define	ESO_MIC_REC_VOL		12
#define	ESO_LINE_REC_VOL	13
#define	ESO_SYNTH_REC_VOL	14
#define	ESO_MONO_REC_VOL	15
#define	ESO_CD_REC_VOL		16
#define	ESO_AUXB_REC_VOL	17
/* Used to keep software state */
#define	ESO_NGAINDEVS		(ESO_AUXB_REC_VOL + 1)

/* Other, non-gain related mixer identifiers */
#define	ESO_RECORD_SOURCE	18
#define	ESO_MONOOUT_SOURCE	19
#define	ESO_MONOIN_BYPASS	20
#define	ESO_RECORD_MONITOR	21
#define	ESO_MIC_PREAMP		22
#define	ESO_SPATIALIZER_ENABLE	23
#define	ESO_MASTER_MUTE		24

/* Classes of the above */
#define	ESO_INPUT_CLASS		25
#define	ESO_OUTPUT_CLASS	26
#define	ESO_MICROPHONE_CLASS	27
#define	ESO_MONITOR_CLASS	28
#define	ESO_RECORD_CLASS	29
#define	ESO_MONOIN_CLASS	30


/*
 * Software state
 */
struct eso_softc {
	struct device		sc_dev;
	pci_intr_handle_t *	sc_ih;
	unsigned int		sc_revision;	/* PCI Revision ID */

	/* Optionally deferred configuration of Audio 1 DMAC I/O space */
	struct pci_attach_args	sc_pa;
	bus_size_t		sc_vcsize;	/* original size of mapping */

	/* DMA */
	bus_dma_tag_t		sc_dmat;
	struct eso_dma *	sc_dmas;

	/* I/O Base device */
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	/* Audio/FM device */
	bus_space_tag_t		sc_sb_iot;
	bus_space_handle_t	sc_sb_ioh;

	/* Audio 1 DMAC device */
	unsigned int		sc_dmac_configured;
	bus_addr_t		sc_dmac_addr;
	bus_space_tag_t		sc_dmac_iot;
	bus_space_handle_t	sc_dmac_ioh;

	/* MPU-401 device */
	bus_space_tag_t		sc_mpu_iot;
	bus_space_handle_t	sc_mpu_ioh;
	struct device *		sc_mpudev;

#if 0
	/* Game device */
	bus_space_tag_t		sc_game_iot;
	bus_space_handle_t	sc_game_ioh;
#endif

	/* MI audio interface: play/record interrupt callbacks and arguments */
	void			(*sc_pintr)(void *);
	void *			sc_parg;
	void			(*sc_rintr)(void *);
	void *			sc_rarg;

	/* Auto-initialize DMA transfer block drain timeouts, in milliseconds */
	int			sc_pdrain;
	int			sc_rdrain;

	/* Audio 2 state */
	uint8_t			sc_a2c2;	/* Audio 2 Control 2 */
	
	/* Mixer state */
	uint8_t			sc_gain[ESO_NGAINDEVS][2];
#define	ESO_LEFT		0
#define	ESO_RIGHT		1
	unsigned int		sc_recsrc;	/* record source selection */
	unsigned int		sc_monooutsrc;	/* MONO_OUT source selection */
	unsigned int		sc_monoinbypass;/* MONO_IN bypass enable */
	unsigned int		sc_recmon;	/* record monitor setting */
	unsigned int		sc_preamp;	/* microphone preamp */
	unsigned int		sc_spatializer;	/* spatializer enable */
	unsigned int		sc_mvmute;	/* master volume mute */
};

#endif /* !_DEV_PCI_ESOVAR_H_ */
