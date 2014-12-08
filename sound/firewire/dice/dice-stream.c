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

static void release_resources(struct snd_dice *dice)
{
	unsigned int channel;

	/* Reset channel number */
	channel = cpu_to_be32((u32)-1);
	snd_dice_transaction_write_rx(dice, RX_ISOCHRONOUS, &channel, 4);

	fw_iso_resources_free(&dice->rx_resources);
}

static int keep_resources(struct snd_dice *dice, unsigned int max_payload_bytes)
{
	unsigned int channel;
	int err;

	err = fw_iso_resources_allocate(&dice->rx_resources, max_payload_bytes,
				fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		goto end;

	/* Set channel number */
	channel = cpu_to_be32(dice->rx_resources.channel);
	err = snd_dice_transaction_write_rx(dice, RX_ISOCHRONOUS,
					    &channel, 4);
	if (err < 0)
		release_resources(dice);
end:
	return err;
}

static void stop_stream(struct snd_dice *dice)
{
	if (!amdtp_stream_running(&dice->rx_stream))
		return;

	amdtp_stream_pcm_abort(&dice->rx_stream);
	amdtp_stream_stop(&dice->rx_stream);
	release_resources(dice);
}

static int start_stream(struct snd_dice *dice, unsigned int rate)
{
	unsigned int i, mode, pcm_chs, midi_ports;
	int err;

	err = snd_dice_stream_get_rate_mode(dice, rate, &mode);
	if (err < 0)
		goto end;

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
	pcm_chs = dice->rx_channels[mode];
	midi_ports = dice->rx_midi_ports[mode];
	if (mode > 1) {
		rate /= 2;
		pcm_chs *= 2;
		dice->rx_stream.double_pcm_frames = true;
	} else {
		dice->rx_stream.double_pcm_frames = false;
	}

	amdtp_stream_set_parameters(&dice->rx_stream, rate,
				    pcm_chs, midi_ports);
	if (mode > 1) {
		pcm_chs /= 2;

		for (i = 0; i < pcm_chs; i++) {
			dice->rx_stream.pcm_positions[i] = i * 2;
			dice->rx_stream.pcm_positions[i + pcm_chs] = i * 2 + 1;
		}
	}

	err = keep_resources(dice,
			     amdtp_stream_get_max_payload(&dice->rx_stream));
	if (err < 0) {
		dev_err(&dice->unit->device,
			"fail to keep isochronous resources\n");
		goto end;
	}

	err = amdtp_stream_start(&dice->rx_stream, dice->rx_resources.channel,
				 fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		release_resources(dice);
end:
	return err;
}

int snd_dice_stream_start(struct snd_dice *dice, unsigned int rate)
{
	unsigned int curr_rate;
	int err;

	/* Some packet queueing errors. */
	if (amdtp_streaming_error(&dice->rx_stream))
		stop_stream(dice);

	/* Stop stream if rate is different. */
	err = snd_dice_transaction_get_rate(dice, &curr_rate);
	if (err < 0) {
		dev_err(&dice->unit->device,
			"fail to get sampling rate\n");
		goto end;
	}
	if (rate != curr_rate)
		stop_stream(dice);

	if (!amdtp_stream_running(&dice->rx_stream)) {
		snd_dice_transaction_clear_enable(dice);

		err = snd_dice_transaction_set_rate(dice, rate);
		if (err < 0) {
			dev_err(&dice->unit->device,
				"fail to set sampling rate\n");
			goto end;
		}

		/* Start stream. */
		err = start_stream(dice, rate);
		if (err < 0) {
			dev_err(&dice->unit->device,
				"fail to start AMDTP stream\n");
			goto end;
		}
		err = snd_dice_transaction_set_enable(dice);
		if (err < 0) {
			dev_err(&dice->unit->device,
				"fail to enable interface\n");
			stop_stream(dice);
			goto end;
		}

		if (!amdtp_stream_wait_callback(&dice->rx_stream,
						CALLBACK_TIMEOUT)) {
			snd_dice_transaction_clear_enable(dice);
			stop_stream(dice);
			err = -ETIMEDOUT;
		}
	}
end:
	return err;
}

void snd_dice_stream_stop(struct snd_dice *dice)
{
	snd_dice_transaction_clear_enable(dice);
	stop_stream(dice);
}

int snd_dice_stream_init(struct snd_dice *dice)
{
	int err;

	err = fw_iso_resources_init(&dice->rx_resources, dice->unit);
	if (err < 0)
		goto end;
	dice->rx_resources.channels_mask = 0x00000000ffffffffuLL;

	err = amdtp_stream_init(&dice->rx_stream, dice->unit, AMDTP_OUT_STREAM,
				CIP_BLOCKING);
	if (err < 0)
		goto error;

	err = snd_dice_transaction_set_clock_source(dice, CLOCK_SOURCE_ARX1);
	if (err < 0)
		goto error;
end:
	return err;
error:
	amdtp_stream_destroy(&dice->rx_stream);
	fw_iso_resources_destroy(&dice->rx_resources);
	return err;
}

void snd_dice_stream_destroy(struct snd_dice *dice)
{
	snd_dice_transaction_clear_enable(dice);
	stop_stream(dice);
	amdtp_stream_destroy(&dice->rx_stream);
	fw_iso_resources_destroy(&dice->rx_resources);
}

void snd_dice_stream_update(struct snd_dice *dice)
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

	stop_stream(dice);

	fw_iso_resources_update(&dice->rx_resources);
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
