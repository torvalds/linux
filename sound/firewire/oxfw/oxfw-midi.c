/*
 * oxfw_midi.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

static int midi_capture_open(struct snd_rawmidi_substream *substream)
{
	struct snd_oxfw *oxfw = substream->rmidi->private_data;
	int err;

	mutex_lock(&oxfw->mutex);

	oxfw->capture_substreams++;
	err = snd_oxfw_stream_start_simplex(oxfw, &oxfw->tx_stream, 0, 0);

	mutex_unlock(&oxfw->mutex);

	return err;
}

static int midi_playback_open(struct snd_rawmidi_substream *substream)
{
	struct snd_oxfw *oxfw = substream->rmidi->private_data;
	int err;

	mutex_lock(&oxfw->mutex);

	oxfw->playback_substreams++;
	err = snd_oxfw_stream_start_simplex(oxfw, &oxfw->rx_stream, 0, 0);

	mutex_unlock(&oxfw->mutex);

	return err;
}

static int midi_capture_close(struct snd_rawmidi_substream *substream)
{
	struct snd_oxfw *oxfw = substream->rmidi->private_data;

	mutex_lock(&oxfw->mutex);

	oxfw->capture_substreams--;
	snd_oxfw_stream_stop_simplex(oxfw, &oxfw->tx_stream);

	mutex_unlock(&oxfw->mutex);

	return 0;
}

static int midi_playback_close(struct snd_rawmidi_substream *substream)
{
	struct snd_oxfw *oxfw = substream->rmidi->private_data;

	mutex_lock(&oxfw->mutex);

	oxfw->playback_substreams--;
	snd_oxfw_stream_stop_simplex(oxfw, &oxfw->rx_stream);

	mutex_unlock(&oxfw->mutex);

	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_oxfw *oxfw = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&oxfw->lock, flags);

	if (up)
		amdtp_stream_midi_trigger(&oxfw->tx_stream,
					  substrm->number, substrm);
	else
		amdtp_stream_midi_trigger(&oxfw->tx_stream,
					  substrm->number, NULL);

	spin_unlock_irqrestore(&oxfw->lock, flags);
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_oxfw *oxfw = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&oxfw->lock, flags);

	if (up)
		amdtp_stream_midi_trigger(&oxfw->rx_stream,
					  substrm->number, substrm);
	else
		amdtp_stream_midi_trigger(&oxfw->rx_stream,
					  substrm->number, NULL);

	spin_unlock_irqrestore(&oxfw->lock, flags);
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

static void set_midi_substream_names(struct snd_oxfw *oxfw,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	list_for_each_entry(subs, &str->substreams, list) {
		snprintf(subs->name, sizeof(subs->name),
			 "%s MIDI %d",
			 oxfw->card->shortname, subs->number + 1);
	}
}

int snd_oxfw_create_midi(struct snd_oxfw *oxfw)
{
	struct snd_oxfw_stream_formation formation;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	u8 *format;
	int i, err;

	/* If its stream has MIDI conformant data channel, add one MIDI port */
	for (i = 0; i < SND_OXFW_STREAM_FORMAT_ENTRIES; i++) {
		format = oxfw->tx_stream_formats[i];
		if (format != NULL) {
			err = snd_oxfw_stream_parse_format(format, &formation);
			if (err >= 0 && formation.midi > 0)
				oxfw->midi_input_ports = 1;
		}

		format = oxfw->rx_stream_formats[i];
		if (format != NULL) {
			err = snd_oxfw_stream_parse_format(format, &formation);
			if (err >= 0 && formation.midi > 0)
				oxfw->midi_output_ports = 1;
		}
	}
	if ((oxfw->midi_input_ports == 0) && (oxfw->midi_output_ports == 0))
		return 0;

	/* create midi ports */
	err = snd_rawmidi_new(oxfw->card, oxfw->card->driver, 0,
			      oxfw->midi_output_ports, oxfw->midi_input_ports,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", oxfw->card->shortname);
	rmidi->private_data = oxfw;

	if (oxfw->midi_input_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
				    &midi_capture_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(oxfw, str);
	}

	if (oxfw->midi_output_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &midi_playback_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		set_midi_substream_names(oxfw, str);
	}

	if ((oxfw->midi_output_ports > 0) && (oxfw->midi_input_ports > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
