/*
 * ff-stream.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "ff.h"

#define CALLBACK_TIMEOUT_MS	200

static int get_rate_mode(unsigned int rate, unsigned int *mode)
{
	int i;

	for (i = 0; i < CIP_SFC_COUNT; i++) {
		if (amdtp_rate_table[i] == rate)
			break;
	}

	if (i == CIP_SFC_COUNT)
		return -EINVAL;

	*mode = ((int)i - 1) / 2;

	return 0;
}

/*
 * Fireface 400 manages isochronous channel number in 3 bit field. Therefore,
 * we can allocate between 0 and 7 channel.
 */
static int keep_resources(struct snd_ff *ff, unsigned int rate)
{
	int mode;
	int err;

	err = get_rate_mode(rate, &mode);
	if (err < 0)
		return err;

	/* Keep resources for in-stream. */
	err = amdtp_ff_set_parameters(&ff->tx_stream, rate,
				      ff->spec->pcm_capture_channels[mode]);
	if (err < 0)
		return err;
	ff->tx_resources.channels_mask = 0x00000000000000ffuLL;
	err = fw_iso_resources_allocate(&ff->tx_resources,
			amdtp_stream_get_max_payload(&ff->tx_stream),
			fw_parent_device(ff->unit)->max_speed);
	if (err < 0)
		return err;

	/* Keep resources for out-stream. */
	err = amdtp_ff_set_parameters(&ff->rx_stream, rate,
				      ff->spec->pcm_playback_channels[mode]);
	if (err < 0)
		return err;
	ff->rx_resources.channels_mask = 0x00000000000000ffuLL;
	err = fw_iso_resources_allocate(&ff->rx_resources,
			amdtp_stream_get_max_payload(&ff->rx_stream),
			fw_parent_device(ff->unit)->max_speed);
	if (err < 0)
		fw_iso_resources_free(&ff->tx_resources);

	return err;
}

static void release_resources(struct snd_ff *ff)
{
	fw_iso_resources_free(&ff->tx_resources);
	fw_iso_resources_free(&ff->rx_resources);
}

static inline void finish_session(struct snd_ff *ff)
{
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

int snd_ff_stream_start_duplex(struct snd_ff *ff, unsigned int rate)
{
	unsigned int curr_rate;
	enum snd_ff_clock_src src;
	int err;

	if (ff->substreams_counter == 0)
		return 0;

	err = ff->spec->protocol->get_clock(ff, &curr_rate, &src);
	if (err < 0)
		return err;
	if (curr_rate != rate ||
	    amdtp_streaming_error(&ff->tx_stream) ||
	    amdtp_streaming_error(&ff->rx_stream)) {
		finish_session(ff);

		amdtp_stream_stop(&ff->tx_stream);
		amdtp_stream_stop(&ff->rx_stream);

		release_resources(ff);
	}

	/*
	 * Regardless of current source of clock signal, drivers transfer some
	 * packets. Then, the device transfers packets.
	 */
	if (!amdtp_stream_running(&ff->rx_stream)) {
		err = keep_resources(ff, rate);
		if (err < 0)
			goto error;

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
	amdtp_stream_stop(&ff->tx_stream);
	amdtp_stream_stop(&ff->rx_stream);

	finish_session(ff);
	release_resources(ff);

	return err;
}

void snd_ff_stream_stop_duplex(struct snd_ff *ff)
{
	if (ff->substreams_counter > 0)
		return;

	amdtp_stream_stop(&ff->tx_stream);
	amdtp_stream_stop(&ff->rx_stream);
	finish_session(ff);
	release_resources(ff);
}

void snd_ff_stream_update_duplex(struct snd_ff *ff)
{
	/* The device discontinue to transfer packets.  */
	amdtp_stream_pcm_abort(&ff->tx_stream);
	amdtp_stream_stop(&ff->tx_stream);

	amdtp_stream_pcm_abort(&ff->rx_stream);
	amdtp_stream_stop(&ff->rx_stream);

	fw_iso_resources_update(&ff->tx_resources);
	fw_iso_resources_update(&ff->rx_resources);
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
