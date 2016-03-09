/*
 * dice_midi.c - a part of driver for Dice based devices
 *
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */
#include "dice.h"

static int midi_open(struct snd_rawmidi_substream *substream)
{
	struct snd_dice *dice = substream->rmidi->private_data;
	int err;

	err = snd_dice_stream_lock_try(dice);
	if (err < 0)
		return err;

	mutex_lock(&dice->mutex);

	dice->substreams_counter++;
	err = snd_dice_stream_start_duplex(dice, 0);

	mutex_unlock(&dice->mutex);

	if (err < 0)
		snd_dice_stream_lock_release(dice);

	return err;
}

static int midi_close(struct snd_rawmidi_substream *substream)
{
	struct snd_dice *dice = substream->rmidi->private_data;

	mutex_lock(&dice->mutex);

	dice->substreams_counter--;
	snd_dice_stream_stop_duplex(dice);

	mutex_unlock(&dice->mutex);

	snd_dice_stream_lock_release(dice);
	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_dice *dice = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dice->lock, flags);

	if (up)
		amdtp_am824_midi_trigger(&dice->tx_stream[0],
					  substrm->number, substrm);
	else
		amdtp_am824_midi_trigger(&dice->tx_stream[0],
					  substrm->number, NULL);

	spin_unlock_irqrestore(&dice->lock, flags);
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_dice *dice = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&dice->lock, flags);

	if (up)
		amdtp_am824_midi_trigger(&dice->rx_stream[0],
					 substrm->number, substrm);
	else
		amdtp_am824_midi_trigger(&dice->rx_stream[0],
					 substrm->number, NULL);

	spin_unlock_irqrestore(&dice->lock, flags);
}

static struct snd_rawmidi_ops capture_ops = {
	.open		= midi_open,
	.close		= midi_close,
	.trigger	= midi_capture_trigger,
};

static struct snd_rawmidi_ops playback_ops = {
	.open		= midi_open,
	.close		= midi_close,
	.trigger	= midi_playback_trigger,
};

static void set_midi_substream_names(struct snd_dice *dice,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	list_for_each_entry(subs, &str->substreams, list) {
		snprintf(subs->name, sizeof(subs->name),
			 "%s MIDI %d", dice->card->shortname, subs->number + 1);
	}
}

int snd_dice_create_midi(struct snd_dice *dice)
{
	__be32 reg;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	unsigned int midi_in_ports, midi_out_ports;
	int err;

	/*
	 * Use the number of MIDI conformant data channel at current sampling
	 * transfer frequency.
	 */
	err = snd_dice_transaction_read_tx(dice, TX_NUMBER_MIDI,
					   &reg, sizeof(reg));
	if (err < 0)
		return err;
	midi_in_ports = be32_to_cpu(reg);

	err = snd_dice_transaction_read_rx(dice, RX_NUMBER_MIDI,
					   &reg, sizeof(reg));
	if (err < 0)
		return err;
	midi_out_ports = be32_to_cpu(reg);

	if (midi_in_ports + midi_out_ports == 0)
		return 0;

	/* create midi ports */
	err = snd_rawmidi_new(dice->card, dice->card->driver, 0,
			      midi_out_ports, midi_in_ports,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
		 "%s MIDI", dice->card->shortname);
	rmidi->private_data = dice;

	if (midi_in_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
				    &capture_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(dice, str);
	}

	if (midi_out_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &playback_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		set_midi_substream_names(dice, str);
	}

	if ((midi_out_ports > 0) && (midi_in_ports > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
