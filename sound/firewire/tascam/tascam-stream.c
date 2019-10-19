// SPDX-License-Identifier: GPL-2.0-only
/*
 * tascam-stream.c - a part of driver for TASCAM FireWire series
 *
 * Copyright (c) 2015 Takashi Sakamoto
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

	// Set an option for unknown purpose.
	reg = cpu_to_be32(0x00200000);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_SET_OPTION,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	return enable_data_channels(tscm);
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

	// Unregister channels.
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
}

static int begin_session(struct snd_tscm *tscm)
{
	__be32 reg;
	int err;

	// Register the isochronous channel for transmitting stream.
	reg = cpu_to_be32(tscm->tx_resources.channel);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_TX_CH,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	// Unknown.
	reg = cpu_to_be32(0x00000002);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_UNKNOWN,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	// Register the isochronous channel for receiving stream.
	reg = cpu_to_be32(tscm->rx_resources.channel);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_RX_CH,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

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

	// Set an option for unknown purpose.
	reg = cpu_to_be32(0x00002000);
	err = snd_fw_transaction(tscm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 TSCM_ADDR_BASE + TSCM_OFFSET_SET_OPTION,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	// Start multiplexing PCM samples on packets.
	reg = cpu_to_be32(0x00000001);
	return snd_fw_transaction(tscm->unit,
				  TCODE_WRITE_QUADLET_REQUEST,
				  TSCM_ADDR_BASE + TSCM_OFFSET_ISOC_TX_ON,
				  &reg, sizeof(reg), 0);
}

static int keep_resources(struct snd_tscm *tscm, unsigned int rate,
			  struct amdtp_stream *stream)
{
	struct fw_iso_resources *resources;
	int err;

	if (stream == &tscm->tx_stream)
		resources = &tscm->tx_resources;
	else
		resources = &tscm->rx_resources;

	err = amdtp_tscm_set_parameters(stream, rate);
	if (err < 0)
		return err;

	return fw_iso_resources_allocate(resources,
				amdtp_stream_get_max_payload(stream),
				fw_parent_device(tscm->unit)->max_speed);
}

static int init_stream(struct snd_tscm *tscm, struct amdtp_stream *s)
{
	struct fw_iso_resources *resources;
	enum amdtp_stream_direction dir;
	unsigned int pcm_channels;
	int err;

	if (s == &tscm->tx_stream) {
		resources = &tscm->tx_resources;
		dir = AMDTP_IN_STREAM;
		pcm_channels = tscm->spec->pcm_capture_analog_channels;
	} else {
		resources = &tscm->rx_resources;
		dir = AMDTP_OUT_STREAM;
		pcm_channels = tscm->spec->pcm_playback_analog_channels;
	}

	if (tscm->spec->has_adat)
		pcm_channels += 8;
	if (tscm->spec->has_spdif)
		pcm_channels += 2;

	err = fw_iso_resources_init(resources, tscm->unit);
	if (err < 0)
		return err;

	err = amdtp_tscm_init(s, tscm->unit, dir, pcm_channels);
	if (err < 0)
		fw_iso_resources_free(resources);

	return err;
}

static void destroy_stream(struct snd_tscm *tscm, struct amdtp_stream *s)
{
	amdtp_stream_destroy(s);

	if (s == &tscm->tx_stream)
		fw_iso_resources_destroy(&tscm->tx_resources);
	else
		fw_iso_resources_destroy(&tscm->rx_resources);
}

int snd_tscm_stream_init_duplex(struct snd_tscm *tscm)
{
	int err;

	err = init_stream(tscm, &tscm->tx_stream);
	if (err < 0)
		return err;

	err = init_stream(tscm, &tscm->rx_stream);
	if (err < 0) {
		destroy_stream(tscm, &tscm->tx_stream);
		return err;
	}

	err = amdtp_domain_init(&tscm->domain);
	if (err < 0) {
		destroy_stream(tscm, &tscm->tx_stream);
		destroy_stream(tscm, &tscm->rx_stream);
	}

	return err;
}

// At bus reset, streaming is stopped and some registers are clear.
void snd_tscm_stream_update_duplex(struct snd_tscm *tscm)
{
	amdtp_domain_stop(&tscm->domain);

	amdtp_stream_pcm_abort(&tscm->tx_stream);
	amdtp_stream_pcm_abort(&tscm->rx_stream);
}

// This function should be called before starting streams or after stopping
// streams.
void snd_tscm_stream_destroy_duplex(struct snd_tscm *tscm)
{
	amdtp_domain_destroy(&tscm->domain);

	destroy_stream(tscm, &tscm->rx_stream);
	destroy_stream(tscm, &tscm->tx_stream);
}

int snd_tscm_stream_reserve_duplex(struct snd_tscm *tscm, unsigned int rate,
				   unsigned int frames_per_period,
				   unsigned int frames_per_buffer)
{
	unsigned int curr_rate;
	int err;

	err = snd_tscm_stream_get_rate(tscm, &curr_rate);
	if (err < 0)
		return err;

	if (tscm->substreams_counter == 0 || rate != curr_rate) {
		amdtp_domain_stop(&tscm->domain);

		finish_session(tscm);

		fw_iso_resources_free(&tscm->tx_resources);
		fw_iso_resources_free(&tscm->rx_resources);

		err = set_clock(tscm, rate, INT_MAX);
		if (err < 0)
			return err;

		err = keep_resources(tscm, rate, &tscm->tx_stream);
		if (err < 0)
			return err;

		err = keep_resources(tscm, rate, &tscm->rx_stream);
		if (err < 0) {
			fw_iso_resources_free(&tscm->tx_resources);
			return err;
		}

		err = amdtp_domain_set_events_per_period(&tscm->domain,
					frames_per_period, frames_per_buffer);
		if (err < 0) {
			fw_iso_resources_free(&tscm->tx_resources);
			fw_iso_resources_free(&tscm->rx_resources);
			return err;
		}
	}

	return 0;
}

int snd_tscm_stream_start_duplex(struct snd_tscm *tscm, unsigned int rate)
{
	unsigned int generation = tscm->rx_resources.generation;
	int err;

	if (tscm->substreams_counter == 0)
		return 0;

	if (amdtp_streaming_error(&tscm->rx_stream) ||
	    amdtp_streaming_error(&tscm->tx_stream)) {
		amdtp_domain_stop(&tscm->domain);
		finish_session(tscm);
	}

	if (generation != fw_parent_device(tscm->unit)->card->generation) {
		err = fw_iso_resources_update(&tscm->tx_resources);
		if (err < 0)
			goto error;

		err = fw_iso_resources_update(&tscm->rx_resources);
		if (err < 0)
			goto error;
	}

	if (!amdtp_stream_running(&tscm->rx_stream)) {
		int spd = fw_parent_device(tscm->unit)->max_speed;

		err = set_stream_formats(tscm, rate);
		if (err < 0)
			goto error;

		err = begin_session(tscm);
		if (err < 0)
			goto error;

		err = amdtp_domain_add_stream(&tscm->domain, &tscm->rx_stream,
					      tscm->rx_resources.channel, spd);
		if (err < 0)
			goto error;

		err = amdtp_domain_add_stream(&tscm->domain, &tscm->tx_stream,
					      tscm->tx_resources.channel, spd);
		if (err < 0)
			goto error;

		err = amdtp_domain_start(&tscm->domain, 0);
		if (err < 0)
			return err;

		if (!amdtp_stream_wait_callback(&tscm->rx_stream,
						CALLBACK_TIMEOUT) ||
		    !amdtp_stream_wait_callback(&tscm->tx_stream,
						CALLBACK_TIMEOUT)) {
			err = -ETIMEDOUT;
			goto error;
		}
	}

	return 0;
error:
	amdtp_domain_stop(&tscm->domain);
	finish_session(tscm);

	return err;
}

void snd_tscm_stream_stop_duplex(struct snd_tscm *tscm)
{
	if (tscm->substreams_counter == 0) {
		amdtp_domain_stop(&tscm->domain);
		finish_session(tscm);

		fw_iso_resources_free(&tscm->tx_resources);
		fw_iso_resources_free(&tscm->rx_resources);
	}
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
