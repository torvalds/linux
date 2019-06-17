/*
 * fireworks_pcm.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */
#include "./fireworks.h"

/*
 * NOTE:
 * Fireworks changes its AMDTP channels for PCM data according to its sampling
 * rate. There are three modes. Here _XX is either _rx or _tx.
 *  0:  32.0- 48.0 kHz then snd_efw_hwinfo.amdtp_XX_pcm_channels applied
 *  1:  88.2- 96.0 kHz then snd_efw_hwinfo.amdtp_XX_pcm_channels_2x applied
 *  2: 176.4-192.0 kHz then snd_efw_hwinfo.amdtp_XX_pcm_channels_4x applied
 *
 * The number of PCM channels for analog input and output are always fixed but
 * the number of PCM channels for digital input and output are differed.
 *
 * Additionally, according to "AudioFire Owner's Manual Version 2.2", in some
 * model, the number of PCM channels for digital input has more restriction
 * depending on which digital interface is selected.
 *  - S/PDIF coaxial and optical	: use input 1-2
 *  - ADAT optical at 32.0-48.0 kHz	: use input 1-8
 *  - ADAT optical at 88.2-96.0 kHz	: use input 1-4 (S/MUX format)
 *
 * The data in AMDTP channels for blank PCM channels are zero.
 */
static const unsigned int freq_table[] = {
	/* multiplier mode 0 */
	[0] = 32000,
	[1] = 44100,
	[2] = 48000,
	/* multiplier mode 1 */
	[3] = 88200,
	[4] = 96000,
	/* multiplier mode 2 */
	[5] = 176400,
	[6] = 192000,
};

static inline unsigned int
get_multiplier_mode_with_index(unsigned int index)
{
	return ((int)index - 1) / 2;
}

int snd_efw_get_multiplier_mode(unsigned int sampling_rate, unsigned int *mode)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(freq_table); i++) {
		if (freq_table[i] == sampling_rate) {
			*mode = get_multiplier_mode_with_index(i);
			return 0;
		}
	}

	return -EINVAL;
}

static int
hw_rule_rate(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule)
{
	unsigned int *pcm_channels = rule->private;
	struct snd_interval *r =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, mode;

	for (i = 0; i < ARRAY_SIZE(freq_table); i++) {
		mode = get_multiplier_mode_with_index(i);
		if (!snd_interval_test(c, pcm_channels[mode]))
			continue;

		t.min = min(t.min, freq_table[i]);
		t.max = max(t.max, freq_table[i]);
	}

	return snd_interval_refine(r, &t);
}

static int
hw_rule_channels(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule)
{
	unsigned int *pcm_channels = rule->private;
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i, mode;

	for (i = 0; i < ARRAY_SIZE(freq_table); i++) {
		mode = get_multiplier_mode_with_index(i);
		if (!snd_interval_test(r, freq_table[i]))
			continue;

		t.min = min(t.min, pcm_channels[mode]);
		t.max = max(t.max, pcm_channels[mode]);
	}

	return snd_interval_refine(c, &t);
}

static void
limit_channels(struct snd_pcm_hardware *hw, unsigned int *pcm_channels)
{
	unsigned int i, mode;

	hw->channels_min = UINT_MAX;
	hw->channels_max = 0;

	for (i = 0; i < ARRAY_SIZE(freq_table); i++) {
		mode = get_multiplier_mode_with_index(i);
		if (pcm_channels[mode] == 0)
			continue;

		hw->channels_min = min(hw->channels_min, pcm_channels[mode]);
		hw->channels_max = max(hw->channels_max, pcm_channels[mode]);
	}
}

static int
pcm_init_hw_params(struct snd_efw *efw,
		   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct amdtp_stream *s;
	unsigned int *pcm_channels;
	int err;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		runtime->hw.formats = AM824_IN_PCM_FORMAT_BITS;
		s = &efw->tx_stream;
		pcm_channels = efw->pcm_capture_channels;
	} else {
		runtime->hw.formats = AM824_OUT_PCM_FORMAT_BITS;
		s = &efw->rx_stream;
		pcm_channels = efw->pcm_playback_channels;
	}

	/* limit rates */
	runtime->hw.rates = efw->supported_sampling_rate,
	snd_pcm_limit_hw_rates(runtime);

	limit_channels(&runtime->hw, pcm_channels);

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  hw_rule_channels, pcm_channels,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		goto end;

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  hw_rule_rate, pcm_channels,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		goto end;

	err = amdtp_am824_add_pcm_hw_constraints(s, runtime);
end:
	return err;
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;
	unsigned int sampling_rate;
	enum snd_efw_clock_source clock_source;
	int err;

	err = snd_efw_stream_lock_try(efw);
	if (err < 0)
		goto end;

	err = pcm_init_hw_params(efw, substream);
	if (err < 0)
		goto err_locked;

	err = snd_efw_command_get_clock_source(efw, &clock_source);
	if (err < 0)
		goto err_locked;

	/*
	 * When source of clock is not internal or any PCM streams are running,
	 * available sampling rate is limited at current sampling rate.
	 */
	if ((clock_source != SND_EFW_CLOCK_SOURCE_INTERNAL) ||
	    amdtp_stream_pcm_running(&efw->tx_stream) ||
	    amdtp_stream_pcm_running(&efw->rx_stream)) {
		err = snd_efw_command_get_sampling_rate(efw, &sampling_rate);
		if (err < 0)
			goto err_locked;
		substream->runtime->hw.rate_min = sampling_rate;
		substream->runtime->hw.rate_max = sampling_rate;
	}

	snd_pcm_set_sync(substream);
end:
	return err;
err_locked:
	snd_efw_stream_lock_release(efw);
	return err;
}

static int pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;
	snd_efw_stream_lock_release(efw);
	return 0;
}

static int pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct snd_efw *efw = substream->private_data;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		unsigned int rate = params_rate(hw_params);

		mutex_lock(&efw->mutex);
		err = snd_efw_stream_reserve_duplex(efw, rate);
		if (err >= 0)
			++efw->substreams_counter;
		mutex_unlock(&efw->mutex);
	}

	return err;
}

static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;

	mutex_lock(&efw->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		--efw->substreams_counter;

	snd_efw_stream_stop_duplex(efw);

	mutex_unlock(&efw->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;
	int err;

	err = snd_efw_stream_start_duplex(efw);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&efw->tx_stream);

	return err;
}
static int pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;
	int err;

	err = snd_efw_stream_start_duplex(efw);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&efw->rx_stream);

	return err;
}

static int pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_efw *efw = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&efw->tx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&efw->tx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
static int pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_efw *efw = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&efw->rx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&efw->rx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t pcm_capture_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_efw *efw = sbstrm->private_data;
	return amdtp_stream_pcm_pointer(&efw->tx_stream);
}
static snd_pcm_uframes_t pcm_playback_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_efw *efw = sbstrm->private_data;
	return amdtp_stream_pcm_pointer(&efw->rx_stream);
}

static int pcm_capture_ack(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;

	return amdtp_stream_pcm_ack(&efw->tx_stream);
}

static int pcm_playback_ack(struct snd_pcm_substream *substream)
{
	struct snd_efw *efw = substream->private_data;

	return amdtp_stream_pcm_ack(&efw->rx_stream);
}

int snd_efw_create_pcm_devices(struct snd_efw *efw)
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
		.page		= snd_pcm_lib_get_vmalloc_page,
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
		.page		= snd_pcm_lib_get_vmalloc_page,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(efw->card, efw->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		goto end;

	pcm->private_data = efw;
	snprintf(pcm->name, sizeof(pcm->name), "%s PCM", efw->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &capture_ops);
end:
	return err;
}

