// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *     and (c) 1999 Steve Ratcliffe <steve@parabola.demon.co.uk>
 *  Copyright (C) 1999-2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Emu8000 synth plug-in routine
 */

#include "emu8000_local.h"
#include <linux/init.h>
#include <linux/module.h>
#include <sound/initval.h>

MODULE_AUTHOR("Takashi Iwai, Steve Ratcliffe");
MODULE_DESCRIPTION("Emu8000 synth plug-in routine");
MODULE_LICENSE("GPL");

/*----------------------------------------------------------------*/

/*
 * create a new hardware dependent device for Emu8000
 */
static int snd_emu8000_probe(struct device *_dev)
{
	struct snd_seq_device *dev = to_seq_dev(_dev);
	struct snd_emu8000 *hw;
	struct snd_emux *emu;

	hw = *(struct snd_emu8000**)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (hw == NULL)
		return -EINVAL;

	if (hw->emu)
		return -EBUSY; /* already exists..? */

	if (snd_emux_new(&emu) < 0)
		return -ENOMEM;

	hw->emu = emu;
	snd_emu8000_ops_setup(hw);

	emu->hw = hw;
	emu->max_voices = EMU8000_DRAM_VOICES;
	emu->num_ports = hw->seq_ports;

	if (hw->memhdr) {
		dev_err(hw->card->dev, "memhdr is already initialized!?\n");
		snd_util_memhdr_free(hw->memhdr);
	}
	hw->memhdr = snd_util_memhdr_new(hw->mem_size);
	if (hw->memhdr == NULL) {
		snd_emux_free(emu);
		hw->emu = NULL;
		return -ENOMEM;
	}

	emu->memhdr = hw->memhdr;
	emu->midi_ports = hw->seq_ports < 2 ? hw->seq_ports : 2; /* number of virmidi ports */
	emu->midi_devidx = 1;
	emu->linear_panning = 1;
	emu->hwdep_idx = 2; /* FIXED */

	if (snd_emux_register(emu, dev->card, hw->index, "Emu8000") < 0) {
		snd_emux_free(emu);
		snd_util_memhdr_free(hw->memhdr);
		hw->emu = NULL;
		hw->memhdr = NULL;
		return -ENOMEM;
	}

	if (hw->mem_size > 0)
		snd_emu8000_pcm_new(dev->card, hw, 1);

	dev->driver_data = hw;

	return 0;
}


/*
 * free all resources
 */
static int snd_emu8000_remove(struct device *_dev)
{
	struct snd_seq_device *dev = to_seq_dev(_dev);
	struct snd_emu8000 *hw;

	if (dev->driver_data == NULL)
		return 0; /* no synth was allocated actually */

	hw = dev->driver_data;
	if (hw->pcm)
		snd_device_free(dev->card, hw->pcm);
	snd_emux_free(hw->emu);
	snd_util_memhdr_free(hw->memhdr);
	hw->emu = NULL;
	hw->memhdr = NULL;
	return 0;
}

/*
 *  INIT part
 */

static struct snd_seq_driver emu8000_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.probe = snd_emu8000_probe,
		.remove = snd_emu8000_remove,
	},
	.id = SNDRV_SEQ_DEV_ID_EMU8000,
	.argsize = sizeof(struct snd_emu8000 *),
};

module_snd_seq_driver(emu8000_driver);
