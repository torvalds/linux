// SPDX-License-Identifier: GPL-2.0-only
/*
 * tascam-midi.c - a part of driver for TASCAM FireWire series
 *
 * Copyright (c) 2015 Takashi Sakamoto
 */

#include "tascam.h"

static int midi_capture_open(struct snd_rawmidi_substream *substream)
{
	/* Do nothing. */
	return 0;
}

static int midi_playback_open(struct snd_rawmidi_substream *substream)
{
	struct snd_tscm *tscm = substream->rmidi->private_data;

	snd_fw_async_midi_port_init(&tscm->out_ports[substream->number]);

	return 0;
}

static int midi_capture_close(struct snd_rawmidi_substream *substream)
{
	/* Do nothing. */
	return 0;
}

static int midi_playback_close(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static void midi_playback_drain(struct snd_rawmidi_substream *substream)
{
	struct snd_tscm *tscm = substream->rmidi->private_data;

	snd_fw_async_midi_port_finish(&tscm->out_ports[substream->number]);
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_tscm *tscm = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&tscm->lock, flags);

	if (up)
		tscm->tx_midi_substreams[substrm->number] = substrm;
	else
		tscm->tx_midi_substreams[substrm->number] = NULL;

	spin_unlock_irqrestore(&tscm->lock, flags);
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_tscm *tscm = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&tscm->lock, flags);

	if (up)
		snd_fw_async_midi_port_run(&tscm->out_ports[substrm->number],
					   substrm);

	spin_unlock_irqrestore(&tscm->lock, flags);
}

int snd_tscm_create_midi_devices(struct snd_tscm *tscm)
{
	static const struct snd_rawmidi_ops capture_ops = {
		.open		= midi_capture_open,
		.close		= midi_capture_close,
		.trigger	= midi_capture_trigger,
	};
	static const struct snd_rawmidi_ops playback_ops = {
		.open		= midi_playback_open,
		.close		= midi_playback_close,
		.drain		= midi_playback_drain,
		.trigger	= midi_playback_trigger,
	};
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *stream;
	struct snd_rawmidi_substream *subs;
	int err;

	err = snd_rawmidi_new(tscm->card, tscm->card->driver, 0,
			      tscm->spec->midi_playback_ports,
			      tscm->spec->midi_capture_ports,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", tscm->card->shortname);
	rmidi->private_data = tscm;

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			    &capture_ops);
	stream = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

	/* Set port names for MIDI input. */
	list_for_each_entry(subs, &stream->substreams, list) {
		/* TODO: support virtual MIDI ports. */
		if (subs->number < tscm->spec->midi_capture_ports) {
			/* Hardware MIDI ports. */
			snprintf(subs->name, sizeof(subs->name),
				 "%s MIDI %d",
				 tscm->card->shortname, subs->number + 1);
		}
	}

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &playback_ops);
	stream = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

	/* Set port names for MIDI ourput. */
	list_for_each_entry(subs, &stream->substreams, list) {
		if (subs->number < tscm->spec->midi_playback_ports) {
			/* Hardware MIDI ports only. */
			snprintf(subs->name, sizeof(subs->name),
				 "%s MIDI %d",
				 tscm->card->shortname, subs->number + 1);
		}
	}

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
