/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	ctatc.h
 *
 * @Brief
 * This file contains the definition of the device resource management object.
 *
 * @Author	Liu Chun
 * @Date 	Mar 28 2008
 *
 */

#ifndef CTATC_H
#define CTATC_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <sound/core.h>

#include "ctvmem.h"
#include "cthardware.h"
#include "ctresource.h"

enum CTALSADEVS {		/* Types of alsa devices */
	FRONT,
	SURROUND,
	CLFE,
	SIDE,
	IEC958,
	MIXER,
	NUM_CTALSADEVS		/* This should always be the last */
};

struct ct_atc_chip_sub_details {
	u16 subsys;
	const char *nm_model;
};

struct ct_atc_chip_details {
	u16 vendor;
	u16 device;
	const struct ct_atc_chip_sub_details *sub_details;
	const char *nm_card;
};

struct ct_atc;
struct ct_timer;
struct ct_timer_instance;

/* alsa pcm stream descriptor */
struct ct_atc_pcm {
	struct snd_pcm_substream *substream;
	void (*interrupt)(struct ct_atc_pcm *apcm);
	struct ct_timer_instance *timer;
	unsigned int started:1;

	/* Only mono and interleaved modes are supported now. */
	struct ct_vm_block *vm_block;
	void *src;		/* SRC for interacting with host memory */
	void **srccs;		/* SRCs for sample rate conversion */
	void **srcimps;		/* SRC Input Mappers */
	void **amixers;		/* AMIXERs for routing converted data */
	void *mono;		/* A SUM resource for mixing chs to one */
	unsigned char n_srcc;	/* Number of converting SRCs */
	unsigned char n_srcimp;	/* Number of SRC Input Mappers */
	unsigned char n_amixer;	/* Number of AMIXERs */
};

/* Chip resource management object */
struct ct_atc {
	struct pci_dev *pci;
	struct snd_card *card;
	unsigned int rsr; /* reference sample rate in Hz */
	unsigned int msr; /* master sample rate in rsr */
	unsigned int pll_rate; /* current rate of Phase Lock Loop */

	int chip_type;
	int model;
	const char *chip_name;
	const char *model_name;

	struct ct_vm *vm; /* device virtual memory manager for this card */
	int (*map_audio_buffer)(struct ct_atc *atc, struct ct_atc_pcm *apcm);
	void (*unmap_audio_buffer)(struct ct_atc *atc, struct ct_atc_pcm *apcm);
	unsigned long (*get_ptp_phys)(struct ct_atc *atc, int index);

	struct mutex atc_mutex;

	int (*pcm_playback_prepare)(struct ct_atc *atc,
				    struct ct_atc_pcm *apcm);
	int (*pcm_playback_start)(struct ct_atc *atc, struct ct_atc_pcm *apcm);
	int (*pcm_playback_stop)(struct ct_atc *atc, struct ct_atc_pcm *apcm);
	int (*pcm_playback_position)(struct ct_atc *atc,
				     struct ct_atc_pcm *apcm);
	int (*spdif_passthru_playback_prepare)(struct ct_atc *atc,
					       struct ct_atc_pcm *apcm);
	int (*pcm_capture_prepare)(struct ct_atc *atc, struct ct_atc_pcm *apcm);
	int (*pcm_capture_start)(struct ct_atc *atc, struct ct_atc_pcm *apcm);
	int (*pcm_capture_stop)(struct ct_atc *atc, struct ct_atc_pcm *apcm);
	int (*pcm_capture_position)(struct ct_atc *atc,
				    struct ct_atc_pcm *apcm);
	int (*pcm_release_resources)(struct ct_atc *atc,
				     struct ct_atc_pcm *apcm);
	int (*select_line_in)(struct ct_atc *atc);
	int (*select_mic_in)(struct ct_atc *atc);
	int (*select_digit_io)(struct ct_atc *atc);
	int (*line_front_unmute)(struct ct_atc *atc, unsigned char state);
	int (*line_surround_unmute)(struct ct_atc *atc, unsigned char state);
	int (*line_clfe_unmute)(struct ct_atc *atc, unsigned char state);
	int (*line_rear_unmute)(struct ct_atc *atc, unsigned char state);
	int (*line_in_unmute)(struct ct_atc *atc, unsigned char state);
	int (*mic_unmute)(struct ct_atc *atc, unsigned char state);
	int (*spdif_out_unmute)(struct ct_atc *atc, unsigned char state);
	int (*spdif_in_unmute)(struct ct_atc *atc, unsigned char state);
	int (*spdif_out_get_status)(struct ct_atc *atc, unsigned int *status);
	int (*spdif_out_set_status)(struct ct_atc *atc, unsigned int status);
	int (*spdif_out_passthru)(struct ct_atc *atc, unsigned char state);
	struct capabilities (*capabilities)(struct ct_atc *atc);
	int (*output_switch_get)(struct ct_atc *atc);
	int (*output_switch_put)(struct ct_atc *atc, int position);
	int (*mic_source_switch_get)(struct ct_atc *atc);
	int (*mic_source_switch_put)(struct ct_atc *atc, int position);

	/* Don't touch! Used for internal object. */
	void *rsc_mgrs[NUM_RSCTYP]; /* chip resource managers */
	void *mixer;		/* internal mixer object */
	void *hw;		/* chip specific hardware access object */
	void **daios;		/* digital audio io resources */
	void **pcm;		/* SUMs for collecting all pcm stream */
	void **srcs;		/* Sample Rate Converters for input signal */
	void **srcimps;		/* input mappers for SRCs */
	unsigned char n_daio;
	unsigned char n_src;
	unsigned char n_srcimp;
	unsigned char n_pcm;

	struct ct_timer *timer;

#ifdef CONFIG_PM_SLEEP
	int (*suspend)(struct ct_atc *atc);
	int (*resume)(struct ct_atc *atc);
#define NUM_PCMS (NUM_CTALSADEVS - 1)
	struct snd_pcm *pcms[NUM_PCMS];
#endif
};


int ct_atc_create(struct snd_card *card, struct pci_dev *pci,
		  unsigned int rsr, unsigned int msr, int chip_type,
		  unsigned int subsysid, struct ct_atc **ratc);
int ct_atc_create_alsa_devs(struct ct_atc *atc);

#endif /* CTATC_H */
