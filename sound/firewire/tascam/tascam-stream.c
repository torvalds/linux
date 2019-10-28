/*
 * tascam-stream.c - a part of driver for TASCAM FireWire series
 *
 * Copyright (c) 2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/delay.h>
#include "tascam.h"

#define CLOCK_STATUS_MASK      0xffff0000
#define CLOCK_CONFIG_MASK      0x0000ffff

#define CALLBACK_TIMEOUT 500

static int get_clock(struct snd_tscm *tscm, u32 *data)
{
	int trial = 0;
	__be32 reg;
	int err;

	while (trial++ < 5) {
		err = snd_fw_transaction(tscm->unit, TCODE_READ_QUADLET_REQUEST,
				TSCM_ADDR_BASE + TSCM_OFFSET_CLOCK_STATUS,
				&reg, sizeof(reg), 0);
		if (err < 0)
			return err;

		*data = be32_to_cpu(reg);
		if (*data & CLOCK_STATUS_MASK)
			break;

		// In intermediate state after changing clock status.
		msleep(50);
	}

	// Still in the intermediate state.
	if (trial >= 5)
		return -EAGAIN;

	return 0;
}

static int set_clock(struct snd_tscm *tscm, unsigned int rate,
		     enum snd_tscm_clock clock)
{
	u32 data;
	__be32 reg;
	int err;

	err = get_clock(tscm, &data);
	if (err < 0)
		return err;
	data &= CLOCK_CONFIG_MASK;

	if (rate > 0) {
		data &= 0x000000ff;
		/* Base rate. */
		if ((rate % 44100) == 0) {
			data |= 0x00000100;
			/* Multiplier. */
			if (rate / 44100 == 2)
				data |= 0x00008000;
		} else if ((rate % 48000) == 0) {
			data |= 0x00000200;
			/* Multiplier. */
			if (rate / 48000 == 2)
				data |= 0x00008000;
		} else {
			return -EAGAIN;
		}
	}

	if (clock != INT_MAX) {
		data &= 0x0000ff00;
		data |= clock + 1;
	}

	reg = cpu_to_be32(data);

	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_CLOCK_STATUS,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	if (data & 0x00008000)
		reg = cpu_to_be32(0x0000001a);
	else
		reg = cpu_to_be32(0x0000000d);

	return snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				  TSCM_ADDR_BASE + TSCM_OFFSET_MULTIPLEX_MODE,
				  &reg, sizeof(reg), 0);
}

int snd_tscm_stream_get_rate(struct snd_tscm *tscm, unsigned int *rate)
{
	u32 data;
	int err;

	err = get_clock(tscm, &data);
	if (err < 0)
		return err;

	data = (data & 0xff000000) >> 24;

	/* Check base rate. */
	if ((data & 0x0f) == 0x01)
		*rate = 44100;
	else if ((data & 0x0f) == 0x02)
		*rate = 48000;
	else
		return -EAGAIN;

	/* Check multiplier. */
	if ((data & 0xf0) == 0x80)
		*rate *= 2;
	else if ((data & 0xf0) != 0x00)
		return -EAGAIN;

	return err;
}

int snd_tscm_stream_get_clock(struct snd_tscm *tscm, enum snd_tscm_clock *clock)
{
	u32 data;
	int err;

	err = get_clock(tscm, &data);
	if (err < 0)
		return err;

	*clock = ((data & 0x00ff0000) >> 16) - 1;
	if (*clock < 0 || *clock > SND_TSCM_CLOCK_ADAT)
		return -EIO;

	return 0;
}

static int enable_data_channels(struct snd_tscm *tscm)
{
	__be32 reg;
	u32 data;
	unsigned int i;
	int err;

	data = 0;
	for (i = 0; i < tscm->spec->pcm_capture_analog_channels; ++i)
		data |= BIT(i);
	if (tscm->spec->has_adat)
		data |= 0x0000ff00;
	if (tscm->spec->has_spdif)
		data |= 0x00030000;

	reg = cpu_to_be32(data);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_TX_PCM_CHANNELS,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	data = 0;
	for (i = 0; i < tscm->spec->pcm_playback_analog_channels; ++i)
		data |= BIT(i);
	if (tscm->spec->has_adat)
		data |= 0x0000ff00;
	if (tscm->spec->has_spdif)
		data |= 0x00030000;

	reg = cpu_to_be32(data);
	return snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				  TSCM_ADDR_BASE + TSCM_OFFSET_RX_PCM_CHANNELS,
				  &reg, sizeof(reg), 0);
}

static int set_stream_formats(struct snd_tscm *tscm, unsigned int rate)
{
	__be32 reg;
	int err;

	/* Set an option for unknown purpose. */
	reg = cpu_to_be32(0x00200000);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_SET_OPTION,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	err = enable_data_channels(tscm);
	if (err < 0)
		return err;

	return set_clock(tscm, rate, INT_MAX);
}

static void finish_session(struct snd_tscm *tscm)
{
	__be32 reg;

	reg = 0;
	snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
			   TSCM_ADDR_BASE + TSCM_OFFSET_START_STREAMING,
			   &reg, sizeof(reg), 0);

	reg = 0;
	snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
			   TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_RX_ON,
			   &reg, sizeof(reg), 0);

}

static int begin_session(struct snd_tscm *tscm)
{
	__be32 reg;
	int err;

	reg = cpu_to_be32(0x00000001);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_START_STREAMING,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	reg = cpu_to_be32(0x00000001);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_RX_ON,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	/* Set an option for unknown purpose. */
	reg = cpu_to_be32(0x00002000);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_SET_OPTION,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	/* Start multiplexing PCM samples on packets. */
	reg = cpu_to_be32(0x00000001);
	return snd_fw_transaction(tscm->unit,
				  TCODE_WRITE_QUADLET_REQUEST,
				  TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_TX_ON,
				  &reg, sizeof(reg), 0);
}

static void release_resources(struct snd_tscm *tscm)
{
	__be32 reg;

	/* Unregister channels. */
	reg = cpu_to_be32(0x00000000);
	snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
			   TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_TX_CH,
			   &reg, sizeof(reg), 0);
	reg = cpu_to_be32(0x00000000);
	snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
			   TSCM_ADDR_BASE + TSCM_OFFSET_UNKNOWN,
			   &reg, sizeof(reg), 0);
	reg = cpu_to_be32(0x00000000);
	snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
			   TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_RX_CH,
			   &reg, sizeof(reg), 0);

	/* Release isochronous resources. */
	fw_iso_resources_free(&tscm->tx_resources);
	fw_iso_resources_free(&tscm->rx_resources);
}

static int keep_resources(struct snd_tscm *tscm, unsigned int rate)
{
	__be32 reg;
	int err;

	/* Keep resources for in-stream. */
	err = amdtp_tscm_set_parameters(&tscm->tx_stream, rate);
	if (err < 0)
		return err;
	err = fw_iso_resources_allocate(&tscm->tx_resources,
			amdtp_stream_get_max_payload(&tscm->tx_stream),
			fw_parent_device(tscm->unit)->max_speed);
	if (err < 0)
		goto error;

	/* Keep resources for out-stream. */
	err = amdtp_tscm_set_parameters(&tscm->rx_stream, rate);
	if (err < 0)
		return err;
	err = fw_iso_resources_allocate(&tscm->rx_resources,
			amdtp_stream_get_max_payload(&tscm->rx_stream),
			fw_parent_device(tscm->unit)->max_speed);
	if (err < 0)
		return err;

	/* Register the isochronous channel for transmitting stream. */
	reg = cpu_to_be32(tscm->tx_resources.channel);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_TX_CH,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		goto error;

	/* Unknown */
	reg = cpu_to_be32(0x00000002);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_UNKNOWN,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		goto error;

	/* Register the isochronous channel for receiving stream. */
	reg = cpu_to_be32(tscm->rx_resources.channel);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_RX_CH,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		goto error;

	return 0;
error:
	release_resources(tscm);
	return err;
}

int snd_tscm_stream_init_duplex(struct snd_tscm *tscm)
{
	unsigned int pcm_channels;
	int err;

	/* For out-stream. */
	err = fw_iso_resources_init(&tscm->rx_resources, tscm->unit);
	if (err < 0)
		return err;
	pcm_channels = tscm->spec->pcm_playback_analog_channels;
	if (tscm->spec->has_adat)
		pcm_channels += 8;
	if (tscm->spec->has_spdif)
		pcm_channels += 2;
	err = amdtp_tscm_init(&tscm->rx_stream, tscm->unit, AMDTP_OUT_STREAM,
			      pcm_channels);
	if (err < 0)
		return err;

	/* For in-stream. */
	err = fw_iso_resources_init(&tscm->tx_resources, tscm->unit);
	if (err < 0)
		return err;
	pcm_channels = tscm->spec->pcm_capture_analog_channels;
	if (tscm->spec->has_adat)
		pcm_channels += 8;
	if (tscm->spec->has_spdif)
		pcm_channels += 2;
	err = amdtp_tscm_init(&tscm->tx_stream, tscm->unit, AMDTP_IN_STREAM,
			      pcm_channels);
	if (err < 0)
		amdtp_stream_destroy(&tscm->rx_stream);

	return err;
}

/* At bus reset, streaming is stopped and some registers are clear. */
void snd_tscm_stream_update_duplex(struct snd_tscm *tscm)
{
	amdtp_stream_pcm_abort(&tscm->tx_stream);
	amdtp_stream_stop(&tscm->tx_stream);

	amdtp_stream_pcm_abort(&tscm->rx_stream);
	amdtp_stream_stop(&tscm->rx_stream);
}

/*
 * This function should be called before starting streams or after stopping
 * streams.
 */
void snd_tscm_stream_destroy_duplex(struct snd_tscm *tscm)
{
	amdtp_stream_destroy(&tscm->rx_stream);
	amdtp_stream_destroy(&tscm->tx_stream);

	fw_iso_resources_destroy(&tscm->rx_resources);
	fw_iso_resources_destroy(&tscm->tx_resources);
}

int snd_tscm_stream_start_duplex(struct snd_tscm *tscm, unsigned int rate)
{
	unsigned int curr_rate;
	int err;

	if (tscm->substreams_counter == 0)
		return 0;

	err = snd_tscm_stream_get_rate(tscm, &curr_rate);
	if (err < 0)
		return err;
	if (curr_rate != rate ||
	    amdtp_streaming_error(&tscm->rx_stream) ||
	    amdtp_streaming_error(&tscm->tx_stream)) {
		finish_session(tscm);

		amdtp_stream_stop(&tscm->rx_stream);
		amdtp_stream_stop(&tscm->tx_stream);

		release_resources(tscm);
	}

	if (!amdtp_stream_running(&tscm->rx_stream)) {
		err = keep_resources(tscm, rate);
		if (err < 0)
			goto error;

		err = set_stream_formats(tscm, rate);
		if (err < 0)
			goto error;

		err = begin_session(tscm);
		if (err < 0)
			goto error;

		err = amdtp_stream_start(&tscm->rx_stream,
				tscm->rx_resources.channel,
				fw_parent_device(tscm->unit)->max_speed);
		if (err < 0)
			goto error;

		if (!amdtp_stream_wait_callback(&tscm->rx_stream,
						CALLBACK_TIMEOUT)) {
			err = -ETIMEDOUT;
			goto error;
		}
	}

	if (!amdtp_stream_running(&tscm->tx_stream)) {
		err = amdtp_stream_start(&tscm->tx_stream,
				tscm->tx_resources.channel,
				fw_parent_device(tscm->unit)->max_speed);
		if (err < 0)
			goto error;

		if (!amdtp_stream_wait_callback(&tscm->tx_stream,
						CALLBACK_TIMEOUT)) {
			err = -ETIMEDOUT;
			goto error;
		}
	}

	return 0;
error:
	amdtp_stream_stop(&tscm->rx_stream);
	amdtp_stream_stop(&tscm->tx_stream);

	finish_session(tscm);
	release_resources(tscm);

	return err;
}

void snd_tscm_stream_stop_duplex(struct snd_tscm *tscm)
{
	if (tscm->substreams_counter > 0)
		return;

	amdtp_stream_stop(&tscm->tx_stream);
	amdtp_stream_stop(&tscm->rx_stream);

	finish_session(tscm);
	release_resources(tscm);
}

void snd_tscm_stream_lock_changed(struct snd_tscm *tscm)
{
	tscm->dev_lock_changed = true;
	wake_up(&tscm->hwdep_wait);
}

int snd_tscm_stream_lock_try(struct snd_tscm *tscm)
{
	int err;

	spin_lock_irq(&tscm->lock);

	/* user land lock this */
	if (tscm->dev_lock_count < 0) {
		err = -EBUSY;
		goto end;
	}

	/* this is the first time */
	if (tscm->dev_lock_count++ == 0)
		snd_tscm_stream_lock_changed(tscm);
	err = 0;
end:
	spin_unlock_irq(&tscm->lock);
	return err;
}

void snd_tscm_stream_lock_release(struct snd_tscm *tscm)
{
	spin_lock_irq(&tscm->lock);

	if (WARN_ON(tscm->dev_lock_count <= 0))
		goto end;
	if (--tscm->dev_lock_count == 0)
		snd_tscm_stream_lock_changed(tscm);
end:
	spin_unlock_irq(&tscm->lock);
}
