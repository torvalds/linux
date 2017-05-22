/*
 * dice_pcm.c - a part of driver for DICE based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2014 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "dice.h"

static int limit_channels_and_rates(struct snd_dice *dice,
				    struct snd_pcm_runtime *runtime,
				    enum amdtp_stream_direction dir,
				    unsigned int index, unsigned int size)
{
	struct snd_pcm_hardware *hw = &runtime->hw;
	struct amdtp_stream *stream;
	unsigned int rate;
	__be32 reg;
	int err;

	/*
	 * Retrieve current Multi Bit Linear Audio data channel and limit to
	 * it.
	 */
	if (dir == AMDTP_IN_STREAM) {
		stream = &dice->tx_stream[index];
		err = snd_dice_transaction_read_tx(dice,
				size * index + TX_NUMBER_AUDIO,
				&reg, sizeof(reg));
	} else {
		stream = &dice->rx_stream[index];
		err = snd_dice_transaction_read_rx(dice,
				size * index + RX_NUMBER_AUDIO,
				&reg, sizeof(reg));
	}
	if (err < 0)
		return err;

	hw->channels_min = hw->channels_max = be32_to_cpu(reg);

	/* Retrieve current sampling transfer frequency and limit to it. */
	err = snd_dice_transaction_get_rate(dice, &rate);
	if (err < 0)
		return err;

	hw->rates = snd_pcm_rate_to_rate_bit(rate);
	snd_pcm_limit_hw_rates(runtime);

	return 0;
}

static void limit_period_and_buffer(struct snd_pcm_hardware *hw)
{
	hw->periods_min = 2;			/* SNDRV_PCM_INFO_BATCH */
	hw->periods_max = UINT_MAX;

	hw->period_bytes_min = 4 * hw->channels_max;    /* byte for a frame */

	/* Just to prevent from allocating much pages. */
	hw->period_bytes_max = hw->period_bytes_min * 2048;
	hw->buffer_bytes_max = hw->period_bytes_max * hw->periods_min;
}

static int init_hw_info(struct snd_dice *dice,
			struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm_hardware *hw = &runtime->hw;
	enum amdtp_stream_direction dir;
	struct amdtp_stream *stream;
	__be32 reg[2];
	unsigned int count, size;
	int err;

	hw->info = SNDRV_PCM_INFO_MMAP |
		   SNDRV_PCM_INFO_MMAP_VALID |
		   SNDRV_PCM_INFO_BATCH |
		   SNDRV_PCM_INFO_INTERLEAVED |
		   SNDRV_PCM_INFO_JOINT_DUPLEX |
		   SNDRV_PCM_INFO_BLOCK_TRANSFER;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		hw->formats = AM824_IN_PCM_FORMAT_BITS;
		dir = AMDTP_IN_STREAM;
		stream = &dice->tx_stream[substream->pcm->device];
		err = snd_dice_transaction_read_tx(dice, TX_NUMBER, reg,
						   sizeof(reg));
	} else {
		hw->formats = AM824_OUT_PCM_FORMAT_BITS;
		dir = AMDTP_OUT_STREAM;
		stream = &dice->rx_stream[substream->pcm->device];
		err = snd_dice_transaction_read_rx(dice, RX_NUMBER, reg,
						   sizeof(reg));
	}

	if (err < 0)
		return err;

	count = min_t(unsigned int, be32_to_cpu(reg[0]), MAX_STREAMS);
	if (substream->pcm->device >= count)
		return -ENXIO;

	size = be32_to_cpu(reg[1]) * 4;
	err = limit_channels_and_rates(dice, substream->runtime, dir,
				       substream->pcm->device, size);
	if (err < 0)
		return err;
	limit_period_and_buffer(hw);

	return amdtp_am824_add_pcm_hw_constraints(stream, runtime);
}

static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	int err;

	err = snd_dice_stream_lock_try(dice);
	if (err < 0)
		goto end;

	err = init_hw_info(dice, substream);
	if (err < 0)
		goto err_locked;

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
		mutex_lock(&dice->mutex);
		dice->substreams_counter++;
		mutex_unlock(&dice->mutex);
	}

	return 0;
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
		mutex_lock(&dice->mutex);
		dice->substreams_counter++;
		mutex_unlock(&dice->mutex);
	}

	return 0;
}

static int capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;

	mutex_lock(&dice->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		dice->substreams_counter--;

	snd_dice_stream_stop_duplex(dice);

	mutex_unlock(&dice->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;

	mutex_lock(&dice->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		dice->substreams_counter--;

	snd_dice_stream_stop_duplex(dice);

	mutex_unlock(&dice->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->tx_stream[substream->pcm->device];
	int err;

	mutex_lock(&dice->mutex);
	err = snd_dice_stream_start_duplex(dice, substream->runtime->rate);
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
	err = snd_dice_stream_start_duplex(dice, substream->runtime->rate);
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
		.page      = snd_pcm_lib_get_vmalloc_page,
		.mmap      = snd_pcm_lib_mmap_vmalloc,
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
		.page      = snd_pcm_lib_get_vmalloc_page,
		.mmap      = snd_pcm_lib_mmap_vmalloc,
	};
	__be32 reg;
	struct snd_pcm *pcm;
	unsigned int i, max_capture, max_playback, capture, playback;
	int err;

	/* Check whether PCM substreams are required. */
	if (dice->force_two_pcms) {
		max_capture = max_playback = 2;
	} else {
		max_capture = max_playback = 0;
		err = snd_dice_transaction_read_tx(dice, TX_NUMBER, &reg,
						   sizeof(reg));
		if (err < 0)
			return err;
		max_capture = min_t(unsigned int, be32_to_cpu(reg), MAX_STREAMS);

		err = snd_dice_transaction_read_rx(dice, RX_NUMBER, &reg,
						   sizeof(reg));
		if (err < 0)
			return err;
		max_playback = min_t(unsigned int, be32_to_cpu(reg), MAX_STREAMS);
	}

	for (i = 0; i < MAX_STREAMS; i++) {
		capture = playback = 0;
		if (i < max_capture)
			capture = 1;
		if (i < max_playback)
			playback = 1;
		if (capture == 0 && playback == 0)
			break;

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
