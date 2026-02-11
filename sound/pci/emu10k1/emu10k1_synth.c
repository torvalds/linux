// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Routines for control of EMU10K1 WaveTable synth
 */

#include "emu10k1_synth_local.h"
#include <linux/init.h>
#include <linux/module.h>

MODULE_AUTHOR("Takashi Iwai");
MODULE_DESCRIPTION("Routines for control of EMU10K1 WaveTable synth");
MODULE_LICENSE("GPL");

/*
 * create a new hardware dependent device for Emu10k1
 */
static int snd_emu10k1_synth_probe(struct snd_seq_device *dev)
{
	struct snd_emux *emux;
	struct snd_emu10k1 *hw;
	struct snd_emu10k1_synth_arg *arg;

	arg = SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (arg == NULL)
		return -EINVAL;

	if (arg->seq_ports <= 0)
		return 0; /* nothing */
	if (arg->max_voices < 1)
		arg->max_voices = 1;
	else if (arg->max_voices > 64)
		arg->max_voices = 64;

	if (snd_emux_new(&emux) < 0)
		return -ENOMEM;

	snd_emu10k1_ops_setup(emux);
	hw = arg->hwptr;
	emux->hw = hw;
	emux->max_voices = arg->max_voices;
	emux->num_ports = arg->seq_ports;
	emux->memhdr = hw->memhdr;
	/* maximum two ports */
	emux->midi_ports = arg->seq_ports < 2 ? arg->seq_ports : 2;
	/* audigy has two external midis */
	emux->midi_devidx = hw->audigy ? 2 : 1;
	emux->linear_panning = 0;
	emux->hwdep_idx = 2; /* FIXED */

	if (snd_emux_register(emux, dev->card, arg->index, "Emu10k1") < 0) {
		snd_emux_free(emux);
		return -ENOMEM;
	}

	guard(spinlock_irq)(&hw->voice_lock);
	hw->synth = emux;
	hw->get_synth_voice = snd_emu10k1_synth_get_voice;

	dev->driver_data = emux;

	return 0;
}

static void snd_emu10k1_synth_remove(struct snd_seq_device *dev)
{
	struct snd_emux *emux;
	struct snd_emu10k1 *hw;

	if (dev->driver_data == NULL)
		return; /* not registered actually */

	emux = dev->driver_data;

	hw = emux->hw;
	scoped_guard(spinlock_irq, &hw->voice_lock) {
		hw->synth = NULL;
		hw->get_synth_voice = NULL;
	}

	snd_emux_free(emux);
}

/*
 *  INIT part
 */

static struct snd_seq_driver emu10k1_synth_driver = {
	.probe = snd_emu10k1_synth_probe,
	.remove = snd_emu10k1_synth_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.id = SNDRV_SEQ_DEV_ID_EMU10K1_SYNTH,
	.argsize = sizeof(struct snd_emu10k1_synth_arg),
};

module_snd_seq_driver(emu10k1_synth_driver);
