/*
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Routines for control of EMU10K1 WaveTable synth
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include "emu10k1_synth_local.h"
#include <linux/init.h>

MODULE_AUTHOR("Takashi Iwai");
MODULE_DESCRIPTION("Routines for control of EMU10K1 WaveTable synth");
MODULE_LICENSE("GPL");

/*
 * create a new hardware dependent device for Emu10k1
 */
static int snd_emu10k1_synth_new_device(snd_seq_device_t *dev)
{
	snd_emux_t *emu;
	emu10k1_t *hw;
	snd_emu10k1_synth_arg_t *arg;
	unsigned long flags;

	arg = SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (arg == NULL)
		return -EINVAL;

	if (arg->seq_ports <= 0)
		return 0; /* nothing */
	if (arg->max_voices < 1)
		arg->max_voices = 1;
	else if (arg->max_voices > 64)
		arg->max_voices = 64;

	if (snd_emux_new(&emu) < 0)
		return -ENOMEM;

	snd_emu10k1_ops_setup(emu);
	emu->hw = hw = arg->hwptr;
	emu->max_voices = arg->max_voices;
	emu->num_ports = arg->seq_ports;
	emu->pitch_shift = -501;
	emu->memhdr = hw->memhdr;
	emu->midi_ports = arg->seq_ports < 2 ? arg->seq_ports : 2; /* maximum two ports */
	emu->midi_devidx = hw->audigy ? 2 : 1; /* audigy has two external midis */
	emu->linear_panning = 0;
	emu->hwdep_idx = 2; /* FIXED */

	if (snd_emux_register(emu, dev->card, arg->index, "Emu10k1") < 0) {
		snd_emux_free(emu);
		emu->hw = NULL;
		return -ENOMEM;
	}

	spin_lock_irqsave(&hw->voice_lock, flags);
	hw->synth = emu;
	hw->get_synth_voice = snd_emu10k1_synth_get_voice;
	spin_unlock_irqrestore(&hw->voice_lock, flags);

	dev->driver_data = emu;

	return 0;
}

static int snd_emu10k1_synth_delete_device(snd_seq_device_t *dev)
{
	snd_emux_t *emu;
	emu10k1_t *hw;
	unsigned long flags;

	if (dev->driver_data == NULL)
		return 0; /* not registered actually */

	emu = dev->driver_data;

	hw = emu->hw;
	spin_lock_irqsave(&hw->voice_lock, flags);
	hw->synth = NULL;
	hw->get_synth_voice = NULL;
	spin_unlock_irqrestore(&hw->voice_lock, flags);

	snd_emux_free(emu);
	return 0;
}

/*
 *  INIT part
 */

static int __init alsa_emu10k1_synth_init(void)
{
	
	static snd_seq_dev_ops_t ops = {
		snd_emu10k1_synth_new_device,
		snd_emu10k1_synth_delete_device,
	};
	return snd_seq_device_register_driver(SNDRV_SEQ_DEV_ID_EMU10K1_SYNTH, &ops, sizeof(snd_emu10k1_synth_arg_t));
}

static void __exit alsa_emu10k1_synth_exit(void)
{
	snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_EMU10K1_SYNTH);
}

module_init(alsa_emu10k1_synth_init)
module_exit(alsa_emu10k1_synth_exit)
