// SPDX-License-Identifier: GPL-2.0-only
/*
 * ff-stream.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 */

#include "ff.h"

#define CALLBACK_TIMEOUT_MS	200

int snd_ff_stream_get_multiplier_mode(enum cip_sfc sfc,
				      enum snd_ff_stream_mode *mode)
{
	static const enum snd_ff_stream_mode modes[] = {
		[CIP_SFC_32000] = SND_FF_STREAM_MODE_LOW,
		[CIP_SFC_44100] = SND_FF_STREAM_MODE_LOW,
		[CIP_SFC_48000] = SND_FF_STREAM_MODE_LOW,
		[CIP_SFC_88200] = SND_FF_STREAM_MODE_MID,
		[CIP_SFC_96000] = SND_FF_STREAM_MODE_MID,
		[CIP_SFC_176400] = SND_FF_STREAM_MODE_HIGH,
		[CIP_SFC_192000] = SND_FF_STREAM_MODE_HIGH,
	};

	if (sfc >= CIP_SFC_COUNT)
		return -EINVAL;

	*mode = modes[sfc];

	return 0;
}

static inline void finish_session(struct snd_ff *ff)
{
	ff->spec->protocol->finish_session(ff);
	ff->spec->protocol->switch_fetching_mode(ff, false);
}

static int init_stream(struct snd_ff *ff, struct amdtp_stream *s)
{
	struct fw_iso_resources *resources;
	enum amdtp_stream_direction dir;
	int err;

	if (s == &ff->tx_stream) {
		resources = &ff->tx_resources;
		dir = AMDTP_IN_STREAM;
	} else {
		resources = &ff->rx_resources;
		dir = AMDTP_OUT_STREAM;
	}

	err = fw_iso_resources_init(resources, ff->unit);
	if (err < 0)
		return err;

	err = amdtp_ff_init(s, ff->unit, dir);
	if (err < 0)
		fw_iso_resources_destroy(resources);

	return err;
}

static void destroy_stream(struct snd_ff *ff, struct amdtp_stream *s)
{
	amdtp_stream_destroy(s);

	if (s == &ff->tx_stream)
		fw_iso_resources_destroy(&ff->tx_resources);
	else
		fw_iso_resources_destroy(&ff->rx_resources);
}

int snd_ff_stream_init_duplex(struct snd_ff *ff)
{
	int err;

	err = init_stream(ff, &ff->rx_stream);
	if (err < 0)
		return err;

	err = init_stream(ff, &ff->tx_stream);
	if (err < 0) {
		destroy_stream(ff, &ff->rx_stream);
		return err;
	}

	err = amdtp_domain_init(&ff->domain);
	if (err < 0) {
		destroy_stream(ff, &ff->rx_stream);
		destroy_stream(ff, &ff->tx_stream);
	}

	return err;
}

/*
 * This function should be called before starting streams or after stopping
 * streams.
 */
void snd_ff_stream_destroy_duplex(struct snd_ff *ff)
{
	amdtp_domain_destroy(&ff->domain);

	destroy_stream(ff, &ff->rx_stream);
	destroy_stream(ff, &ff->tx_stream);
}

int snd_ff_stream_reserve_duplex(struct snd_ff *ff, unsigned int rate,
				 unsigned int frames_per_period,
				 unsigned int frames_per_buffer)
{
	unsigned int curr_rate;
	enum snd_ff_clock_src src;
	int err;

	err = ff->spec->protocol->get_clock(ff, &curr_rate, &src);
	if (err < 0)
		return err;

	if (ff->substreams_counter == 0 || curr_rate != rate) {
		enum snd_ff_stream_mode mode;
		int i;

		amdtp_domain_stop(&ff->domain);
		finish_session(ff);

		fw_iso_resources_free(&ff->tx_resources);
		fw_iso_resources_free(&ff->rx_resources);

		for (i = 0; i < CIP_SFC_COUNT; ++i) {
			if (amdtp_rate_table[i] == rate)
				break;
		}
		if (i >= CIP_SFC_COUNT)
			return -EINVAL;

		err = snd_ff_stream_get_multiplier_mode(i, &mode);
		if (err < 0)
			return err;

		err = amdtp_ff_set_parameters(&ff->tx_stream, rate,
					ff->spec->pcm_capture_channels[mode]);
		if (err < 0)
			return err;

		err = amdtp_ff_set_parameters(&ff->rx_stream, rate,
					ff->spec->pcm_playback_channels[mode]);
		if (err < 0)
			return err;

		err = ff->spec->protocol->allocate_resources(ff, rate);
		if (err < 0)
			return err;

		err = amdtp_domain_set_events_per_period(&ff->domain,
					frames_per_period, frames_per_buffer);
		if (err < 0) {
			fw_iso_resources_free(&ff->tx_resources);
			fw_iso_resources_free(&ff->rx_resources);
			return err;
		}
	}

	return 0;
}

int snd_ff_stream_start_duplex(struct snd_ff *ff, unsigned int rate)
{
	int err;

	if (ff->substreams_counter == 0)
		return 0;

	if (amdtp_streaming_error(&ff->tx_stream) ||
	    amdtp_streaming_error(&ff->rx_stream)) {
		amdtp_domain_stop(&ff->domain);
		finish_session(ff);
	}

	/*
	 * Regardless of current source of clock signal, drivers transfer some
	 * packets. Then, the device transfers packets.
	 */
	if (!amdtp_stream_running(&ff->rx_stream)) {
		int spd = fw_parent_device(ff->unit)->max_speed;
		unsigned int ir_delay_cycle;

		err = ff->spec->protocol->begin_session(ff, rate);
		if (err < 0)
			goto error;

		err = amdtp_domain_add_stream(&ff->domain, &ff->rx_stream,
					      ff->rx_resources.channel, spd);
		if (err < 0)
			goto error;

		err = amdtp_domain_add_stream(&ff->domain, &ff->tx_stream,
					      ff->tx_resources.channel, spd);
		if (err < 0)
			goto error;

		// The device postpones start of transmission mostly for several
		// cycles after receiving packets firstly.
		if (ff->spec->protocol == &snd_ff_protocol_ff800)
			ir_delay_cycle = 800;	// = 100 msec
		else
			ir_delay_cycle = 16;	// = 2 msec

		err = amdtp_domain_start(&ff->domain, ir_delay_cycle);
		if (err < 0)
			goto error;

		if (!amdtp_stream_wait_callback(&ff->rx_stream,
						CALLBACK_TIMEOUT_MS) ||
		    !amdtp_stream_wait_callback(&ff->tx_stream,
						CALLBACK_TIMEOUT_MS)) {
			err = -ETIMEDOUT;
			goto error;
		}

		err = ff->spec->protocol->switch_fetching_mode(ff, true);
		if (err < 0)
			goto error;
	}

	return 0;
error:
	amdtp_domain_stop(&ff->domain);
	finish_session(ff);

	return err;
}

void snd_ff_stream_stop_duplex(struct snd_ff *ff)
{
	if (ff->substreams_counter == 0) {
		amdtp_domain_stop(&ff->domain);
		finish_session(ff);

		fw_iso_resources_free(&ff->tx_resources);
		fw_iso_resources_free(&ff->rx_resources);
	}
}

void snd_ff_stream_update_duplex(struct snd_ff *ff)
{
	amdtp_domain_stop(&ff->domain);

	// The device discontinue to transfer packets.
	amdtp_stream_pcm_abort(&ff->tx_stream);
	amdtp_stream_pcm_abort(&ff->rx_stream);
}

void snd_ff_stream_lock_changed(struct snd_ff *ff)
{
	ff->dev_lock_changed = true;
	wake_up(&ff->hwdep_wait);
}

int snd_ff_stream_lock_try(struct snd_ff *ff)
{
	int err;

	spin_lock_irq(&ff->lock);

	/* user land lock this */
	if (ff->dev_lock_count < 0) {
		err = -EBUSY;
		goto end;
	}

	/* this is the first time */
	if (ff->dev_lock_count++ == 0)
		snd_ff_stream_lock_changed(ff);
	err = 0;
end:
	spin_unlock_irq(&ff->lock);
	return err;
}

void snd_ff_stream_lock_release(struct snd_ff *ff)
{
	spin_lock_irq(&ff->lock);

	if (WARN_ON(ff->dev_lock_count <= 0))
		goto end;
	if (--ff->dev_lock_count == 0)
		snd_ff_stream_lock_changed(ff);
end:
	spin_unlock_irq(&ff->lock);
}
