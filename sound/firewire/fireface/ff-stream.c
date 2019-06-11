/*
 * ff-stream.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
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
	amdtp_stream_stop(&ff->tx_stream);
	amdtp_stream_stop(&ff->rx_stream);

	ff->spec->protocol->finish_session(ff);
	ff->spec->protocol->switch_fetching_mode(ff, false);
}

static int init_stream(struct snd_ff *ff, enum amdtp_stream_direction dir)
{
	int err;
	struct fw_iso_resources *resources;
	struct amdtp_stream *stream;

	if (dir == AMDTP_IN_STREAM) {
		resources = &ff->tx_resources;
		stream = &ff->tx_stream;
	} else {
		resources = &ff->rx_resources;
		stream = &ff->rx_stream;
	}

	err = fw_iso_resources_init(resources, ff->unit);
	if (err < 0)
		return err;

	err = amdtp_ff_init(stream, ff->unit, dir);
	if (err < 0)
		fw_iso_resources_destroy(resources);

	return err;
}

static void destroy_stream(struct snd_ff *ff, enum amdtp_stream_direction dir)
{
	if (dir == AMDTP_IN_STREAM) {
		amdtp_stream_destroy(&ff->tx_stream);
		fw_iso_resources_destroy(&ff->tx_resources);
	} else {
		amdtp_stream_destroy(&ff->rx_stream);
		fw_iso_resources_destroy(&ff->rx_resources);
	}
}

int snd_ff_stream_init_duplex(struct snd_ff *ff)
{
	int err;

	err = init_stream(ff, AMDTP_OUT_STREAM);
	if (err < 0)
		goto end;

	err = init_stream(ff, AMDTP_IN_STREAM);
	if (err < 0)
		destroy_stream(ff, AMDTP_OUT_STREAM);
end:
	return err;
}

/*
 * This function should be called before starting streams or after stopping
 * streams.
 */
void snd_ff_stream_destroy_duplex(struct snd_ff *ff)
{
	destroy_stream(ff, AMDTP_IN_STREAM);
	destroy_stream(ff, AMDTP_OUT_STREAM);
}

int snd_ff_stream_reserve_duplex(struct snd_ff *ff, unsigned int rate)
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
	}

	return 0;
}

void snd_ff_stream_release_duplex(struct snd_ff *ff)
{
	if (ff->substreams_counter == 0) {
		fw_iso_resources_free(&ff->tx_resources);
		fw_iso_resources_free(&ff->rx_resources);
	}
}

int snd_ff_stream_start_duplex(struct snd_ff *ff, unsigned int rate)
{
	int err;

	if (ff->substreams_counter == 0)
		return 0;

	if (amdtp_streaming_error(&ff->tx_stream) ||
	    amdtp_streaming_error(&ff->rx_stream))
		finish_session(ff);

	/*
	 * Regardless of current source of clock signal, drivers transfer some
	 * packets. Then, the device transfers packets.
	 */
	if (!amdtp_stream_running(&ff->rx_stream)) {
		err = ff->spec->protocol->begin_session(ff, rate);
		if (err < 0)
			goto error;

		err = amdtp_stream_start(&ff->rx_stream,
					 ff->rx_resources.channel,
					 fw_parent_device(ff->unit)->max_speed);
		if (err < 0)
			goto error;

		if (!amdtp_stream_wait_callback(&ff->rx_stream,
						CALLBACK_TIMEOUT_MS)) {
			err = -ETIMEDOUT;
			goto error;
		}

		err = ff->spec->protocol->switch_fetching_mode(ff, true);
		if (err < 0)
			goto error;
	}

	if (!amdtp_stream_running(&ff->tx_stream)) {
		err = amdtp_stream_start(&ff->tx_stream,
					 ff->tx_resources.channel,
					 fw_parent_device(ff->unit)->max_speed);
		if (err < 0)
			goto error;

		if (!amdtp_stream_wait_callback(&ff->tx_stream,
						CALLBACK_TIMEOUT_MS)) {
			err = -ETIMEDOUT;
			goto error;
		}
	}

	return 0;
error:
	finish_session(ff);

	return err;
}

void snd_ff_stream_stop_duplex(struct snd_ff *ff)
{
	if (ff->substreams_counter == 0)
		finish_session(ff);
}

void snd_ff_stream_update_duplex(struct snd_ff *ff)
{
	// The device discontinue to transfer packets.
	amdtp_stream_pcm_abort(&ff->tx_stream);
	amdtp_stream_stop(&ff->tx_stream);

	amdtp_stream_pcm_abort(&ff->rx_stream);
	amdtp_stream_stop(&ff->rx_stream);
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
