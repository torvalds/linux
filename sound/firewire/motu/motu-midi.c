/*
 * motu-midi.h - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */
#include "motu.h"

static int midi_open(struct snd_rawmidi_substream *substream)
{
	struct snd_motu *motu = substream->rmidi->private_data;
	int err;

	err = snd_motu_stream_lock_try(motu);
	if (err < 0)
		return err;

	mutex_lock(&motu->mutex);

	err = snd_motu_stream_reserve_duplex(motu, 0);
	if (err >= 0) {
		++motu->substreams_counter;
		err = snd_motu_stream_start_duplex(motu);
	}

	mutex_unlock(&motu->mutex);

	if (err < 0)
		snd_motu_stream_lock_release(motu);

	return err;
}

static int midi_close(struct snd_rawmidi_substream *substream)
{
	struct snd_motu *motu = substream->rmidi->private_data;

	mutex_lock(&motu->mutex);

	--motu->substreams_counter;
	snd_motu_stream_stop_duplex(motu);
	snd_motu_stream_release_duplex(motu);

	mutex_unlock(&motu->mutex);

	snd_motu_stream_lock_release(motu);
	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_motu *motu = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&motu->lock, flags);

	if (up)
		amdtp_motu_midi_trigger(&motu->tx_stream, substrm->number,
					substrm);
	else
		amdtp_motu_midi_trigger(&motu->tx_stream, substrm->number,
					NULL);

	spin_unlock_irqrestore(&motu->lock, flags);
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_motu *motu = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&motu->lock, flags);

	if (up)
		amdtp_motu_midi_trigger(&motu->rx_stream, substrm->number,
					substrm);
	else
		amdtp_motu_midi_trigger(&motu->rx_stream, substrm->number,
					NULL);

	spin_unlock_irqrestore(&motu->lock, flags);
}

static void set_midi_substream_names(struct snd_motu *motu,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	list_for_each_entry(subs, &str->substreams, list) {
		snprintf(subs->name, sizeof(subs->name),
			 "%s MIDI %d", motu->card->shortname, subs->number + 1);
	}
}

int snd_motu_create_midi_devices(struct snd_motu *motu)
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
	err = snd_rawmidi_new(motu->card, motu->card->driver, 0, 1, 1, &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", motu->card->shortname);
	rmidi->private_data = motu;

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT |
			     SNDRV_RAWMIDI_INFO_OUTPUT |
			     SNDRV_RAWMIDI_INFO_DUPLEX;

	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			    &capture_ops);
	str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];
	set_midi_substream_names(motu, str);

	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &playback_ops);
	str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];
	set_midi_substream_names(motu, str);

	return 0;
}
