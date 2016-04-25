/*
 * bebob_midi.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "bebob.h"

static int midi_capture_open(struct snd_rawmidi_substream *substream)
{
	struct snd_bebob *bebob = substream->rmidi->private_data;
	int err;

	err = snd_bebob_stream_lock_try(bebob);
	if (err < 0)
		goto end;

	mutex_lock(&bebob->mutex);
	bebob->substreams_counter++;
	err = snd_bebob_stream_start_duplex(bebob, 0);
	mutex_unlock(&bebob->mutex);
	if (err < 0)
		snd_bebob_stream_lock_release(bebob);
end:
	return err;
}

static int midi_playback_open(struct snd_rawmidi_substream *substream)
{
	struct snd_bebob *bebob = substream->rmidi->private_data;
	int err;

	err = snd_bebob_stream_lock_try(bebob);
	if (err < 0)
		goto end;

	mutex_lock(&bebob->mutex);
	bebob->substreams_counter++;
	err = snd_bebob_stream_start_duplex(bebob, 0);
	mutex_unlock(&bebob->mutex);
	if (err < 0)
		snd_bebob_stream_lock_release(bebob);
end:
	return err;
}

static int midi_capture_close(struct snd_rawmidi_substream *substream)
{
	struct snd_bebob *bebob = substream->rmidi->private_data;

	mutex_lock(&bebob->mutex);
	bebob->substreams_counter--;
	snd_bebob_stream_stop_duplex(bebob);
	mutex_unlock(&bebob->mutex);

	snd_bebob_stream_lock_release(bebob);
	return 0;
}

static int midi_playback_close(struct snd_rawmidi_substream *substream)
{
	struct snd_bebob *bebob = substream->rmidi->private_data;

	mutex_lock(&bebob->mutex);
	bebob->substreams_counter--;
	snd_bebob_stream_stop_duplex(bebob);
	mutex_unlock(&bebob->mutex);

	snd_bebob_stream_lock_release(bebob);
	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_bebob *bebob = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&bebob->lock, flags);

	if (up)
		amdtp_am824_midi_trigger(&bebob->tx_stream,
					 substrm->number, substrm);
	else
		amdtp_am824_midi_trigger(&bebob->tx_stream,
					 substrm->number, NULL);

	spin_unlock_irqrestore(&bebob->lock, flags);
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_bebob *bebob = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&bebob->lock, flags);

	if (up)
		amdtp_am824_midi_trigger(&bebob->rx_stream,
					 substrm->number, substrm);
	else
		amdtp_am824_midi_trigger(&bebob->rx_stream,
					 substrm->number, NULL);

	spin_unlock_irqrestore(&bebob->lock, flags);
}

static struct snd_rawmidi_ops midi_capture_ops = {
	.open		= midi_capture_open,
	.close		= midi_capture_close,
	.trigger	= midi_capture_trigger,
};

static struct snd_rawmidi_ops midi_playback_ops = {
	.open		= midi_playback_open,
	.close		= midi_playback_close,
	.trigger	= midi_playback_trigger,
};

static void set_midi_substream_names(struct snd_bebob *bebob,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	list_for_each_entry(subs, &str->substreams, list) {
		snprintf(subs->name, sizeof(subs->name),
			 "%s MIDI %d",
			 bebob->card->shortname, subs->number + 1);
	}
}

int snd_bebob_create_midi_devices(struct snd_bebob *bebob)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	int err;

	/* create midi ports */
	err = snd_rawmidi_new(bebob->card, bebob->card->driver, 0,
			      bebob->midi_output_ports, bebob->midi_input_ports,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", bebob->card->shortname);
	rmidi->private_data = bebob;

	if (bebob->midi_input_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
				    &midi_capture_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(bebob, str);
	}

	if (bebob->midi_output_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &midi_playback_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		set_midi_substream_names(bebob, str);
	}

	if ((bebob->midi_output_ports > 0) && (bebob->midi_input_ports > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
