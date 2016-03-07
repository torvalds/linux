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
#define NOTIFICATION_TIMEOUT_MS	(2 * MSEC_PER_SEC)

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

/*
 * This operation has an effect to synchronize GLOBAL_STATUS/GLOBAL_SAMPLE_RATE
 * to GLOBAL_STATUS. Especially, just after powering on, these are different.
 */
static int ensure_phase_lock(struct snd_dice *dice)
{
	__be32 reg, nominal;
	int err;

	err = snd_dice_transaction_read_global(dice, GLOBAL_CLOCK_SELECT,
					       &reg, sizeof(reg));
	if (err < 0)
		return err;

	if (completion_done(&dice->clock_accepted))
		reinit_completion(&dice->clock_accepted);

	err = snd_dice_transaction_write_global(dice, GLOBAL_CLOCK_SELECT,
						&reg, sizeof(reg));
	if (err < 0)
		return err;

	if (wait_for_completion_timeout(&dice->clock_accepted,
			msecs_to_jiffies(NOTIFICATION_TIMEOUT_MS)) == 0) {
		/*
		 * Old versions of Dice firmware transfer no notification when
		 * the same clock status as current one is set. In this case,
		 * just check current clock status.
		 */
		err = snd_dice_transaction_read_global(dice, GLOBAL_STATUS,
						&nominal, sizeof(nominal));
		if (err < 0)
			return err;
		if (!(be32_to_cpu(nominal) & STATUS_SOURCE_LOCKED))
			return -ETIMEDOUT;
	}

	return 0;
}

static void release_resources(struct snd_dice *dice,
			      struct fw_iso_resources *resources)
{
	__be32 channel;

	/* Reset channel number */
	channel = cpu_to_be32((u32)-1);
	if (resources == &dice->tx_resources[0])
		snd_dice_transaction_write_tx(dice, TX_ISOCHRONOUS,
					      &channel, sizeof(channel));
	else
		snd_dice_transaction_write_rx(dice, RX_ISOCHRONOUS,
					      &channel, sizeof(channel));

	fw_iso_resources_free(resources);
}

static int keep_resources(struct snd_dice *dice,
			  struct fw_iso_resources *resources,
			  unsigned int max_payload_bytes)
{
	__be32 channel;
	int err;

	err = fw_iso_resources_allocate(resources, max_payload_bytes,
				fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		goto end;

	/* Set channel number */
	channel = cpu_to_be32(resources->channel);
	if (resources == &dice->tx_resources[0])
		err = snd_dice_transaction_write_tx(dice, TX_ISOCHRONOUS,
						    &channel, sizeof(channel));
	else
		err = snd_dice_transaction_write_rx(dice, RX_ISOCHRONOUS,
						    &channel, sizeof(channel));
	if (err < 0)
		release_resources(dice, resources);
end:
	return err;
}

static void stop_stream(struct snd_dice *dice, struct amdtp_stream *stream)
{
	amdtp_stream_pcm_abort(stream);
	amdtp_stream_stop(stream);

	if (stream == &dice->tx_stream[0])
		release_resources(dice, &dice->tx_resources[0]);
	else
		release_resources(dice, &dice->rx_resources[0]);
}

static int start_stream(struct snd_dice *dice, struct amdtp_stream *stream,
			unsigned int rate)
{
	struct fw_iso_resources *resources;
	__be32 reg[2];
	unsigned int i, pcm_chs, midi_ports;
	bool double_pcm_frames;
	int err;

	if (stream == &dice->tx_stream[0]) {
		resources = &dice->tx_resources[0];
		err = snd_dice_transaction_read_tx(dice, TX_NUMBER_AUDIO,
						   reg, sizeof(reg));
	} else {
		resources = &dice->rx_resources[0];
		err = snd_dice_transaction_read_rx(dice, RX_NUMBER_AUDIO,
						   reg, sizeof(reg));
	}

	if (err < 0)
		goto end;

	pcm_chs = be32_to_cpu(reg[0]);
	midi_ports = be32_to_cpu(reg[1]);

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
	double_pcm_frames = rate > 96000;
	if (double_pcm_frames) {
		rate /= 2;
		pcm_chs *= 2;
	}

	err = amdtp_am824_set_parameters(stream, rate, pcm_chs, midi_ports,
					 double_pcm_frames);
	if (err < 0)
		goto end;

	if (double_pcm_frames) {
		pcm_chs /= 2;

		for (i = 0; i < pcm_chs; i++) {
			amdtp_am824_set_pcm_position(stream, i, i * 2);
			amdtp_am824_set_pcm_position(stream, i + pcm_chs,
						     i * 2 + 1);
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

int snd_dice_stream_start_duplex(struct snd_dice *dice, unsigned int rate)
{
	struct amdtp_stream *master, *slave;
	unsigned int curr_rate;
	int err = 0;

	if (dice->substreams_counter == 0)
		goto end;

	master = &dice->rx_stream[0];
	slave  = &dice->tx_stream[0];

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
	if (rate == 0)
		rate = curr_rate;
	if (rate != curr_rate) {
		err = -EINVAL;
		goto end;
	}

	if (!amdtp_stream_running(master)) {
		stop_stream(dice, slave);
		snd_dice_transaction_clear_enable(dice);

		err = ensure_phase_lock(dice);
		if (err < 0) {
			dev_err(&dice->unit->device,
				"fail to ensure phase lock\n");
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

	stop_stream(dice, &dice->tx_stream[0]);
	stop_stream(dice, &dice->rx_stream[0]);
}

static int init_stream(struct snd_dice *dice, struct amdtp_stream *stream)
{
	int err;
	struct fw_iso_resources *resources;
	enum amdtp_stream_direction dir;

	if (stream == &dice->tx_stream[0]) {
		resources = &dice->tx_resources[0];
		dir = AMDTP_IN_STREAM;
	} else {
		resources = &dice->rx_resources[0];
		dir = AMDTP_OUT_STREAM;
	}

	err = fw_iso_resources_init(resources, dice->unit);
	if (err < 0)
		goto end;
	resources->channels_mask = 0x00000000ffffffffuLL;

	err = amdtp_am824_init(stream, dice->unit, dir, CIP_BLOCKING);
	if (err < 0) {
		amdtp_stream_destroy(stream);
		fw_iso_resources_destroy(resources);
	}
end:
	return err;
}

/*
 * This function should be called before starting streams or after stopping
 * streams.
 */
static void destroy_stream(struct snd_dice *dice, struct amdtp_stream *stream)
{
	struct fw_iso_resources *resources;

	if (stream == &dice->tx_stream[0])
		resources = &dice->tx_resources[0];
	else
		resources = &dice->rx_resources[0];

	amdtp_stream_destroy(stream);
	fw_iso_resources_destroy(resources);
}

int snd_dice_stream_init_duplex(struct snd_dice *dice)
{
	int err;

	dice->substreams_counter = 0;

	err = init_stream(dice, &dice->tx_stream[0]);
	if (err < 0)
		goto end;

	err = init_stream(dice, &dice->rx_stream[0]);
	if (err < 0)
		destroy_stream(dice, &dice->tx_stream[0]);
end:
	return err;
}

void snd_dice_stream_destroy_duplex(struct snd_dice *dice)
{
	snd_dice_transaction_clear_enable(dice);

	destroy_stream(dice, &dice->tx_stream[0]);
	destroy_stream(dice, &dice->rx_stream[0]);

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

	stop_stream(dice, &dice->rx_stream[0]);
	stop_stream(dice, &dice->tx_stream[0]);

	fw_iso_resources_update(&dice->rx_resources[0]);
	fw_iso_resources_update(&dice->tx_resources[0]);
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
