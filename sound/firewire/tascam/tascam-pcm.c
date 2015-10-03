/*
 * tascam-pcm.c - a part of driver for TASCAM FireWire series
 *
 * Copyright (c) 2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "tascam.h"

static void set_buffer_params(struct snd_pcm_hardware *hw)
{
	hw->period_bytes_min = 4 * hw->channels_min;
	hw->period_bytes_max = hw->period_bytes_min * 2048;
	hw->buffer_bytes_max = hw->period_bytes_max * 2;

	hw->periods_min = 2;
	hw->periods_max = UINT_MAX;
}

static int pcm_init_hw_params(struct snd_tscm *tscm,
			      struct snd_pcm_substream *substream)
{
	static const struct snd_pcm_hardware hardware = {
		.info = SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_BLOCK_TRANSFER |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_JOINT_DUPLEX |
			SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID,
		.rates = SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000 |
			 SNDRV_PCM_RATE_88200 |
			 SNDRV_PCM_RATE_96000,
		.rate_min = 44100,
		.rate_max = 96000,
		.channels_min = 10,
		.channels_max = 18,
	};
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct amdtp_stream *stream;
	unsigned int pcm_channels;

	runtime->hw = hardware;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S32;
		stream = &tscm->tx_stream;
		pcm_channels = tscm->spec->pcm_capture_analog_channels;
	} else {
		runtime->hw.formats =
				SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_S32;
		stream = &tscm->rx_stream;
		pcm_channels = tscm->spec->pcm_playback_analog_channels;
	}

	if (tscm->spec->has_adat)
		pcm_channels += 8;
	if (tscm->spec->has_spdif)
		pcm_channels += 2;
	runtime->hw.channels_min = runtime->hw.channels_max = pcm_channels;

	set_buffer_params(&runtime->hw);

	return amdtp_tscm_add_pcm_hw_constraints(stream, runtime);
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;
	enum snd_tscm_clock clock;
	unsigned int rate;
	int err;

	err = snd_tscm_stream_lock_try(tscm);
	if (err < 0)
		goto end;

	err = pcm_init_hw_params(tscm, substream);
	if (err < 0)
		goto err_locked;

	err = snd_tscm_stream_get_clock(tscm, &clock);
	if (clock != SND_TSCM_CLOCK_INTERNAL ||
	    amdtp_stream_pcm_running(&tscm->rx_stream) ||
	    amdtp_stream_pcm_running(&tscm->tx_stream)) {
		err = snd_tscm_stream_get_rate(tscm, &rate);
		if (err < 0)
			goto err_locked;
		substream->runtime->hw.rate_min = rate;
		substream->runtime->hw.rate_max = rate;
	}

	snd_pcm_set_sync(substream);
end:
	return err;
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

static int pcm_capture_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct snd_tscm *tscm = substream->private_data;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		mutex_lock(&tscm->mutex);
		tscm->substreams_counter++;
		mutex_unlock(&tscm->mutex);
	}

	amdtp_tscm_set_pcm_format(&tscm->tx_stream, params_format(hw_params));

	return 0;
}

static int pcm_playback_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *hw_params)
{
	struct snd_tscm *tscm = substream->private_data;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		mutex_lock(&tscm->mutex);
		tscm->substreams_counter++;
		mutex_unlock(&tscm->mutex);
	}

	amdtp_tscm_set_pcm_format(&tscm->rx_stream, params_format(hw_params));

	return 0;
}

static int pcm_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;

	mutex_lock(&tscm->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		tscm->substreams_counter--;

	snd_tscm_stream_stop_duplex(tscm);

	mutex_unlock(&tscm->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int pcm_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_tscm *tscm = substream->private_data;

	mutex_lock(&tscm->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		tscm->substreams_counter--;

	snd_tscm_stream_stop_duplex(tscm);

	mutex_unlock(&tscm->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
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

	return amdtp_stream_pcm_pointer(&tscm->tx_stream);
}

static snd_pcm_uframes_t pcm_playback_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_tscm *tscm = sbstrm->private_data;

	return amdtp_stream_pcm_pointer(&tscm->rx_stream);
}

static struct snd_pcm_ops pcm_capture_ops = {
	.open		= pcm_open,
	.close		= pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pcm_capture_hw_params,
	.hw_free	= pcm_capture_hw_free,
	.prepare	= pcm_capture_prepare,
	.trigger	= pcm_capture_trigger,
	.pointer	= pcm_capture_pointer,
	.page		= snd_pcm_lib_get_vmalloc_page,
};

static struct snd_pcm_ops pcm_playback_ops = {
	.open		= pcm_open,
	.close		= pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pcm_playback_hw_params,
	.hw_free	= pcm_playback_hw_free,
	.prepare	= pcm_playback_prepare,
	.trigger	= pcm_playback_trigger,
	.pointer	= pcm_playback_pointer,
	.page		= snd_pcm_lib_get_vmalloc_page,
	.mmap		= snd_pcm_lib_mmap_vmalloc,
};

int snd_tscm_create_pcm_devices(struct snd_tscm *tscm)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(tscm->card, tscm->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = tscm;
	snprintf(pcm->name, sizeof(pcm->name),
		 "%s PCM", tscm->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_capture_ops);

	return 0;
}
