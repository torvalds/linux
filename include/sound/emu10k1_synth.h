/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __EMU10K1_SYNTH_H
#define __EMU10K1_SYNTH_H
/*
 *  Defines for the Emu10k1 WaveTable synth
 *
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 */

#include <sound/emu10k1.h>
#include <sound/emux_synth.h>

/* sequencer device id */
#define SNDRV_SEQ_DEV_ID_EMU10K1_SYNTH	"emu10k1-synth"

/* argument for snd_seq_device_new */
struct snd_emu10k1_synth_arg {
	struct snd_emu10k1 *hwptr;	/* chip */
	int index;		/* sequencer client index */
	int seq_ports;		/* number of sequencer ports to be created */
	int max_voices;		/* maximum number of voices for wavetable */
};

#define EMU10K1_MAX_MEMSIZE	(32 * 1024 * 1024) /* 32MB */

#endif
