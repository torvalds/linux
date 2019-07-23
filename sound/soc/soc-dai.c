// SPDX-License-Identifier: GPL-2.0
//
// soc-dai.c
//
// Copyright (C) 2019 Renesas Electronics Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//

#include <sound/soc.h>
#include <sound/soc-dai.h>

/**
 * snd_soc_dai_set_sysclk - configure DAI system or master clock.
 * @dai: DAI
 * @clk_id: DAI specific clock ID
 * @freq: new clock frequency in Hz
 * @dir: new clock direction - input/output.
 *
 * Configures the DAI master (MCLK) or system (SYSCLK) clocking.
 */
int snd_soc_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			   unsigned int freq, int dir)
{
	if (dai->driver->ops->set_sysclk)
		return dai->driver->ops->set_sysclk(dai, clk_id, freq, dir);

	return snd_soc_component_set_sysclk(dai->component, clk_id, 0,
					    freq, dir);
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_sysclk);

/**
 * snd_soc_dai_set_clkdiv - configure DAI clock dividers.
 * @dai: DAI
 * @div_id: DAI specific clock divider ID
 * @div: new clock divisor.
 *
 * Configures the clock dividers. This is used to derive the best DAI bit and
 * frame clocks from the system or master clock. It's best to set the DAI bit
 * and frame clocks as low as possible to save system power.
 */
int snd_soc_dai_set_clkdiv(struct snd_soc_dai *dai,
			   int div_id, int div)
{
	if (dai->driver->ops->set_clkdiv)
		return dai->driver->ops->set_clkdiv(dai, div_id, div);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_clkdiv);

/**
 * snd_soc_dai_set_pll - configure DAI PLL.
 * @dai: DAI
 * @pll_id: DAI specific PLL ID
 * @source: DAI specific source for the PLL
 * @freq_in: PLL input clock frequency in Hz
 * @freq_out: requested PLL output clock frequency in Hz
 *
 * Configures and enables PLL to generate output clock based on input clock.
 */
int snd_soc_dai_set_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	if (dai->driver->ops->set_pll)
		return dai->driver->ops->set_pll(dai, pll_id, source,
						 freq_in, freq_out);

	return snd_soc_component_set_pll(dai->component, pll_id, source,
					 freq_in, freq_out);
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_pll);

/**
 * snd_soc_dai_set_bclk_ratio - configure BCLK to sample rate ratio.
 * @dai: DAI
 * @ratio: Ratio of BCLK to Sample rate.
 *
 * Configures the DAI for a preset BCLK to sample rate ratio.
 */
int snd_soc_dai_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	if (dai->driver->ops->set_bclk_ratio)
		return dai->driver->ops->set_bclk_ratio(dai, ratio);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_bclk_ratio);

/**
 * snd_soc_dai_set_fmt - configure DAI hardware audio format.
 * @dai: DAI
 * @fmt: SND_SOC_DAIFMT_* format value.
 *
 * Configures the DAI hardware format and clocking.
 */
int snd_soc_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	if (dai->driver->ops->set_fmt == NULL)
		return -ENOTSUPP;
	return dai->driver->ops->set_fmt(dai, fmt);
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_fmt);

/**
 * snd_soc_xlate_tdm_slot - generate tx/rx slot mask.
 * @slots: Number of slots in use.
 * @tx_mask: bitmask representing active TX slots.
 * @rx_mask: bitmask representing active RX slots.
 *
 * Generates the TDM tx and rx slot default masks for DAI.
 */
static int snd_soc_xlate_tdm_slot_mask(unsigned int slots,
				       unsigned int *tx_mask,
				       unsigned int *rx_mask)
{
	if (*tx_mask || *rx_mask)
		return 0;

	if (!slots)
		return -EINVAL;

	*tx_mask = (1 << slots) - 1;
	*rx_mask = (1 << slots) - 1;

	return 0;
}

/**
 * snd_soc_dai_set_tdm_slot() - Configures a DAI for TDM operation
 * @dai: The DAI to configure
 * @tx_mask: bitmask representing active TX slots.
 * @rx_mask: bitmask representing active RX slots.
 * @slots: Number of slots in use.
 * @slot_width: Width in bits for each slot.
 *
 * This function configures the specified DAI for TDM operation. @slot contains
 * the total number of slots of the TDM stream and @slot_with the width of each
 * slot in bit clock cycles. @tx_mask and @rx_mask are bitmasks specifying the
 * active slots of the TDM stream for the specified DAI, i.e. which slots the
 * DAI should write to or read from. If a bit is set the corresponding slot is
 * active, if a bit is cleared the corresponding slot is inactive. Bit 0 maps to
 * the first slot, bit 1 to the second slot and so on. The first active slot
 * maps to the first channel of the DAI, the second active slot to the second
 * channel and so on.
 *
 * TDM mode can be disabled by passing 0 for @slots. In this case @tx_mask,
 * @rx_mask and @slot_width will be ignored.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int snd_soc_dai_set_tdm_slot(struct snd_soc_dai *dai,
			     unsigned int tx_mask, unsigned int rx_mask,
			     int slots, int slot_width)
{
	if (dai->driver->ops->xlate_tdm_slot_mask)
		dai->driver->ops->xlate_tdm_slot_mask(slots,
						      &tx_mask, &rx_mask);
	else
		snd_soc_xlate_tdm_slot_mask(slots, &tx_mask, &rx_mask);

	dai->tx_mask = tx_mask;
	dai->rx_mask = rx_mask;

	if (dai->driver->ops->set_tdm_slot)
		return dai->driver->ops->set_tdm_slot(dai, tx_mask, rx_mask,
						      slots, slot_width);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_tdm_slot);

/**
 * snd_soc_dai_set_channel_map - configure DAI audio channel map
 * @dai: DAI
 * @tx_num: how many TX channels
 * @tx_slot: pointer to an array which imply the TX slot number channel
 *           0~num-1 uses
 * @rx_num: how many RX channels
 * @rx_slot: pointer to an array which imply the RX slot number channel
 *           0~num-1 uses
 *
 * configure the relationship between channel number and TDM slot number.
 */
int snd_soc_dai_set_channel_map(struct snd_soc_dai *dai,
				unsigned int tx_num, unsigned int *tx_slot,
				unsigned int rx_num, unsigned int *rx_slot)
{
	if (dai->driver->ops->set_channel_map)
		return dai->driver->ops->set_channel_map(dai, tx_num, tx_slot,
							 rx_num, rx_slot);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_channel_map);

/**
 * snd_soc_dai_get_channel_map - Get DAI audio channel map
 * @dai: DAI
 * @tx_num: how many TX channels
 * @tx_slot: pointer to an array which imply the TX slot number channel
 *           0~num-1 uses
 * @rx_num: how many RX channels
 * @rx_slot: pointer to an array which imply the RX slot number channel
 *           0~num-1 uses
 */
int snd_soc_dai_get_channel_map(struct snd_soc_dai *dai,
				unsigned int *tx_num, unsigned int *tx_slot,
				unsigned int *rx_num, unsigned int *rx_slot)
{
	if (dai->driver->ops->get_channel_map)
		return dai->driver->ops->get_channel_map(dai, tx_num, tx_slot,
							 rx_num, rx_slot);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_get_channel_map);

/**
 * snd_soc_dai_set_tristate - configure DAI system or master clock.
 * @dai: DAI
 * @tristate: tristate enable
 *
 * Tristates the DAI so that others can use it.
 */
int snd_soc_dai_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	if (dai->driver->ops->set_tristate)
		return dai->driver->ops->set_tristate(dai, tristate);
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_set_tristate);

/**
 * snd_soc_dai_digital_mute - configure DAI system or master clock.
 * @dai: DAI
 * @mute: mute enable
 * @direction: stream to mute
 *
 * Mutes the DAI DAC.
 */
int snd_soc_dai_digital_mute(struct snd_soc_dai *dai, int mute,
			     int direction)
{
	if (dai->driver->ops->mute_stream)
		return dai->driver->ops->mute_stream(dai, mute, direction);
	else if (direction == SNDRV_PCM_STREAM_PLAYBACK &&
		 dai->driver->ops->digital_mute)
		return dai->driver->ops->digital_mute(dai, mute);
	else
		return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_dai_digital_mute);

int snd_soc_dai_hw_params(struct snd_soc_dai *dai,
			  struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int ret;

	/* perform any topology hw_params fixups before DAI  */
	if (rtd->dai_link->be_hw_params_fixup) {
		ret = rtd->dai_link->be_hw_params_fixup(rtd, params);
		if (ret < 0) {
			dev_err(rtd->dev,
				"ASoC: hw_params topology fixup failed %d\n",
				ret);
			return ret;
		}
	}

	if (dai->driver->ops->hw_params) {
		ret = dai->driver->ops->hw_params(substream, params, dai);
		if (ret < 0) {
			dev_err(dai->dev, "ASoC: can't set %s hw params: %d\n",
				dai->name, ret);
			return ret;
		}
	}

	return 0;
}

void snd_soc_dai_hw_free(struct snd_soc_dai *dai,
			 struct snd_pcm_substream *substream)
{
	if (dai->driver->ops->hw_free)
		dai->driver->ops->hw_free(substream, dai);
}

int snd_soc_dai_startup(struct snd_soc_dai *dai,
			struct snd_pcm_substream *substream)
{
	int ret = 0;

	if (dai->driver->ops->startup)
		ret = dai->driver->ops->startup(substream, dai);

	return ret;
}

void snd_soc_dai_shutdown(struct snd_soc_dai *dai,
			 struct snd_pcm_substream *substream)
{
	if (dai->driver->ops->shutdown)
		dai->driver->ops->shutdown(substream, dai);
}

int snd_soc_dai_prepare(struct snd_soc_dai *dai,
			struct snd_pcm_substream *substream)
{
	int ret = 0;

	if (dai->driver->ops->prepare)
		ret = dai->driver->ops->prepare(substream, dai);

	return ret;
}

int snd_soc_dai_trigger(struct snd_soc_dai *dai,
			struct snd_pcm_substream *substream,
			int cmd)
{
	int ret = 0;

	if (dai->driver->ops->trigger)
		ret = dai->driver->ops->trigger(substream, cmd, dai);

	return ret;
}

int snd_soc_dai_bespoke_trigger(struct snd_soc_dai *dai,
				struct snd_pcm_substream *substream,
				int cmd)
{
	int ret = 0;

	if (dai->driver->ops->bespoke_trigger)
		ret = dai->driver->ops->bespoke_trigger(substream, cmd, dai);

	return ret;
}

snd_pcm_sframes_t snd_soc_dai_delay(struct snd_soc_dai *dai,
				    struct snd_pcm_substream *substream)
{
	int delay = 0;

	if (dai->driver->ops->delay)
		delay = dai->driver->ops->delay(substream, dai);

	return delay;
}

void snd_soc_dai_suspend(struct snd_soc_dai *dai)
{
	if (dai->driver->suspend)
		dai->driver->suspend(dai);
}

void snd_soc_dai_resume(struct snd_soc_dai *dai)
{
	if (dai->driver->resume)
		dai->driver->resume(dai);
}

int snd_soc_dai_probe(struct snd_soc_dai *dai)
{
	if (dai->driver->probe)
		return dai->driver->probe(dai);
	return 0;
}

int snd_soc_dai_remove(struct snd_soc_dai *dai)
{
	if (dai->driver->remove)
		return dai->driver->remove(dai);
	return 0;
}

int snd_soc_dai_compress_new(struct snd_soc_dai *dai,
			     struct snd_soc_pcm_runtime *rtd, int num)
{
	if (dai->driver->compress_new)
		return dai->driver->compress_new(rtd, num);
	return -ENOTSUPP;
}

/*
 * snd_soc_dai_stream_valid() - check if a DAI supports the given stream
 *
 * Returns true if the DAI supports the indicated stream type.
 */
bool snd_soc_dai_stream_valid(struct snd_soc_dai *dai, int dir)
{
	struct snd_soc_pcm_stream *stream;

	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		stream = &dai->driver->playback;
	else
		stream = &dai->driver->capture;

	/* If the codec specifies any channels at all, it supports the stream */
	return stream->channels_min;
}
