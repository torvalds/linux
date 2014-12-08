/*
 * dice_stream.c - a part of driver for DICE based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2014 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "dice.h"

#define	CALLBACK_TIMEOUT	200

const unsigned int snd_dice_rates[SND_DICE_RATES_COUNT] = {
	/* mode 0 */
	[0] =  32000,
	[1] =  44100,
	[2] =  48000,
	/* mode 1 */
	[3] =  88200,
	[4] =  96000,
	/* mode 2 */
	[5] = 176400,
	[6] = 192000,
};

int snd_dice_stream_get_rate_mode(struct snd_dice *dice, unsigned int rate,
				  unsigned int *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(snd_dice_rates); i++) {
		if (!(dice->clock_caps & BIT(i)))
			continue;
		if (snd_dice_rates[i] != rate)
			continue;

		*mode = (i - 1) / 2;
		return 0;
	}
	return -EINVAL;
}

static void release_resources(struct snd_dice *dice,
			      struct fw_iso_resources *resources)
{
	unsigned int channel;

	/* Reset channel number */
	channel = cpu_to_be32((u32)-1);
	if (resources == &dice->tx_resources)
		snd_dice_transaction_write_tx(dice, TX_ISOCHRONOUS,
					      &channel, 4);
	else
		snd_dice_transaction_write_rx(dice, RX_ISOCHRONOUS,
					      &channel, 4);

	fw_iso_resources_free(resources);
}

static int keep_resources(struct snd_dice *dice,
			  struct fw_iso_resources *resources,
			  unsigned int max_payload_bytes)
{
	unsigned int channel;
	int err;

	err = fw_iso_resources_allocate(resources, max_payload_bytes,
				fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		goto end;

	/* Set channel number */
	channel = cpu_to_be32(resources->channel);
	if (resources == &dice->tx_resources)
		err = snd_dice_transaction_write_tx(dice, TX_ISOCHRONOUS,
						    &channel, 4);
	else
		err = snd_dice_transaction_write_rx(dice, RX_ISOCHRONOUS,
						    &channel, 4);
	if (err < 0)
		release_resources(dice, resources);
end:
	return err;
}

static void stop_stream(struct snd_dice *dice, struct amdtp_stream *stream)
{
	amdtp_stream_pcm_abort(stream);
	amdtp_stream_stop(stream);

	if (stream == &dice->tx_stream)
		release_resources(dice, &dice->tx_resources);
	else
		release_resources(dice, &dice->rx_resources);
}

static int start_stream(struct snd_dice *dice, struct amdtp_stream *stream,
			unsigned int rate)
{
	struct fw_iso_resources *resources;
	unsigned int i, mode, pcm_chs, midi_ports;
	int err;

	err = snd_dice_stream_get_rate_mode(dice, rate, &mode);
	if (err < 0)
		goto end;
	if (stream == &dice->tx_stream) {
		resources = &dice->tx_resources;
		pcm_chs = dice->tx_channels[mode];
		midi_ports = dice->tx_midi_ports[mode];
	} else {
		resources = &dice->rx_resources;
		pcm_chs = dice->rx_channels[mode];
		midi_ports = dice->rx_midi_ports[mode];
	}

	/*
	 * At 176.4/192.0 kHz, Dice has a quirk to transfer two PCM frames in
	 * one data block of AMDTP packet. Thus sampling transfer frequency is
	 * a half of PCM sampling frequency, i.e. PCM frames at 192.0 kHz are
	 * transferred on AMDTP packets at 96 kHz. Two successive samples of a
	 * channel are stored consecutively in the packet. This quirk is called
	 * as 'Dual Wire'.
	 * For this quirk, blocking mode is required and PCM buffer size should
	 * be aligned to SYT_INTERVAL.
	 */
	if (mode > 1) {
		rate /= 2;
		pcm_chs *= 2;
		stream->double_pcm_frames = true;
	} else {
		stream->double_pcm_frames = false;
	}

	amdtp_stream_set_parameters(stream, rate, pcm_chs, midi_ports);
	if (mode > 1) {
		pcm_chs /= 2;

		for (i = 0; i < pcm_chs; i++) {
			stream->pcm_positions[i] = i * 2;
			stream->pcm_positions[i + pcm_chs] = i * 2 + 1;
		}
	}

	err = keep_resources(dice, resources,
			     amdtp_stream_get_max_payload(stream));
	if (err < 0) {
		dev_err(&dice->unit->device,
			"fail to keep isochronous resources\n");
		goto end;
	}

	err = amdtp_stream_start(stream, resources->channel,
				 fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		release_resources(dice, resources);
end:
	return err;
}

static int get_sync_mode(struct snd_dice *dice, enum cip_flags *sync_mode)
{
	/* Currently, clock source is fixed at SYT-Match mode. */
	*sync_mode = 0;
	return 0;
}

int snd_dice_stream_start_duplex(struct snd_dice *dice, unsigned int rate)
{
	struct amdtp_stream *master, *slave;
	unsigned int curr_rate;
	enum cip_flags sync_mode;
	int err = 0;

	if (dice->substreams_counter == 0)
		goto end;

	err = get_sync_mode(dice, &sync_mode);
	if (err < 0)
		goto end;
	if (sync_mode == CIP_SYNC_TO_DEVICE) {
		master = &dice->tx_stream;
		slave  = &dice->rx_stream;
	} else {
		master = &dice->rx_stream;
		slave  = &dice->tx_stream;
	}

	/* Some packet queueing errors. */
	if (amdtp_streaming_error(master) || amdtp_streaming_error(slave))
		stop_stream(dice, master);

	/* Stop stream if rate is different. */
	err = snd_dice_transaction_get_rate(dice, &curr_rate);
	if (err < 0) {
		dev_err(&dice->unit->device,
			"fail to get sampling rate\n");
		goto end;
	}
	if (rate != curr_rate)
		stop_stream(dice, master);

	if (!amdtp_stream_running(master)) {
		stop_stream(dice, slave);
		snd_dice_transaction_clear_enable(dice);

		amdtp_stream_set_sync(sync_mode, master, slave);

		err = snd_dice_transaction_set_rate(dice, rate);
		if (err < 0) {
			dev_err(&dice->unit->device,
				"fail to set sampling rate\n");
			goto end;
		}

		/* Start both streams. */
		err = start_stream(dice, master, rate);
		if (err < 0) {
			dev_err(&dice->unit->device,
				"fail to start AMDTP master stream\n");
			goto end;
		}
		err = start_stream(dice, slave, rate);
		if (err < 0) {
			dev_err(&dice->unit->device,
				"fail to start AMDTP slave stream\n");
			stop_stream(dice, master);
			goto end;
		}
		err = snd_dice_transaction_set_enable(dice);
		if (err < 0) {
			dev_err(&dice->unit->device,
				"fail to enable interface\n");
			stop_stream(dice, master);
			stop_stream(dice, slave);
			goto end;
		}

		/* Wait first callbacks */
		if (!amdtp_stream_wait_callback(master, CALLBACK_TIMEOUT) ||
		    !amdtp_stream_wait_callback(slave, CALLBACK_TIMEOUT)) {
			snd_dice_transaction_clear_enable(dice);
			stop_stream(dice, master);
			stop_stream(dice, slave);
			err = -ETIMEDOUT;
		}
	}
end:
	return err;
}

void snd_dice_stream_stop_duplex(struct snd_dice *dice)
{
	if (dice->substreams_counter > 0)
		return;

	snd_dice_transaction_clear_enable(dice);

	stop_stream(dice, &dice->tx_stream);
	stop_stream(dice, &dice->rx_stream);
}

static int init_stream(struct snd_dice *dice, struct amdtp_stream *stream)
{
	int err;
	struct fw_iso_resources *resources;
	enum amdtp_stream_direction dir;

	if (stream == &dice->tx_stream) {
		resources = &dice->tx_resources;
		dir = AMDTP_IN_STREAM;
	} else {
		resources = &dice->rx_resources;
		dir = AMDTP_OUT_STREAM;
	}

	err = fw_iso_resources_init(resources, dice->unit);
	if (err < 0)
		goto end;
	resources->channels_mask = 0x00000000ffffffffuLL;

	err = amdtp_stream_init(stream, dice->unit, dir, CIP_BLOCKING);
	if (err < 0) {
		amdtp_stream_destroy(stream);
		fw_iso_resources_destroy(resources);
	}
end:
	return err;
}

static void destroy_stream(struct snd_dice *dice, struct amdtp_stream *stream)
{
	amdtp_stream_destroy(stream);

	if (stream == &dice->tx_stream)
		fw_iso_resources_destroy(&dice->tx_resources);
	else
		fw_iso_resources_destroy(&dice->rx_resources);
}

int snd_dice_stream_init_duplex(struct snd_dice *dice)
{
	int err;

	dice->substreams_counter = 0;

	err = init_stream(dice, &dice->tx_stream);
	if (err < 0)
		goto end;

	err = init_stream(dice, &dice->rx_stream);
	if (err < 0)
		goto end;

	/* Currently, clock source is fixed at SYT-Match mode. */
	err = snd_dice_transaction_set_clock_source(dice, CLOCK_SOURCE_ARX1);
	if (err < 0) {
		destroy_stream(dice, &dice->rx_stream);
		destroy_stream(dice, &dice->tx_stream);
	}
end:
	return err;
}

void snd_dice_stream_destroy_duplex(struct snd_dice *dice)
{
	snd_dice_transaction_clear_enable(dice);

	stop_stream(dice, &dice->tx_stream);
	destroy_stream(dice, &dice->tx_stream);

	stop_stream(dice, &dice->rx_stream);
	destroy_stream(dice, &dice->rx_stream);

	dice->substreams_counter = 0;
}

void snd_dice_stream_update_duplex(struct snd_dice *dice)
{
	/*
	 * On a bus reset, the DICE firmware disables streaming and then goes
	 * off contemplating its own navel for hundreds of milliseconds before
	 * it can react to any of our attempts to reenable streaming.  This
	 * means that we lose synchronization anyway, so we force our streams
	 * to stop so that the application can restart them in an orderly
	 * manner.
	 */
	dice->global_enabled = false;

	stop_stream(dice, &dice->rx_stream);
	stop_stream(dice, &dice->tx_stream);

	fw_iso_resources_update(&dice->rx_resources);
	fw_iso_resources_update(&dice->tx_resources);
}

static void dice_lock_changed(struct snd_dice *dice)
{
	dice->dev_lock_changed = true;
	wake_up(&dice->hwdep_wait);
}

int snd_dice_stream_lock_try(struct snd_dice *dice)
{
	int err;

	spin_lock_irq(&dice->lock);

	if (dice->dev_lock_count < 0) {
		err = -EBUSY;
		goto out;
	}

	if (dice->dev_lock_count++ == 0)
		dice_lock_changed(dice);
	err = 0;
out:
	spin_unlock_irq(&dice->lock);
	return err;
}

void snd_dice_stream_lock_release(struct snd_dice *dice)
{
	spin_lock_irq(&dice->lock);

	if (WARN_ON(dice->dev_lock_count <= 0))
		goto out;

	if (--dice->dev_lock_count == 0)
		dice_lock_changed(dice);
out:
	spin_unlock_irq(&dice->lock);
}
