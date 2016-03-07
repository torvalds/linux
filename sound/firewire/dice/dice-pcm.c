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
				    struct amdtp_stream *stream)
{
	struct snd_pcm_hardware *hw = &runtime->hw;
	unsigned int rate;
	__be32 reg[2];
	int err;

	/*
	 * Retrieve current Multi Bit Linear Audio data channel and limit to
	 * it.
	 */
	if (stream == &dice->tx_stream[0]) {
		err = snd_dice_transaction_read_tx(dice, TX_NUMBER_AUDIO,
						   reg, sizeof(reg));
	} else {
		err = snd_dice_transaction_read_rx(dice, RX_NUMBER_AUDIO,
						   reg, sizeof(reg));
	}
	if (err < 0)
		return err;

	hw->channels_min = hw->channels_max = be32_to_cpu(reg[0]);

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
	struct amdtp_stream *stream;
	int err;

	hw->info = SNDRV_PCM_INFO_MMAP |
		   SNDRV_PCM_INFO_MMAP_VALID |
		   SNDRV_PCM_INFO_BATCH |
		   SNDRV_PCM_INFO_INTERLEAVED |
		   SNDRV_PCM_INFO_JOINT_DUPLEX |
		   SNDRV_PCM_INFO_BLOCK_TRANSFER;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		hw->formats = AM824_IN_PCM_FORMAT_BITS;
		stream = &dice->tx_stream[0];
	} else {
		hw->formats = AM824_OUT_PCM_FORMAT_BITS;
		stream = &dice->rx_stream[0];
	}

	err = limit_channels_and_rates(dice, runtime, stream);
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
	struct amdtp_stream *stream = &dice->tx_stream[0];
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

	amdtp_am824_set_pcm_format(stream, params_format(hw_params));

	return 0;
}
static int playback_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *hw_params)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->rx_stream[0];
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

	amdtp_am824_set_pcm_format(stream, params_format(hw_params));

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
	struct amdtp_stream *stream = &dice->tx_stream[0];
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
	struct amdtp_stream *stream = &dice->rx_stream[0];
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
	struct amdtp_stream *stream = &dice->tx_stream[0];

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
	struct amdtp_stream *stream = &dice->rx_stream[0];

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
	struct amdtp_stream *stream = &dice->tx_stream[0];

	return amdtp_stream_pcm_pointer(stream);
}
static snd_pcm_uframes_t playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_dice *dice = substream->private_data;
	struct amdtp_stream *stream = &dice->rx_stream[0];

	return amdtp_stream_pcm_pointer(stream);
}

int snd_dice_create_pcm(struct snd_dice *dice)
{
	static struct snd_pcm_ops capture_ops = {
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
	static struct snd_pcm_ops playback_ops = {
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
	unsigned int capture, playback;
	int err;

	/*
	 * Check whether PCM substreams are required.
	 *
	 * TODO: in the case that any PCM substreams are not avail at a certain
	 * sampling transfer frequency?
	 */
	err = snd_dice_transaction_read_tx(dice, TX_NUMBER_AUDIO,
					   &reg, sizeof(reg));
	if (err < 0)
		return err;
	if (be32_to_cpu(reg) > 0)
		capture = 1;

	err = snd_dice_transaction_read_rx(dice, RX_NUMBER_AUDIO,
					   &reg, sizeof(reg));
	if (err < 0)
		return err;
	if (be32_to_cpu(reg) > 0)
		playback = 1;

	err = snd_pcm_new(dice->card, "DICE", 0, playback, capture, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = dice;
	strcpy(pcm->name, dice->card->shortname);

	if (capture > 0)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &capture_ops);

	if (playback > 0)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &playback_ops);

	return 0;
}
