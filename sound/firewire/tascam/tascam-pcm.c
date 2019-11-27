// SPDX-License-Identifier: GPL-2.0-only
/*
 * tascam-pcm.c - a part of driver for TASCAM FireWire series
 *
 * Copyright (c) 2015 Takashi Sakamoto
 */

#include "tascam.h"

static int pcm_init_hw_params(struct snd_tscm *tscm,
			      struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	struct amdtp_stream *stream;
	unsigned int pcm_channels;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S32;
		stream = &tscm->tx_stream;
		pcm_channels = tscm->spec->pcm_capture_analog_channels;
	} else {
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S32;
		stream = &tscm->rx_stream;
		pcm_channels = tscm->spec->pcm_playback_analog_channels;
	}

	if (tscm->spec->has_adat)
		pcm_channels += 8;
	if (tscm->spec->has_spdif)
		pcm_channels += 2;
	runtime->hw.channels_min = runtime->hw.channels_max = pcm_channels;

	hw->rates = SNDRV_PCM_RATE_44100 |
		    SNDRV_PCM_RATE_48000 |
		    SNDRV_PCM_RATE_88200 |
		    SNDRV_PCM_RATE_96000;
	snd_pcm_limit_hw_rates(runtime);

	return amdtp_tscm_add_pcm_hw_constraints(stream, runtime);
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;
	struct amdtp_domain *d = &tscm->domain;
	enum snd_tscm_clock clock;
	int err;

	err = snd_tscm_stream_lock_try(tscm);
	if (err < 0)
		return err;

	err = pcm_init_hw_params(tscm, substream);
	if (err < 0)
		goto err_locked;

	err = snd_tscm_stream_get_clock(tscm, &clock);
	if (err < 0)
		goto err_locked;

	mutex_lock(&tscm->mutex);

	// When source of clock is not internal or any stream is reserved for
	// transmission of PCM frames, the available sampling rate is limited
	// at current one.
	if (clock != SND_TSCM_CLOCK_INTERNAL || tscm->substreams_counter > 0) {
		unsigned int frames_per_period = d->events_per_period;
		unsigned int frames_per_buffer = d->events_per_buffer;
		unsigned int rate;

		err = snd_tscm_stream_get_rate(tscm, &rate);
		if (err < 0) {
			mutex_unlock(&tscm->mutex);
			goto err_locked;
		}
		substream->runtime->hw.rate_min = rate;
		substream->runtime->hw.rate_max = rate;

		err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
					frames_per_period, frames_per_period);
		if (err < 0) {
			mutex_unlock(&tscm->mutex);
			goto err_locked;
		}

		err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
					frames_per_buffer, frames_per_buffer);
		if (err < 0) {
			mutex_unlock(&tscm->mutex);
			goto err_locked;
		}
	}

	mutex_unlock(&tscm->mutex);

	snd_pcm_set_sync(substream);

	return 0;
err_locked:
	snd_tscm_stream_lock_release(tscm);
	return err;
}

static int pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;

	snd_tscm_stream_lock_release(tscm);

	return 0;
}

static int pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	struct snd_tscm *tscm = substream->private_data;
	int err;

	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		unsigned int rate = params_rate(hw_params);
		unsigned int frames_per_period = params_period_size(hw_params);
		unsigned int frames_per_buffer = params_buffer_size(hw_params);

		mutex_lock(&tscm->mutex);
		err = snd_tscm_stream_reserve_duplex(tscm, rate,
					frames_per_period, frames_per_buffer);
		if (err >= 0)
			++tscm->substreams_counter;
		mutex_unlock(&tscm->mutex);
	}

	return err;
}

static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;

	mutex_lock(&tscm->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		--tscm->substreams_counter;

	snd_tscm_stream_stop_duplex(tscm);

	mutex_unlock(&tscm->mutex);

	return snd_pcm_lib_free_pages(substream);
}

static int pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	mutex_lock(&tscm->mutex);

	err = snd_tscm_stream_start_duplex(tscm, runtime->rate);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&tscm->tx_stream);

	mutex_unlock(&tscm->mutex);

	return err;
}

static int pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	mutex_lock(&tscm->mutex);

	err = snd_tscm_stream_start_duplex(tscm, runtime->rate);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&tscm->rx_stream);

	mutex_unlock(&tscm->mutex);

	return err;
}

static int pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_tscm *tscm = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&tscm->tx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&tscm->tx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_tscm *tscm = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&tscm->rx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&tscm->rx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t pcm_capture_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_tscm *tscm = sbstrm->private_data;

	return amdtp_domain_stream_pcm_pointer(&tscm->domain, &tscm->tx_stream);
}

static snd_pcm_uframes_t pcm_playback_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_tscm *tscm = sbstrm->private_data;

	return amdtp_domain_stream_pcm_pointer(&tscm->domain, &tscm->rx_stream);
}

static int pcm_capture_ack(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;

	return amdtp_domain_stream_pcm_ack(&tscm->domain, &tscm->tx_stream);
}

static int pcm_playback_ack(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;

	return amdtp_domain_stream_pcm_ack(&tscm->domain, &tscm->rx_stream);
}

int snd_tscm_create_pcm_devices(struct snd_tscm *tscm)
{
	static const struct snd_pcm_ops capture_ops = {
		.open		= pcm_open,
		.close		= pcm_close,
		.ioctl		= snd_pcm_lib_ioctl,
		.hw_params	= pcm_hw_params,
		.hw_free	= pcm_hw_free,
		.prepare	= pcm_capture_prepare,
		.trigger	= pcm_capture_trigger,
		.pointer	= pcm_capture_pointer,
		.ack		= pcm_capture_ack,
	};
	static const struct snd_pcm_ops playback_ops = {
		.open		= pcm_open,
		.close		= pcm_close,
		.ioctl		= snd_pcm_lib_ioctl,
		.hw_params	= pcm_hw_params,
		.hw_free	= pcm_hw_free,
		.prepare	= pcm_playback_prepare,
		.trigger	= pcm_playback_trigger,
		.pointer	= pcm_playback_pointer,
		.ack		= pcm_playback_ack,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(tscm->card, tscm->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = tscm;
	snprintf(pcm->name, sizeof(pcm->name),
		 "%s PCM", tscm->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &capture_ops);
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_VMALLOC,
					      NULL, 0, 0);

	return 0;
}
