/*
 * dice_pcm.c - a part of driver for DICE based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2014 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "dice.h"

static int dice_rate_constraint(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_pcm_substream *substream = rule->private;
	struct snd_dice *dice = substream->private_data;
	unsigned int index = substream->pcm->device;

	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval rates = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int *pcm_channels;
	enum snd_dice_rate_mode mode;
	unsigned int i, rate;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		pcm_channels = dice->tx_pcm_chs[index];
	else
		pcm_channels = dice->rx_pcm_chs[index];

	for (i = 0; i < ARRAY_SIZE(snd_dice_rates); ++i) {
		rate = snd_dice_rates[i];
		if (snd_dice_stream_get_rate_mode(dice, rate, &mode) < 0)
			continue;

		if (!snd_interval_test(c, pcm_channels[mode]))
			continue;

		rates.min = min(rates.min, rate);
		rates.max = max(rates.max, rate);
	}

	return snd_interval_refine(r, &rates);
}

static int dice_channels_constraint(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct snd_pcm_substream *substream = rule->private;
	struct snd_dice *dice = substream->private_data;
	unsigned int index = substream->pcm->device;

	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval channels = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int *pcm_channels;
	enum snd_dice_rate_mode mode;
	unsigned int i, rate;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		pcm_channels = dice->tx_pcm_chs[index];
	else
		pcm_channels = dice->rx_pcm_chs[index];

	for (i = 0; i < ARRAY_SIZE(snd_dice_rates); ++i) {
		rate = snd_dice_rates[i];
		if (snd_dice_stream_get_rate_mode(dice, rate, &mode) < 0)
			continue;

		if (!snd_interval_test(r, rate))
			continue;

		channels.min = min(channels.min, pcm_channels[mode]);
		channels.max = max(channels.max, pcm_channels[mode]);
	}

	return snd_interval_refine(c, &channels);
}

static int limit_channels_and_rates(struct snd_dice *dice,
				    struct snd_pcm_runtime *runtime,
				    enum amdtp_stream_direction dir,
				    unsigned int index)
{
	struct snd_pcm_hardware *hw = &runtime->hw;
	unsigned int *pcm_channels;
	unsigned int i;

	if (dir == AMDTP_IN_STREAM)
		pcm_channels = dice->tx_pcm_chs[index];
	else
		pcm_channels = dice->rx_pcm_chs[index];

	hw->channels_min = UINT_MAX;
	hw->channels_max = 0;

	for (i = 0; i < ARRAY_SIZE(snd_dice_rates); ++i) {
		enum snd_dice_rate_mode mode;
		unsigned int rate, channels;

		rate = snd_dice_rates[i];
		if (snd_dice_stream_get_rate_mode(dice, rate, &mode) < 0)
			continue;
		hw->rates |= snd_pcm_rate_to_rate_bit(rate);

		channels = pcm_channels[mode];
		if (channels == 0)
			continue;
		hw->channels_min = min(hw->channels_min, channels);
		hw->channels_max = max(hw->channels_max, channels);
	}

	snd_pcm_limit_hw_rates(runtime);

	return 0;
}

static int init_hw_info(struct snd_dice *dice,
			struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	unsigned int index = substream->pcm->device;
	enum amdtp_stream_direction dir;
	struct amdtp_stream *stream;
	int err;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		hw->formats = AM824_IN_PCM_FORMAT_BITS;
		dir = AMDTP_IN_STREAM;
		stream = &dice->tx_stream[index];
	} else {
		hw->formats = AM824_OUT_PCM_FORMAT_BITS;
		dir = AMDTP_OUT_STREAM;
		stream = &dice->rx_stream[index];
	}

	err = limit_channels_and_rates(dice, substream->runtime, dir,
				       index);
	if (err < 0)
		return err;

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  dice_rate_constraint, substream,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  dice_channels_constraint, substream,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;

	return amdtp_am824_add_pcm_hw_constraints(stream, runtime);
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	unsigned int source;
	bool internal;
	int err;

	err = snd_dice_stream_lock_try(dice);
	if (err < 0)
		goto end;

	err = init_hw_info(dice, substream);
	if (err < 0)
		goto err_locked;

	err = snd_dice_transaction_get_clock_source(dice, &source);
	if (err < 0)
		goto err_locked;
	switch (source) {
	case CLOCK_SOURCE_AES1:
	case CLOCK_SOURCE_AES2:
	case CLOCK_SOURCE_AES3:
	case CLOCK_SOURCE_AES4:
	case CLOCK_SOURCE_AES_ANY:
	case CLOCK_SOURCE_ADAT:
	case CLOCK_SOURCE_TDIF:
	case CLOCK_SOURCE_WC:
		internal = false;
		break;
	default:
		internal = true;
		break;
	}

	/*
	 * When source of clock is not internal or any PCM streams are running,
	 * available sampling rate is limited at current sampling rate.
	 */
	if (!internal ||
	    amdtp_stream_pcm_running(&dice->tx_stream[0]) ||
	    amdtp_stream_pcm_running(&dice->tx_stream[1]) ||
	    amdtp_stream_pcm_running(&dice->rx_stream[0]) ||
	    amdtp_stream_pcm_running(&dice->rx_stream[1])) {
		unsigned int rate;

		err = snd_dice_transaction_get_rate(dice, &rate);
		if (err < 0)
			goto err_locked;
		substream->runtime->hw.rate_min = rate;
		substream->runtime->hw.rate_max = rate;
	}

	snd_pcm_set_sync(substream);
end:
	return err;
err_locked:
	snd_dice_stream_lock_release(dice);
	return err;
}

static int pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;

	snd_dice_stream_lock_release(dice);

	return 0;
}

static int capture_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hw_params)
{
	struct snd_dice *dice = substream->private_data;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		unsigned int rate = params_rate(hw_params);

		mutex_lock(&dice->mutex);
		err = snd_dice_stream_reserve_duplex(dice, rate);
		if (err >= 0)
			++dice->substreams_counter;
		mutex_unlock(&dice->mutex);
	}

	return err;
}
static int playback_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *hw_params)
{
	struct snd_dice *dice = substream->private_data;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		unsigned int rate = params_rate(hw_params);

		mutex_lock(&dice->mutex);
		err = snd_dice_stream_reserve_duplex(dice, rate);
		if (err >= 0)
			++dice->substreams_counter;
		mutex_unlock(&dice->mutex);
	}

	return err;
}

static int capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;

	mutex_lock(&dice->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		--dice->substreams_counter;

	snd_dice_stream_stop_duplex(dice);
	snd_dice_stream_release_duplex(dice);

	mutex_unlock(&dice->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;

	mutex_lock(&dice->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		--dice->substreams_counter;

	snd_dice_stream_stop_duplex(dice);
	snd_dice_stream_release_duplex(dice);

	mutex_unlock(&dice->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->tx_stream[substream->pcm->device];
	int err;

	mutex_lock(&dice->mutex);
	err = snd_dice_stream_start_duplex(dice);
	mutex_unlock(&dice->mutex);
	if (err >= 0)
		amdtp_stream_pcm_prepare(stream);

	return 0;
}
static int playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->rx_stream[substream->pcm->device];
	int err;

	mutex_lock(&dice->mutex);
	err = snd_dice_stream_start_duplex(dice);
	mutex_unlock(&dice->mutex);
	if (err >= 0)
		amdtp_stream_pcm_prepare(stream);

	return err;
}

static int capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->tx_stream[substream->pcm->device];

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
static int playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->rx_stream[substream->pcm->device];

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->tx_stream[substream->pcm->device];

	return amdtp_stream_pcm_pointer(stream);
}
static snd_pcm_uframes_t playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->rx_stream[substream->pcm->device];

	return amdtp_stream_pcm_pointer(stream);
}

static int capture_ack(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->tx_stream[substream->pcm->device];

	return amdtp_stream_pcm_ack(stream);
}

static int playback_ack(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->rx_stream[substream->pcm->device];

	return amdtp_stream_pcm_ack(stream);
}

int snd_dice_create_pcm(struct snd_dice *dice)
{
	static const struct snd_pcm_ops capture_ops = {
		.open      = pcm_open,
		.close     = pcm_close,
		.ioctl     = snd_pcm_lib_ioctl,
		.hw_params = capture_hw_params,
		.hw_free   = capture_hw_free,
		.prepare   = capture_prepare,
		.trigger   = capture_trigger,
		.pointer   = capture_pointer,
		.ack       = capture_ack,
		.page      = snd_pcm_lib_get_vmalloc_page,
	};
	static const struct snd_pcm_ops playback_ops = {
		.open      = pcm_open,
		.close     = pcm_close,
		.ioctl     = snd_pcm_lib_ioctl,
		.hw_params = playback_hw_params,
		.hw_free   = playback_hw_free,
		.prepare   = playback_prepare,
		.trigger   = playback_trigger,
		.pointer   = playback_pointer,
		.ack       = playback_ack,
		.page      = snd_pcm_lib_get_vmalloc_page,
	};
	struct snd_pcm *pcm;
	unsigned int capture, playback;
	int i, j;
	int err;

	for (i = 0; i < MAX_STREAMS; i++) {
		capture = playback = 0;
		for (j = 0; j < SND_DICE_RATE_MODE_COUNT; ++j) {
			if (dice->tx_pcm_chs[i][j] > 0)
				capture = 1;
			if (dice->rx_pcm_chs[i][j] > 0)
				playback = 1;
		}

		err = snd_pcm_new(dice->card, "DICE", i, playback, capture,
				  &pcm);
		if (err < 0)
			return err;
		pcm->private_data = dice;
		strcpy(pcm->name, dice->card->shortname);

		if (capture > 0)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
					&capture_ops);

		if (playback > 0)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
					&playback_ops);
	}

	return 0;
}
