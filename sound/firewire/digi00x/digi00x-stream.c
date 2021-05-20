// SPDX-License-Identifier: GPL-2.0-only
/*
 * digi00x-stream.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 */

#include "digi00x.h"

#define READY_TIMEOUT_MS	500

const unsigned int snd_dg00x_stream_rates[SND_DG00X_RATE_COUNT] = {
	[SND_DG00X_RATE_44100] = 44100,
	[SND_DG00X_RATE_48000] = 48000,
	[SND_DG00X_RATE_88200] = 88200,
	[SND_DG00X_RATE_96000] = 96000,
};

/* Multi Bit Linear Audio data channels for each sampling transfer frequency. */
const unsigned int
snd_dg00x_stream_pcm_channels[SND_DG00X_RATE_COUNT] = {
	/* Analog/ADAT/SPDIF */
	[SND_DG00X_RATE_44100] = (8 + 8 + 2),
	[SND_DG00X_RATE_48000] = (8 + 8 + 2),
	/* Analog/SPDIF */
	[SND_DG00X_RATE_88200] = (8 + 2),
	[SND_DG00X_RATE_96000] = (8 + 2),
};

int snd_dg00x_stream_get_local_rate(struct snd_dg00x *dg00x, unsigned int *rate)
{
	u32 data;
	__be32 reg;
	int err;

	err = snd_fw_transaction(dg00x->unit, TCODE_READ_QUADLET_REQUEST,
				 DG00X_ADDR_BASE + DG00X_OFFSET_LOCAL_RATE,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	data = be32_to_cpu(reg) & 0x0f;
	if (data < ARRAY_SIZE(snd_dg00x_stream_rates))
		*rate = snd_dg00x_stream_rates[data];
	else
		err = -EIO;

	return err;
}

int snd_dg00x_stream_set_local_rate(struct snd_dg00x *dg00x, unsigned int rate)
{
	__be32 reg;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(snd_dg00x_stream_rates); i++) {
		if (rate == snd_dg00x_stream_rates[i])
			break;
	}
	if (i == ARRAY_SIZE(snd_dg00x_stream_rates))
		return -EINVAL;

	reg = cpu_to_be32(i);
	return snd_fw_transaction(dg00x->unit, TCODE_WRITE_QUADLET_REQUEST,
				  DG00X_ADDR_BASE + DG00X_OFFSET_LOCAL_RATE,
				  &reg, sizeof(reg), 0);
}

int snd_dg00x_stream_get_clock(struct snd_dg00x *dg00x,
			       enum snd_dg00x_clock *clock)
{
	__be32 reg;
	int err;

	err = snd_fw_transaction(dg00x->unit, TCODE_READ_QUADLET_REQUEST,
				 DG00X_ADDR_BASE + DG00X_OFFSET_CLOCK_SOURCE,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	*clock = be32_to_cpu(reg) & 0x0f;
	if (*clock >= SND_DG00X_CLOCK_COUNT)
		err = -EIO;

	return err;
}

int snd_dg00x_stream_check_external_clock(struct snd_dg00x *dg00x, bool *detect)
{
	__be32 reg;
	int err;

	err = snd_fw_transaction(dg00x->unit, TCODE_READ_QUADLET_REQUEST,
				 DG00X_ADDR_BASE + DG00X_OFFSET_DETECT_EXTERNAL,
				 &reg, sizeof(reg), 0);
	if (err >= 0)
		*detect = be32_to_cpu(reg) > 0;

	return err;
}

int snd_dg00x_stream_get_external_rate(struct snd_dg00x *dg00x,
				       unsigned int *rate)
{
	u32 data;
	__be32 reg;
	int err;

	err = snd_fw_transaction(dg00x->unit, TCODE_READ_QUADLET_REQUEST,
				 DG00X_ADDR_BASE + DG00X_OFFSET_EXTERNAL_RATE,
				 &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	data = be32_to_cpu(reg) & 0x0f;
	if (data < ARRAY_SIZE(snd_dg00x_stream_rates))
		*rate = snd_dg00x_stream_rates[data];
	/* This means desync. */
	else
		err = -EBUSY;

	return err;
}

static void finish_session(struct snd_dg00x *dg00x)
{
	__be32 data;

	data = cpu_to_be32(0x00000003);
	snd_fw_transaction(dg00x->unit, TCODE_WRITE_QUADLET_REQUEST,
			   DG00X_ADDR_BASE + DG00X_OFFSET_STREAMING_SET,
			   &data, sizeof(data), 0);

	// Unregister isochronous channels for both direction.
	data = 0;
	snd_fw_transaction(dg00x->unit, TCODE_WRITE_QUADLET_REQUEST,
			   DG00X_ADDR_BASE + DG00X_OFFSET_ISOC_CHANNELS,
			   &data, sizeof(data), 0);

	// Just after finishing the session, the device may lost transmitting
	// functionality for a short time.
	msleep(50);
}

static int begin_session(struct snd_dg00x *dg00x)
{
	__be32 data;
	u32 curr;
	int err;

	// Register isochronous channels for both direction.
	data = cpu_to_be32((dg00x->tx_resources.channel << 16) |
			   dg00x->rx_resources.channel);
	err = snd_fw_transaction(dg00x->unit, TCODE_WRITE_QUADLET_REQUEST,
				 DG00X_ADDR_BASE + DG00X_OFFSET_ISOC_CHANNELS,
				 &data, sizeof(data), 0);
	if (err < 0)
		return err;

	err = snd_fw_transaction(dg00x->unit, TCODE_READ_QUADLET_REQUEST,
				 DG00X_ADDR_BASE + DG00X_OFFSET_STREAMING_STATE,
				 &data, sizeof(data), 0);
	if (err < 0)
		return err;
	curr = be32_to_cpu(data);

	if (curr == 0)
		curr = 2;

	curr--;
	while (curr > 0) {
		data = cpu_to_be32(curr);
		err = snd_fw_transaction(dg00x->unit,
					 TCODE_WRITE_QUADLET_REQUEST,
					 DG00X_ADDR_BASE +
					 DG00X_OFFSET_STREAMING_SET,
					 &data, sizeof(data), 0);
		if (err < 0)
			break;

		msleep(20);
		curr--;
	}

	return err;
}

static int keep_resources(struct snd_dg00x *dg00x, struct amdtp_stream *stream,
			  unsigned int rate)
{
	struct fw_iso_resources *resources;
	int i;
	int err;

	// Check sampling rate.
	for (i = 0; i < SND_DG00X_RATE_COUNT; i++) {
		if (snd_dg00x_stream_rates[i] == rate)
			break;
	}
	if (i == SND_DG00X_RATE_COUNT)
		return -EINVAL;

	if (stream == &dg00x->tx_stream)
		resources = &dg00x->tx_resources;
	else
		resources = &dg00x->rx_resources;

	err = amdtp_dot_set_parameters(stream, rate,
				       snd_dg00x_stream_pcm_channels[i]);
	if (err < 0)
		return err;

	return fw_iso_resources_allocate(resources,
				amdtp_stream_get_max_payload(stream),
				fw_parent_device(dg00x->unit)->max_speed);
}

static int init_stream(struct snd_dg00x *dg00x, struct amdtp_stream *s)
{
	struct fw_iso_resources *resources;
	enum amdtp_stream_direction dir;
	int err;

	if (s == &dg00x->tx_stream) {
		resources = &dg00x->tx_resources;
		dir = AMDTP_IN_STREAM;
	} else {
		resources = &dg00x->rx_resources;
		dir = AMDTP_OUT_STREAM;
	}

	err = fw_iso_resources_init(resources, dg00x->unit);
	if (err < 0)
		return err;

	err = amdtp_dot_init(s, dg00x->unit, dir);
	if (err < 0)
		fw_iso_resources_destroy(resources);

	return err;
}

static void destroy_stream(struct snd_dg00x *dg00x, struct amdtp_stream *s)
{
	amdtp_stream_destroy(s);

	if (s == &dg00x->tx_stream)
		fw_iso_resources_destroy(&dg00x->tx_resources);
	else
		fw_iso_resources_destroy(&dg00x->rx_resources);
}

int snd_dg00x_stream_init_duplex(struct snd_dg00x *dg00x)
{
	int err;

	err = init_stream(dg00x, &dg00x->rx_stream);
	if (err < 0)
		return err;

	err = init_stream(dg00x, &dg00x->tx_stream);
	if (err < 0)
		destroy_stream(dg00x, &dg00x->rx_stream);

	err = amdtp_domain_init(&dg00x->domain);
	if (err < 0) {
		destroy_stream(dg00x, &dg00x->rx_stream);
		destroy_stream(dg00x, &dg00x->tx_stream);
	}

	return err;
}

/*
 * This function should be called before starting streams or after stopping
 * streams.
 */
void snd_dg00x_stream_destroy_duplex(struct snd_dg00x *dg00x)
{
	amdtp_domain_destroy(&dg00x->domain);

	destroy_stream(dg00x, &dg00x->rx_stream);
	destroy_stream(dg00x, &dg00x->tx_stream);
}

int snd_dg00x_stream_reserve_duplex(struct snd_dg00x *dg00x, unsigned int rate,
				    unsigned int frames_per_period,
				    unsigned int frames_per_buffer)
{
	unsigned int curr_rate;
	int err;

	err = snd_dg00x_stream_get_local_rate(dg00x, &curr_rate);
	if (err < 0)
		return err;
	if (rate == 0)
		rate = curr_rate;

	if (dg00x->substreams_counter == 0 || curr_rate != rate) {
		amdtp_domain_stop(&dg00x->domain);

		finish_session(dg00x);

		fw_iso_resources_free(&dg00x->tx_resources);
		fw_iso_resources_free(&dg00x->rx_resources);

		err = snd_dg00x_stream_set_local_rate(dg00x, rate);
		if (err < 0)
			return err;

		err = keep_resources(dg00x, &dg00x->rx_stream, rate);
		if (err < 0)
			return err;

		err = keep_resources(dg00x, &dg00x->tx_stream, rate);
		if (err < 0) {
			fw_iso_resources_free(&dg00x->rx_resources);
			return err;
		}

		err = amdtp_domain_set_events_per_period(&dg00x->domain,
					frames_per_period, frames_per_buffer);
		if (err < 0) {
			fw_iso_resources_free(&dg00x->rx_resources);
			fw_iso_resources_free(&dg00x->tx_resources);
			return err;
		}
	}

	return 0;
}

int snd_dg00x_stream_start_duplex(struct snd_dg00x *dg00x)
{
	unsigned int generation = dg00x->rx_resources.generation;
	int err = 0;

	if (dg00x->substreams_counter == 0)
		return 0;

	if (amdtp_streaming_error(&dg00x->tx_stream) ||
	    amdtp_streaming_error(&dg00x->rx_stream)) {
		amdtp_domain_stop(&dg00x->domain);
		finish_session(dg00x);
	}

	if (generation != fw_parent_device(dg00x->unit)->card->generation) {
		err = fw_iso_resources_update(&dg00x->tx_resources);
		if (err < 0)
			goto error;

		err = fw_iso_resources_update(&dg00x->rx_resources);
		if (err < 0)
			goto error;
	}

	/*
	 * No packets are transmitted without receiving packets, reagardless of
	 * which source of clock is used.
	 */
	if (!amdtp_stream_running(&dg00x->rx_stream)) {
		int spd = fw_parent_device(dg00x->unit)->max_speed;

		err = begin_session(dg00x);
		if (err < 0)
			goto error;

		err = amdtp_domain_add_stream(&dg00x->domain, &dg00x->rx_stream,
					      dg00x->rx_resources.channel, spd);
		if (err < 0)
			goto error;

		err = amdtp_domain_add_stream(&dg00x->domain, &dg00x->tx_stream,
					      dg00x->tx_resources.channel, spd);
		if (err < 0)
			goto error;

		err = amdtp_domain_start(&dg00x->domain, 0);
		if (err < 0)
			goto error;

		if (!amdtp_domain_wait_ready(&dg00x->domain, READY_TIMEOUT_MS)) {
			err = -ETIMEDOUT;
			goto error;
		}
	}

	return 0;
error:
	amdtp_domain_stop(&dg00x->domain);
	finish_session(dg00x);

	return err;
}

void snd_dg00x_stream_stop_duplex(struct snd_dg00x *dg00x)
{
	if (dg00x->substreams_counter == 0) {
		amdtp_domain_stop(&dg00x->domain);
		finish_session(dg00x);

		fw_iso_resources_free(&dg00x->tx_resources);
		fw_iso_resources_free(&dg00x->rx_resources);
	}
}

void snd_dg00x_stream_update_duplex(struct snd_dg00x *dg00x)
{
	fw_iso_resources_update(&dg00x->tx_resources);
	fw_iso_resources_update(&dg00x->rx_resources);

	amdtp_stream_update(&dg00x->tx_stream);
	amdtp_stream_update(&dg00x->rx_stream);
}

void snd_dg00x_stream_lock_changed(struct snd_dg00x *dg00x)
{
	dg00x->dev_lock_changed = true;
	wake_up(&dg00x->hwdep_wait);
}

int snd_dg00x_stream_lock_try(struct snd_dg00x *dg00x)
{
	int err;

	spin_lock_irq(&dg00x->lock);

	/* user land lock this */
	if (dg00x->dev_lock_count < 0) {
		err = -EBUSY;
		goto end;
	}

	/* this is the first time */
	if (dg00x->dev_lock_count++ == 0)
		snd_dg00x_stream_lock_changed(dg00x);
	err = 0;
end:
	spin_unlock_irq(&dg00x->lock);
	return err;
}

void snd_dg00x_stream_lock_release(struct snd_dg00x *dg00x)
{
	spin_lock_irq(&dg00x->lock);

	if (WARN_ON(dg00x->dev_lock_count <= 0))
		goto end;
	if (--dg00x->dev_lock_count == 0)
		snd_dg00x_stream_lock_changed(dg00x);
end:
	spin_unlock_irq(&dg00x->lock);
}
