/*
 * ff-midi.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "ff.h"

static int midi_capture_open(struct snd_rawmidi_substream *substream)
{
	/* Do nothing. */
	return 0;
}

static int midi_playback_open(struct snd_rawmidi_substream *substream)
{
	struct snd_ff *ff = substream->rmidi->private_data;

	/* Initialize internal status. */
	ff->running_status[substream->number] = 0;
	ff->rx_midi_error[substream->number] = false;

	WRITE_ONCE(ff->rx_midi_substreams[substream->number], substream);

	return 0;
}

static int midi_capture_close(struct snd_rawmidi_substream *substream)
{
	/* Do nothing. */
	return 0;
}

static int midi_playback_close(struct snd_rawmidi_substream *substream)
{
	struct snd_ff *ff = substream->rmidi->private_data;

	cancel_work_sync(&ff->rx_midi_work[substream->number]);
	WRITE_ONCE(ff->rx_midi_substreams[substream->number], NULL);

	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substream,
				 int up)
{
	struct snd_ff *ff = substream->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&ff->lock, flags);

	if (up)
		WRITE_ONCE(ff->tx_midi_substreams[substream->number],
			   substream);
	else
		WRITE_ONCE(ff->tx_midi_substreams[substream->number], NULL);

	spin_unlock_irqrestore(&ff->lock, flags);
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substream,
				  int up)
{
	struct snd_ff *ff = substream->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&ff->lock, flags);

	if (up || !ff->rx_midi_error[substream->number])
		schedule_work(&ff->rx_midi_work[substream->number]);

	spin_unlock_irqrestore(&ff->lock, flags);
}

static void set_midi_substream_names(struct snd_rawmidi_str *stream,
				     const char *const name)
{
	struct snd_rawmidi_substream *substream;

	list_for_each_entry(substream, &stream->substreams, list) {
		snprintf(substream->name, sizeof(substream->name),
			 "%s MIDI %d", name, substream->number + 1);
	}
}

int snd_ff_create_midi_devices(struct snd_ff *ff)
{
	static const struct snd_rawmidi_ops midi_capture_ops = {
		.open		= midi_capture_open,
		.close		= midi_capture_close,
		.trigger	= midi_capture_trigger,
	};
	static const struct snd_rawmidi_ops midi_playback_ops = {
		.open		= midi_playback_open,
		.close		= midi_playback_close,
		.trigger	= midi_playback_trigger,
	};
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *stream;
	int err;

	err = snd_rawmidi_new(ff->card, ff->card->driver, 0,
			      ff->spec->midi_out_ports, ff->spec->midi_in_ports,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", ff->card->shortname);
	rmidi->private_data = ff;

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			    &midi_capture_ops);
	stream = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];
	set_midi_substream_names(stream, ff->card->shortname);

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &midi_playback_ops);
	stream = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];
	set_midi_substream_names(stream, ff->card->shortname);

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
