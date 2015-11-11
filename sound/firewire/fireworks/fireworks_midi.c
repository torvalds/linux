/*
 * fireworks_midi.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */
#include "fireworks.h"

static int midi_capture_open(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;
	int err;

	err = snd_efw_stream_lock_try(efw);
	if (err < 0)
		goto end;

	atomic_inc(&efw->capture_substreams);
	err = snd_efw_stream_start_duplex(efw, 0);
	if (err < 0)
		snd_efw_stream_lock_release(efw);

end:
	return err;
}

static int midi_playback_open(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;
	int err;

	err = snd_efw_stream_lock_try(efw);
	if (err < 0)
		goto end;

	atomic_inc(&efw->playback_substreams);
	err = snd_efw_stream_start_duplex(efw, 0);
	if (err < 0)
		snd_efw_stream_lock_release(efw);
end:
	return err;
}

static int midi_capture_close(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;

	atomic_dec(&efw->capture_substreams);
	snd_efw_stream_stop_duplex(efw);

	snd_efw_stream_lock_release(efw);
	return 0;
}

static int midi_playback_close(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;

	atomic_dec(&efw->playback_substreams);
	snd_efw_stream_stop_duplex(efw);

	snd_efw_stream_lock_release(efw);
	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_efw *efw = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&efw->lock, flags);

	if (up)
		amdtp_am824_midi_trigger(&efw->tx_stream,
					  substrm->number, substrm);
	else
		amdtp_am824_midi_trigger(&efw->tx_stream,
					  substrm->number, NULL);

	spin_unlock_irqrestore(&efw->lock, flags);
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_efw *efw = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&efw->lock, flags);

	if (up)
		amdtp_am824_midi_trigger(&efw->rx_stream,
					 substrm->number, substrm);
	else
		amdtp_am824_midi_trigger(&efw->rx_stream,
					 substrm->number, NULL);

	spin_unlock_irqrestore(&efw->lock, flags);
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

static void set_midi_substream_names(struct snd_efw *efw,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	list_for_each_entry(subs, &str->substreams, list) {
		snprintf(subs->name, sizeof(subs->name),
			 "%s MIDI %d", efw->card->shortname, subs->number + 1);
	}
}

int snd_efw_create_midi_devices(struct snd_efw *efw)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	int err;

	/* create midi ports */
	err = snd_rawmidi_new(efw->card, efw->card->driver, 0,
			      efw->midi_out_ports, efw->midi_in_ports,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", efw->card->shortname);
	rmidi->private_data = efw;

	if (efw->midi_in_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
				    &midi_capture_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(efw, str);
	}

	if (efw->midi_out_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &midi_playback_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		set_midi_substream_names(efw, str);
	}

	if ((efw->midi_out_ports > 0) && (efw->midi_in_ports > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
