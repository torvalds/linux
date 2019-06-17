/*
 * bebob_midi.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "bebob.h"

static int midi_open(struct snd_rawmidi_substream *substream)
{
	struct snd_bebob *bebob = substream->rmidi->private_data;
	int err;

	err = snd_bebob_stream_lock_try(bebob);
	if (err < 0)
		return err;

	mutex_lock(&bebob->mutex);
	err = snd_bebob_stream_reserve_duplex(bebob, 0);
	if (err >= 0) {
		++bebob->substreams_counter;
		err = snd_bebob_stream_start_duplex(bebob);
	}
	mutex_unlock(&bebob->mutex);
	if (err < 0)
		snd_bebob_stream_lock_release(bebob);

	return err;
}

static int midi_close(struct snd_rawmidi_substream *substream)
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
	static const struct snd_rawmidi_ops capture_ops = {
		.open		= midi_open,
		.close		= midi_close,
		.trigger	= midi_capture_trigger,
	};
	static const struct snd_rawmidi_ops playback_ops = {
		.open		= midi_open,
		.close		= midi_close,
		.trigger	= midi_playback_trigger,
	};
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
				    &capture_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(bebob, str);
	}

	if (bebob->midi_output_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &playback_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		set_midi_substream_names(bebob, str);
	}

	if ((bebob->midi_output_ports > 0) && (bebob->midi_input_ports > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
