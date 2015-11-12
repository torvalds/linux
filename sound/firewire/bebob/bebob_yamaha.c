/*
 * bebob_yamaha.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./bebob.h"

/*
 * NOTE:
 * Yamaha GO44 is not designed to be used as stand-alone mixer. So any streams
 * must be accompanied. If changing the state, a LED on the device starts to
 * blink and its sync status is false. In this state, the device sounds nothing
 * even if streaming. To start streaming at the current sampling rate is only
 * way to revocer this state. GO46 is better for stand-alone mixer.
 *
 * Both of them have a capability to change its sampling rate up to 192.0kHz.
 * At 192.0kHz, the device reports 4 PCM-in, 1 MIDI-in, 6 PCM-out, 1 MIDI-out.
 * But Yamaha's driver reduce 2 PCM-in, 1 MIDI-in, 2 PCM-out, 1 MIDI-out to use
 * 'Extended Stream Format Information Command - Single Request' in 'Additional
 * AVC commands' defined by BridgeCo.
 * This ALSA driver don't do this because a bit tiresome. Then isochronous
 * streaming with many asynchronous transactions brings sounds with noises.
 * Unfortunately current 'ffado-mixer' generated many asynchronous transaction
 * to observe device's state, mainly check cmp connection and signal format. I
 * reccomend users to close ffado-mixer at 192.0kHz if mixer is needless.
 */

static enum snd_bebob_clock_type clk_src_types[] = {
	SND_BEBOB_CLOCK_TYPE_INTERNAL,
	SND_BEBOB_CLOCK_TYPE_EXTERNAL,	/* S/PDIF */
};
static int
clk_src_get(struct snd_bebob *bebob, unsigned int *id)
{
	int err;

	err = avc_audio_get_selector(bebob->unit, 0, 4, id);
	if (err < 0)
		return err;

	if (*id >= ARRAY_SIZE(clk_src_types))
		return -EIO;

	return 0;
}
static const struct snd_bebob_clock_spec clock_spec = {
	.num	= ARRAY_SIZE(clk_src_types),
	.types	= clk_src_types,
	.get	= &clk_src_get,
};
static const struct snd_bebob_rate_spec rate_spec = {
	.get	= &snd_bebob_stream_get_rate,
	.set	= &snd_bebob_stream_set_rate,
};
const struct snd_bebob_spec yamaha_go_spec = {
	.clock	= &clock_spec,
	.rate	= &rate_spec,
	.meter	= NULL
};
