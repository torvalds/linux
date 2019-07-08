// SPDX-License-Identifier: GPL-2.0-only
/*
 * C-Media CMI8788 driver - PCM code
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

#include <linux/pci.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "oxygen.h"

/* most DMA channels have a 16-bit counter for 32-bit words */
#define BUFFER_BYTES_MAX		((1 << 16) * 4)
/* the multichannel DMA channel has a 24-bit counter */
#define BUFFER_BYTES_MAX_MULTICH	((1 << 24) * 4)

#define FIFO_BYTES			256
#define FIFO_BYTES_MULTICH		1024

#define PERIOD_BYTES_MIN		64

#define DEFAULT_BUFFER_BYTES		(BUFFER_BYTES_MAX / 2)
#define DEFAULT_BUFFER_BYTES_MULTICH	(1024 * 1024)

static const struct snd_pcm_hardware oxygen_stereo_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_SYNC_START |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.rates = SNDRV_PCM_RATE_32000 |
		 SNDRV_PCM_RATE_44100 |
		 SNDRV_PCM_RATE_48000 |
		 SNDRV_PCM_RATE_64000 |
		 SNDRV_PCM_RATE_88200 |
		 SNDRV_PCM_RATE_96000 |
		 SNDRV_PCM_RATE_176400 |
		 SNDRV_PCM_RATE_192000,
	.rate_min = 32000,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = BUFFER_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = BUFFER_BYTES_MAX,
	.periods_min = 1,
	.periods_max = BUFFER_BYTES_MAX / PERIOD_BYTES_MIN,
	.fifo_size = FIFO_BYTES,
};
static const struct snd_pcm_hardware oxygen_multichannel_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_SYNC_START |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.rates = SNDRV_PCM_RATE_32000 |
		 SNDRV_PCM_RATE_44100 |
		 SNDRV_PCM_RATE_48000 |
		 SNDRV_PCM_RATE_64000 |
		 SNDRV_PCM_RATE_88200 |
		 SNDRV_PCM_RATE_96000 |
		 SNDRV_PCM_RATE_176400 |
		 SNDRV_PCM_RATE_192000,
	.rate_min = 32000,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 8,
	.buffer_bytes_max = BUFFER_BYTES_MAX_MULTICH,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = BUFFER_BYTES_MAX_MULTICH,
	.periods_min = 1,
	.periods_max = BUFFER_BYTES_MAX_MULTICH / PERIOD_BYTES_MIN,
	.fifo_size = FIFO_BYTES_MULTICH,
};
static const struct snd_pcm_hardware oxygen_ac97_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_SYNC_START |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = BUFFER_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = BUFFER_BYTES_MAX,
	.periods_min = 1,
	.periods_max = BUFFER_BYTES_MAX / PERIOD_BYTES_MIN,
	.fifo_size = FIFO_BYTES,
};

static const struct snd_pcm_hardware *const oxygen_hardware[PCM_COUNT] = {
	[PCM_A] = &oxygen_stereo_hardware,
	[PCM_B] = &oxygen_stereo_hardware,
	[PCM_C] = &oxygen_stereo_hardware,
	[PCM_SPDIF] = &oxygen_stereo_hardware,
	[PCM_MULTICH] = &oxygen_multichannel_hardware,
	[PCM_AC97] = &oxygen_ac97_hardware,
};

static inline unsigned int
oxygen_substream_channel(struct snd_pcm_substream *substream)
{
	return (unsigned int)(uintptr_t)substream->runtime->private_data;
}

static int oxygen_open(struct snd_pcm_substream *substream,
		       unsigned int channel)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	runtime->private_data = (void *)(uintptr_t)channel;
	if (channel == PCM_B && chip->has_ac97_1 &&
	    (chip->model.device_config & CAPTURE_2_FROM_AC97_1))
		runtime->hw = oxygen_ac97_hardware;
	else
		runtime->hw = *oxygen_hardware[channel];
	switch (channel) {
	case PCM_C:
		if (chip->model.device_config & CAPTURE_1_FROM_SPDIF) {
			runtime->hw.rates &= ~(SNDRV_PCM_RATE_32000 |
					       SNDRV_PCM_RATE_64000);
			runtime->hw.rate_min = 44100;
		}
		/* fall through */
	case PCM_A:
	case PCM_B:
		runtime->hw.fifo_size = 0;
		break;
	case PCM_MULTICH:
		runtime->hw.channels_max = chip->model.dac_channels_pcm;
		break;
	}
	if (chip->model.pcm_hardware_filter)
		chip->model.pcm_hardware_filter(channel, &runtime->hw);
	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (err < 0)
		return err;
	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (err < 0)
		return err;
	if (runtime->hw.formats & SNDRV_PCM_FMTBIT_S32_LE) {
		err = snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
		if (err < 0)
			return err;
	}
	if (runtime->hw.channels_max > 2) {
		err = snd_pcm_hw_constraint_step(runtime, 0,
						 SNDRV_PCM_HW_PARAM_CHANNELS,
						 2);
		if (err < 0)
			return err;
	}
	snd_pcm_set_sync(substream);
	chip->streams[channel] = substream;

	mutex_lock(&chip->mutex);
	chip->pcm_active |= 1 << channel;
	if (channel == PCM_SPDIF) {
		chip->spdif_pcm_bits = chip->spdif_bits;
		chip->controls[CONTROL_SPDIF_PCM]->vd[0].access &=
			~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE |
			       SNDRV_CTL_EVENT_MASK_INFO,
			       &chip->controls[CONTROL_SPDIF_PCM]->id);
	}
	mutex_unlock(&chip->mutex);

	return 0;
}

static int oxygen_rec_a_open(struct snd_pcm_substream *substream)
{
	return oxygen_open(substream, PCM_A);
}

static int oxygen_rec_b_open(struct snd_pcm_substream *substream)
{
	return oxygen_open(substream, PCM_B);
}

static int oxygen_rec_c_open(struct snd_pcm_substream *substream)
{
	return oxygen_open(substream, PCM_C);
}

static int oxygen_spdif_open(struct snd_pcm_substream *substream)
{
	return oxygen_open(substream, PCM_SPDIF);
}

static int oxygen_multich_open(struct snd_pcm_substream *substream)
{
	return oxygen_open(substream, PCM_MULTICH);
}

static int oxygen_ac97_open(struct snd_pcm_substream *substream)
{
	return oxygen_open(substream, PCM_AC97);
}

static int oxygen_close(struct snd_pcm_substream *substream)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	unsigned int channel = oxygen_substream_channel(substream);

	mutex_lock(&chip->mutex);
	chip->pcm_active &= ~(1 << channel);
	if (channel == PCM_SPDIF) {
		chip->controls[CONTROL_SPDIF_PCM]->vd[0].access |=
			SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE |
			       SNDRV_CTL_EVENT_MASK_INFO,
			       &chip->controls[CONTROL_SPDIF_PCM]->id);
	}
	if (channel == PCM_SPDIF || channel == PCM_MULTICH)
		oxygen_update_spdif_source(chip);
	mutex_unlock(&chip->mutex);

	chip->streams[channel] = NULL;
	return 0;
}

static unsigned int oxygen_format(struct snd_pcm_hw_params *hw_params)
{
	if (params_format(hw_params) == SNDRV_PCM_FORMAT_S32_LE)
		return OXYGEN_FORMAT_24;
	else
		return OXYGEN_FORMAT_16;
}

static unsigned int oxygen_rate(struct snd_pcm_hw_params *hw_params)
{
	switch (params_rate(hw_params)) {
	case 32000:
		return OXYGEN_RATE_32000;
	case 44100:
		return OXYGEN_RATE_44100;
	default: /* 48000 */
		return OXYGEN_RATE_48000;
	case 64000:
		return OXYGEN_RATE_64000;
	case 88200:
		return OXYGEN_RATE_88200;
	case 96000:
		return OXYGEN_RATE_96000;
	case 176400:
		return OXYGEN_RATE_176400;
	case 192000:
		return OXYGEN_RATE_192000;
	}
}

static unsigned int oxygen_i2s_bits(struct snd_pcm_hw_params *hw_params)
{
	if (params_format(hw_params) == SNDRV_PCM_FORMAT_S32_LE)
		return OXYGEN_I2S_BITS_24;
	else
		return OXYGEN_I2S_BITS_16;
}

static unsigned int oxygen_play_channels(struct snd_pcm_hw_params *hw_params)
{
	switch (params_channels(hw_params)) {
	default: /* 2 */
		return OXYGEN_PLAY_CHANNELS_2;
	case 4:
		return OXYGEN_PLAY_CHANNELS_4;
	case 6:
		return OXYGEN_PLAY_CHANNELS_6;
	case 8:
		return OXYGEN_PLAY_CHANNELS_8;
	}
}

static const unsigned int channel_base_registers[PCM_COUNT] = {
	[PCM_A] = OXYGEN_DMA_A_ADDRESS,
	[PCM_B] = OXYGEN_DMA_B_ADDRESS,
	[PCM_C] = OXYGEN_DMA_C_ADDRESS,
	[PCM_SPDIF] = OXYGEN_DMA_SPDIF_ADDRESS,
	[PCM_MULTICH] = OXYGEN_DMA_MULTICH_ADDRESS,
	[PCM_AC97] = OXYGEN_DMA_AC97_ADDRESS,
};

static int oxygen_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *hw_params)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	unsigned int channel = oxygen_substream_channel(substream);
	int err;

	err = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	oxygen_write32(chip, channel_base_registers[channel],
		       (u32)substream->runtime->dma_addr);
	if (channel == PCM_MULTICH) {
		oxygen_write32(chip, OXYGEN_DMA_MULTICH_COUNT,
			       params_buffer_bytes(hw_params) / 4 - 1);
		oxygen_write32(chip, OXYGEN_DMA_MULTICH_TCOUNT,
			       params_period_bytes(hw_params) / 4 - 1);
	} else {
		oxygen_write16(chip, channel_base_registers[channel] + 4,
			       params_buffer_bytes(hw_params) / 4 - 1);
		oxygen_write16(chip, channel_base_registers[channel] + 6,
			       params_period_bytes(hw_params) / 4 - 1);
	}
	return 0;
}

static u16 get_mclk(struct oxygen *chip, unsigned int channel,
		    struct snd_pcm_hw_params *params)
{
	unsigned int mclks, shift;

	if (channel == PCM_MULTICH)
		mclks = chip->model.dac_mclks;
	else
		mclks = chip->model.adc_mclks;

	if (params_rate(params) <= 48000)
		shift = 0;
	else if (params_rate(params) <= 96000)
		shift = 2;
	else
		shift = 4;

	return OXYGEN_I2S_MCLK(mclks >> shift);
}

static int oxygen_rec_a_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *hw_params)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	int err;

	err = oxygen_hw_params(substream, hw_params);
	if (err < 0)
		return err;

	spin_lock_irq(&chip->reg_lock);
	oxygen_write8_masked(chip, OXYGEN_REC_FORMAT,
			     oxygen_format(hw_params) << OXYGEN_REC_FORMAT_A_SHIFT,
			     OXYGEN_REC_FORMAT_A_MASK);
	oxygen_write16_masked(chip, OXYGEN_I2S_A_FORMAT,
			      oxygen_rate(hw_params) |
			      chip->model.adc_i2s_format |
			      get_mclk(chip, PCM_A, hw_params) |
			      oxygen_i2s_bits(hw_params),
			      OXYGEN_I2S_RATE_MASK |
			      OXYGEN_I2S_FORMAT_MASK |
			      OXYGEN_I2S_MCLK_MASK |
			      OXYGEN_I2S_BITS_MASK);
	spin_unlock_irq(&chip->reg_lock);

	mutex_lock(&chip->mutex);
	chip->model.set_adc_params(chip, hw_params);
	mutex_unlock(&chip->mutex);
	return 0;
}

static int oxygen_rec_b_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *hw_params)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	int is_ac97;
	int err;

	err = oxygen_hw_params(substream, hw_params);
	if (err < 0)
		return err;

	is_ac97 = chip->has_ac97_1 &&
		(chip->model.device_config & CAPTURE_2_FROM_AC97_1);

	spin_lock_irq(&chip->reg_lock);
	oxygen_write8_masked(chip, OXYGEN_REC_FORMAT,
			     oxygen_format(hw_params) << OXYGEN_REC_FORMAT_B_SHIFT,
			     OXYGEN_REC_FORMAT_B_MASK);
	if (!is_ac97)
		oxygen_write16_masked(chip, OXYGEN_I2S_B_FORMAT,
				      oxygen_rate(hw_params) |
				      chip->model.adc_i2s_format |
				      get_mclk(chip, PCM_B, hw_params) |
				      oxygen_i2s_bits(hw_params),
				      OXYGEN_I2S_RATE_MASK |
				      OXYGEN_I2S_FORMAT_MASK |
				      OXYGEN_I2S_MCLK_MASK |
				      OXYGEN_I2S_BITS_MASK);
	spin_unlock_irq(&chip->reg_lock);

	if (!is_ac97) {
		mutex_lock(&chip->mutex);
		chip->model.set_adc_params(chip, hw_params);
		mutex_unlock(&chip->mutex);
	}
	return 0;
}

static int oxygen_rec_c_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *hw_params)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	bool is_spdif;
	int err;

	err = oxygen_hw_params(substream, hw_params);
	if (err < 0)
		return err;

	is_spdif = chip->model.device_config & CAPTURE_1_FROM_SPDIF;

	spin_lock_irq(&chip->reg_lock);
	oxygen_write8_masked(chip, OXYGEN_REC_FORMAT,
			     oxygen_format(hw_params) << OXYGEN_REC_FORMAT_C_SHIFT,
			     OXYGEN_REC_FORMAT_C_MASK);
	if (!is_spdif)
		oxygen_write16_masked(chip, OXYGEN_I2S_C_FORMAT,
				      oxygen_rate(hw_params) |
				      chip->model.adc_i2s_format |
				      get_mclk(chip, PCM_B, hw_params) |
				      oxygen_i2s_bits(hw_params),
				      OXYGEN_I2S_RATE_MASK |
				      OXYGEN_I2S_FORMAT_MASK |
				      OXYGEN_I2S_MCLK_MASK |
				      OXYGEN_I2S_BITS_MASK);
	spin_unlock_irq(&chip->reg_lock);

	if (!is_spdif) {
		mutex_lock(&chip->mutex);
		chip->model.set_adc_params(chip, hw_params);
		mutex_unlock(&chip->mutex);
	}
	return 0;
}

static int oxygen_spdif_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *hw_params)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	int err;

	err = oxygen_hw_params(substream, hw_params);
	if (err < 0)
		return err;

	mutex_lock(&chip->mutex);
	spin_lock_irq(&chip->reg_lock);
	oxygen_clear_bits32(chip, OXYGEN_SPDIF_CONTROL,
			    OXYGEN_SPDIF_OUT_ENABLE);
	oxygen_write8_masked(chip, OXYGEN_PLAY_FORMAT,
			     oxygen_format(hw_params) << OXYGEN_SPDIF_FORMAT_SHIFT,
			     OXYGEN_SPDIF_FORMAT_MASK);
	oxygen_write32_masked(chip, OXYGEN_SPDIF_CONTROL,
			      oxygen_rate(hw_params) << OXYGEN_SPDIF_OUT_RATE_SHIFT,
			      OXYGEN_SPDIF_OUT_RATE_MASK);
	oxygen_update_spdif_source(chip);
	spin_unlock_irq(&chip->reg_lock);
	mutex_unlock(&chip->mutex);
	return 0;
}

static int oxygen_multich_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	int err;

	err = oxygen_hw_params(substream, hw_params);
	if (err < 0)
		return err;

	mutex_lock(&chip->mutex);
	spin_lock_irq(&chip->reg_lock);
	oxygen_write8_masked(chip, OXYGEN_PLAY_CHANNELS,
			     oxygen_play_channels(hw_params),
			     OXYGEN_PLAY_CHANNELS_MASK);
	oxygen_write8_masked(chip, OXYGEN_PLAY_FORMAT,
			     oxygen_format(hw_params) << OXYGEN_MULTICH_FORMAT_SHIFT,
			     OXYGEN_MULTICH_FORMAT_MASK);
	oxygen_write16_masked(chip, OXYGEN_I2S_MULTICH_FORMAT,
			      oxygen_rate(hw_params) |
			      chip->model.dac_i2s_format |
			      get_mclk(chip, PCM_MULTICH, hw_params) |
			      oxygen_i2s_bits(hw_params),
			      OXYGEN_I2S_RATE_MASK |
			      OXYGEN_I2S_FORMAT_MASK |
			      OXYGEN_I2S_MCLK_MASK |
			      OXYGEN_I2S_BITS_MASK);
	oxygen_update_spdif_source(chip);
	spin_unlock_irq(&chip->reg_lock);

	chip->model.set_dac_params(chip, hw_params);
	oxygen_update_dac_routing(chip);
	mutex_unlock(&chip->mutex);
	return 0;
}

static int oxygen_hw_free(struct snd_pcm_substream *substream)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	unsigned int channel = oxygen_substream_channel(substream);
	unsigned int channel_mask = 1 << channel;

	spin_lock_irq(&chip->reg_lock);
	chip->interrupt_mask &= ~channel_mask;
	oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, chip->interrupt_mask);

	oxygen_set_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);
	oxygen_clear_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);
	spin_unlock_irq(&chip->reg_lock);

	return snd_pcm_lib_free_pages(substream);
}

static int oxygen_spdif_hw_free(struct snd_pcm_substream *substream)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);

	spin_lock_irq(&chip->reg_lock);
	oxygen_clear_bits32(chip, OXYGEN_SPDIF_CONTROL,
			    OXYGEN_SPDIF_OUT_ENABLE);
	spin_unlock_irq(&chip->reg_lock);
	return oxygen_hw_free(substream);
}

static int oxygen_prepare(struct snd_pcm_substream *substream)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	unsigned int channel = oxygen_substream_channel(substream);
	unsigned int channel_mask = 1 << channel;

	spin_lock_irq(&chip->reg_lock);
	oxygen_set_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);
	oxygen_clear_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);

	if (substream->runtime->no_period_wakeup)
		chip->interrupt_mask &= ~channel_mask;
	else
		chip->interrupt_mask |= channel_mask;
	oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, chip->interrupt_mask);
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int oxygen_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	unsigned int mask = 0;
	int pausing;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		pausing = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		pausing = 1;
		break;
	default:
		return -EINVAL;
	}

	snd_pcm_group_for_each_entry(s, substream) {
		if (snd_pcm_substream_chip(s) == chip) {
			mask |= 1 << oxygen_substream_channel(s);
			snd_pcm_trigger_done(s, substream);
		}
	}

	spin_lock(&chip->reg_lock);
	if (!pausing) {
		if (cmd == SNDRV_PCM_TRIGGER_START)
			chip->pcm_running |= mask;
		else
			chip->pcm_running &= ~mask;
		oxygen_write8(chip, OXYGEN_DMA_STATUS, chip->pcm_running);
	} else {
		if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH)
			oxygen_set_bits8(chip, OXYGEN_DMA_PAUSE, mask);
		else
			oxygen_clear_bits8(chip, OXYGEN_DMA_PAUSE, mask);
	}
	spin_unlock(&chip->reg_lock);
	return 0;
}

static snd_pcm_uframes_t oxygen_pointer(struct snd_pcm_substream *substream)
{
	struct oxygen *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int channel = oxygen_substream_channel(substream);
	u32 curr_addr;

	/* no spinlock, this read should be atomic */
	curr_addr = oxygen_read32(chip, channel_base_registers[channel]);
	return bytes_to_frames(runtime, curr_addr - (u32)runtime->dma_addr);
}

static const struct snd_pcm_ops oxygen_rec_a_ops = {
	.open      = oxygen_rec_a_open,
	.close     = oxygen_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = oxygen_rec_a_hw_params,
	.hw_free   = oxygen_hw_free,
	.prepare   = oxygen_prepare,
	.trigger   = oxygen_trigger,
	.pointer   = oxygen_pointer,
};

static const struct snd_pcm_ops oxygen_rec_b_ops = {
	.open      = oxygen_rec_b_open,
	.close     = oxygen_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = oxygen_rec_b_hw_params,
	.hw_free   = oxygen_hw_free,
	.prepare   = oxygen_prepare,
	.trigger   = oxygen_trigger,
	.pointer   = oxygen_pointer,
};

static const struct snd_pcm_ops oxygen_rec_c_ops = {
	.open      = oxygen_rec_c_open,
	.close     = oxygen_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = oxygen_rec_c_hw_params,
	.hw_free   = oxygen_hw_free,
	.prepare   = oxygen_prepare,
	.trigger   = oxygen_trigger,
	.pointer   = oxygen_pointer,
};

static const struct snd_pcm_ops oxygen_spdif_ops = {
	.open      = oxygen_spdif_open,
	.close     = oxygen_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = oxygen_spdif_hw_params,
	.hw_free   = oxygen_spdif_hw_free,
	.prepare   = oxygen_prepare,
	.trigger   = oxygen_trigger,
	.pointer   = oxygen_pointer,
};

static const struct snd_pcm_ops oxygen_multich_ops = {
	.open      = oxygen_multich_open,
	.close     = oxygen_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = oxygen_multich_hw_params,
	.hw_free   = oxygen_hw_free,
	.prepare   = oxygen_prepare,
	.trigger   = oxygen_trigger,
	.pointer   = oxygen_pointer,
};

static const struct snd_pcm_ops oxygen_ac97_ops = {
	.open      = oxygen_ac97_open,
	.close     = oxygen_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = oxygen_hw_params,
	.hw_free   = oxygen_hw_free,
	.prepare   = oxygen_prepare,
	.trigger   = oxygen_trigger,
	.pointer   = oxygen_pointer,
};

int oxygen_pcm_init(struct oxygen *chip)
{
	struct snd_pcm *pcm;
	int outs, ins;
	int err;

	outs = !!(chip->model.device_config & PLAYBACK_0_TO_I2S);
	ins = !!(chip->model.device_config & (CAPTURE_0_FROM_I2S_1 |
					      CAPTURE_0_FROM_I2S_2));
	if (outs | ins) {
		err = snd_pcm_new(chip->card, "Multichannel",
				  0, outs, ins, &pcm);
		if (err < 0)
			return err;
		if (outs)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
					&oxygen_multich_ops);
		if (chip->model.device_config & CAPTURE_0_FROM_I2S_1)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
					&oxygen_rec_a_ops);
		else if (chip->model.device_config & CAPTURE_0_FROM_I2S_2)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
					&oxygen_rec_b_ops);
		pcm->private_data = chip;
		strcpy(pcm->name, "Multichannel");
		if (outs)
			snd_pcm_lib_preallocate_pages(pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream,
						      SNDRV_DMA_TYPE_DEV,
						      snd_dma_pci_data(chip->pci),
						      DEFAULT_BUFFER_BYTES_MULTICH,
						      BUFFER_BYTES_MAX_MULTICH);
		if (ins)
			snd_pcm_lib_preallocate_pages(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream,
						      SNDRV_DMA_TYPE_DEV,
						      snd_dma_pci_data(chip->pci),
						      DEFAULT_BUFFER_BYTES,
						      BUFFER_BYTES_MAX);
	}

	outs = !!(chip->model.device_config & PLAYBACK_1_TO_SPDIF);
	ins = !!(chip->model.device_config & CAPTURE_1_FROM_SPDIF);
	if (outs | ins) {
		err = snd_pcm_new(chip->card, "Digital", 1, outs, ins, &pcm);
		if (err < 0)
			return err;
		if (outs)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
					&oxygen_spdif_ops);
		if (ins)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
					&oxygen_rec_c_ops);
		pcm->private_data = chip;
		strcpy(pcm->name, "Digital");
		snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						      snd_dma_pci_data(chip->pci),
						      DEFAULT_BUFFER_BYTES,
						      BUFFER_BYTES_MAX);
	}

	if (chip->has_ac97_1) {
		outs = !!(chip->model.device_config & PLAYBACK_2_TO_AC97_1);
		ins = !!(chip->model.device_config & CAPTURE_2_FROM_AC97_1);
	} else {
		outs = 0;
		ins = !!(chip->model.device_config & CAPTURE_2_FROM_I2S_2);
	}
	if (outs | ins) {
		err = snd_pcm_new(chip->card, outs ? "AC97" : "Analog2",
				  2, outs, ins, &pcm);
		if (err < 0)
			return err;
		if (outs) {
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
					&oxygen_ac97_ops);
			oxygen_write8_masked(chip, OXYGEN_REC_ROUTING,
					     OXYGEN_REC_B_ROUTE_AC97_1,
					     OXYGEN_REC_B_ROUTE_MASK);
		}
		if (ins)
			snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
					&oxygen_rec_b_ops);
		pcm->private_data = chip;
		strcpy(pcm->name, outs ? "Front Panel" : "Analog 2");
		snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						      snd_dma_pci_data(chip->pci),
						      DEFAULT_BUFFER_BYTES,
						      BUFFER_BYTES_MAX);
	}

	ins = !!(chip->model.device_config & CAPTURE_3_FROM_I2S_3);
	if (ins) {
		err = snd_pcm_new(chip->card, "Analog3", 3, 0, ins, &pcm);
		if (err < 0)
			return err;
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				&oxygen_rec_c_ops);
		oxygen_write8_masked(chip, OXYGEN_REC_ROUTING,
				     OXYGEN_REC_C_ROUTE_I2S_ADC_3,
				     OXYGEN_REC_C_ROUTE_MASK);
		pcm->private_data = chip;
		strcpy(pcm->name, "Analog 3");
		snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						      snd_dma_pci_data(chip->pci),
						      DEFAULT_BUFFER_BYTES,
						      BUFFER_BYTES_MAX);
	}
	return 0;
}
