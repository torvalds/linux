/*
 * dice_stream.c - a part of driver for DICE based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2014 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "dice.h"

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

int snd_dice_stream_start_packets(struct snd_dice *dice)
{
	int err;

	if (amdtp_stream_running(&dice->rx_stream))
		return 0;

	err = amdtp_stream_start(&dice->rx_stream, dice->rx_resources.channel,
				 fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		return err;

	err = snd_dice_transaction_set_enable(dice);
	if (err < 0) {
		amdtp_stream_stop(&dice->rx_stream);
		return err;
	}

	return 0;
}

int snd_dice_stream_start(struct snd_dice *dice)
{
	__be32 channel;
	int err;

	if (!dice->rx_resources.allocated) {
		err = fw_iso_resources_allocate(&dice->rx_resources,
				amdtp_stream_get_max_payload(&dice->rx_stream),
				fw_parent_device(dice->unit)->max_speed);
		if (err < 0)
			goto error;

		channel = cpu_to_be32(dice->rx_resources.channel);
		err = snd_dice_transaction_write_tx(dice, RX_ISOCHRONOUS,
						    &channel, 4);
		if (err < 0)
			goto err_resources;
	}

	err = snd_dice_stream_start_packets(dice);
	if (err < 0)
		goto err_rx_channel;

	return 0;

err_rx_channel:
	channel = cpu_to_be32((u32)-1);
	snd_dice_transaction_write_rx(dice, RX_ISOCHRONOUS, &channel, 4);
err_resources:
	fw_iso_resources_free(&dice->rx_resources);
error:
	return err;
}

void snd_dice_stream_stop_packets(struct snd_dice *dice)
{
	if (!amdtp_stream_running(&dice->rx_stream))
		return;

	snd_dice_transaction_clear_enable(dice);
	amdtp_stream_stop(&dice->rx_stream);
}

void snd_dice_stream_stop(struct snd_dice *dice)
{
	__be32 channel;

	snd_dice_stream_stop_packets(dice);

	if (!dice->rx_resources.allocated)
		return;

	channel = cpu_to_be32((u32)-1);
	snd_dice_transaction_write_rx(dice, RX_ISOCHRONOUS, &channel, 4);

	fw_iso_resources_free(&dice->rx_resources);
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
	amdtp_stream_pcm_abort(&dice->rx_stream);
	snd_dice_stream_stop(dice);
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

	amdtp_stream_pcm_abort(&dice->rx_stream);
	snd_dice_stream_stop_packets(dice);
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
