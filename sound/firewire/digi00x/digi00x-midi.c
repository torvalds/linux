/*
 * digi00x-midi.h - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

static int midi_open(struct snd_rawmidi_substream *substream)
{
	struct snd_dg00x *dg00x = substream->rmidi->private_data;
	int err;

	err = snd_dg00x_stream_lock_try(dg00x);
	if (err < 0)
		return err;

	mutex_lock(&dg00x->mutex);
	dg00x->substreams_counter++;
	err = snd_dg00x_stream_start_duplex(dg00x, 0);
	mutex_unlock(&dg00x->mutex);
	if (err < 0)
		snd_dg00x_stream_lock_release(dg00x);

	return err;
}

static int midi_close(struct snd_rawmidi_substream *substream)
{
	struct snd_dg00x *dg00x = substream->rmidi->private_data;

	mutex_lock(&dg00x->mutex);
	dg00x->substreams_counter--;
	snd_dg00x_stream_stop_duplex(dg00x);
	mutex_unlock(&dg00x->mutex);

	snd_dg00x_stream_lock_release(dg00x);
	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_dg00x *dg00x = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dg00x->lock, flags);

	if (up)
		amdtp_dot_midi_trigger(&dg00x->tx_stream,
				       substrm->number, substrm);
	else
		amdtp_dot_midi_trigger(&dg00x->tx_stream,
				       substrm->number, NULL);

	spin_unlock_irqrestore(&dg00x->lock, flags);
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_dg00x *dg00x = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dg00x->lock, flags);

	if (up)
		amdtp_dot_midi_trigger(&dg00x->rx_stream,
				       substrm->number, substrm);
	else
		amdtp_dot_midi_trigger(&dg00x->rx_stream,
				       substrm->number, NULL);

	spin_unlock_irqrestore(&dg00x->lock, flags);
}

static struct snd_rawmidi_ops midi_capture_ops = {
	.open		= midi_open,
	.close		= midi_close,
	.trigger	= midi_capture_trigger,
};

static struct snd_rawmidi_ops midi_playback_ops = {
	.open		= midi_open,
	.close		= midi_close,
	.trigger	= midi_playback_trigger,
};

static void set_midi_substream_names(struct snd_dg00x *dg00x,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	list_for_each_entry(subs, &str->substreams, list) {
		snprintf(subs->name, sizeof(subs->name),
			 "%s MIDI %d",
			 dg00x->card->shortname, subs->number + 1);
	}
}

int snd_dg00x_create_midi_devices(struct snd_dg00x *dg00x)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	int err;

	err = snd_rawmidi_new(dg00x->card, dg00x->card->driver, 0,
			      DOT_MIDI_OUT_PORTS, DOT_MIDI_IN_PORTS, &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", dg00x->card->shortname);
	rmidi->private_data = dg00x;

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			    &midi_capture_ops);
	str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];
	set_midi_substream_names(dg00x, str);

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &midi_playback_ops);
	str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];
	set_midi_substream_names(dg00x, str);

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
