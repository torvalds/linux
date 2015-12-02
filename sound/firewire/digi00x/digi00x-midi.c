/*
 * digi00x-midi.h - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

static int midi_phys_open(struct snd_rawmidi_substream *substream)
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

static int midi_phys_close(struct snd_rawmidi_substream *substream)
{
	struct snd_dg00x *dg00x = substream->rmidi->private_data;

	mutex_lock(&dg00x->mutex);
	dg00x->substreams_counter--;
	snd_dg00x_stream_stop_duplex(dg00x);
	mutex_unlock(&dg00x->mutex);

	snd_dg00x_stream_lock_release(dg00x);
	return 0;
}

static void midi_phys_capture_trigger(struct snd_rawmidi_substream *substream,
				      int up)
{
	struct snd_dg00x *dg00x = substream->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dg00x->lock, flags);

	if (up)
		amdtp_dot_midi_trigger(&dg00x->tx_stream, substream->number,
				       substream);
	else
		amdtp_dot_midi_trigger(&dg00x->tx_stream, substream->number,
				       NULL);

	spin_unlock_irqrestore(&dg00x->lock, flags);
}

static void midi_phys_playback_trigger(struct snd_rawmidi_substream *substream,
				       int up)
{
	struct snd_dg00x *dg00x = substream->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dg00x->lock, flags);

	if (up)
		amdtp_dot_midi_trigger(&dg00x->rx_stream, substream->number,
				       substream);
	else
		amdtp_dot_midi_trigger(&dg00x->rx_stream, substream->number,
				       NULL);

	spin_unlock_irqrestore(&dg00x->lock, flags);
}

static struct snd_rawmidi_ops midi_phys_capture_ops = {
	.open		= midi_phys_open,
	.close		= midi_phys_close,
	.trigger	= midi_phys_capture_trigger,
};

static struct snd_rawmidi_ops midi_phys_playback_ops = {
	.open		= midi_phys_open,
	.close		= midi_phys_close,
	.trigger	= midi_phys_playback_trigger,
};

static int midi_ctl_open(struct snd_rawmidi_substream *substream)
{
	/* Do nothing. */
	return 0;
}

static int midi_ctl_capture_close(struct snd_rawmidi_substream *substream)
{
	/* Do nothing. */
	return 0;
}

static int midi_ctl_playback_close(struct snd_rawmidi_substream *substream)
{
	struct snd_dg00x *dg00x = substream->rmidi->private_data;

	snd_fw_async_midi_port_finish(&dg00x->out_control);

	return 0;
}

static void midi_ctl_capture_trigger(struct snd_rawmidi_substream *substream,
				     int up)
{
	struct snd_dg00x *dg00x = substream->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dg00x->lock, flags);

	if (up)
		dg00x->in_control = substream;
	else
		dg00x->in_control = NULL;

	spin_unlock_irqrestore(&dg00x->lock, flags);
}

static void midi_ctl_playback_trigger(struct snd_rawmidi_substream *substream,
				      int up)
{
	struct snd_dg00x *dg00x = substream->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dg00x->lock, flags);

	if (up)
		snd_fw_async_midi_port_run(&dg00x->out_control, substream);

	spin_unlock_irqrestore(&dg00x->lock, flags);
}

static struct snd_rawmidi_ops midi_ctl_capture_ops = {
	.open		= midi_ctl_open,
	.close		= midi_ctl_capture_close,
	.trigger	= midi_ctl_capture_trigger,
};

static struct snd_rawmidi_ops midi_ctl_playback_ops = {
	.open		= midi_ctl_open,
	.close		= midi_ctl_playback_close,
	.trigger	= midi_ctl_playback_trigger,
};

static void set_midi_substream_names(struct snd_dg00x *dg00x,
				     struct snd_rawmidi_str *str,
				     bool is_ctl)
{
	struct snd_rawmidi_substream *subs;

	list_for_each_entry(subs, &str->substreams, list) {
		if (!is_ctl)
			snprintf(subs->name, sizeof(subs->name),
				 "%s MIDI %d",
				 dg00x->card->shortname, subs->number + 1);
		else
			/* This port is for asynchronous transaction. */
			snprintf(subs->name, sizeof(subs->name),
				 "%s control",
				 dg00x->card->shortname);
	}
}

int snd_dg00x_create_midi_devices(struct snd_dg00x *dg00x)
{
	struct snd_rawmidi *rmidi[2];
	struct snd_rawmidi_str *str;
	unsigned int i;
	int err;

	/* Add physical midi ports. */
	err = snd_rawmidi_new(dg00x->card, dg00x->card->driver, 0,
			DOT_MIDI_OUT_PORTS, DOT_MIDI_IN_PORTS, &rmidi[0]);
	if (err < 0)
		return err;

	snprintf(rmidi[0]->name, sizeof(rmidi[0]->name),
		 "%s MIDI", dg00x->card->shortname);

	snd_rawmidi_set_ops(rmidi[0], SNDRV_RAWMIDI_STREAM_INPUT,
			    &midi_phys_capture_ops);
	snd_rawmidi_set_ops(rmidi[0], SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &midi_phys_playback_ops);

	/* Add a pair of control midi ports. */
	err = snd_rawmidi_new(dg00x->card, dg00x->card->driver, 1,
			      1, 1, &rmidi[1]);
	if (err < 0)
		return err;

	snprintf(rmidi[1]->name, sizeof(rmidi[1]->name),
		 "%s control", dg00x->card->shortname);

	snd_rawmidi_set_ops(rmidi[1], SNDRV_RAWMIDI_STREAM_INPUT,
			    &midi_ctl_capture_ops);
	snd_rawmidi_set_ops(rmidi[1], SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &midi_ctl_playback_ops);

	for (i = 0; i < ARRAY_SIZE(rmidi); i++) {
		rmidi[i]->private_data = dg00x;

		rmidi[i]->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
		str = &rmidi[i]->streams[SNDRV_RAWMIDI_STREAM_INPUT];
		set_midi_substream_names(dg00x, str, i);

		rmidi[i]->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
		str = &rmidi[i]->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];
		set_midi_substream_names(dg00x, str, i);

		rmidi[i]->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;
	}

	return 0;
}
