/*
 * Copyright (C) 2017 BayLibre, SAS
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu-regs.h"
#include "audio-core.h"

#define DRV_NAME "meson-aiu-i2s"

struct meson_aiu_i2s {
	struct meson_audio_core_data *core;
	struct clk *mclk;
	struct clk *bclks;
	struct clk *iface;
	struct clk *fast;
	bool bclks_idle;
	int irq;
};

#define AIU_MEM_I2S_BUF_CNTL_INIT		BIT(0)
#define AIU_MEM_I2S_CONTROL_INIT		BIT(0)
#define AIU_MEM_I2S_CONTROL_FILL_EN		BIT(1)
#define AIU_MEM_I2S_CONTROL_EMPTY_EN		BIT(2)
#define AIU_MEM_I2S_CONTROL_MODE_16BIT		BIT(6)
#define AIU_MEM_I2S_CONTROL_BUSY		BIT(7)
#define AIU_MEM_I2S_CONTROL_DATA_READY		BIT(8)
#define AIU_MEM_I2S_CONTROL_LEVEL_CNTL		BIT(9)
#define AIU_MEM_I2S_MASKS_IRQ_BLOCK_MASK	GENMASK(31, 16)
#define AIU_MEM_I2S_MASKS_IRQ_BLOCK(n)		((n) << 16)
#define AIU_MEM_I2S_MASKS_CH_MEM_MASK		GENMASK(15, 8)
#define AIU_MEM_I2S_MASKS_CH_MEM(ch)		((ch) << 8)
#define AIU_MEM_I2S_MASKS_CH_RD_MASK		GENMASK(7, 0)
#define AIU_MEM_I2S_MASKS_CH_RD(ch)		((ch) << 0)
#define AIU_RST_SOFT_I2S_FAST_DOMAIN		BIT(0)
#define AIU_RST_SOFT_I2S_SLOW_DOMAIN		BIT(1)

/*
 * The DMA works by i2s "blocks" (or DMA burst). The burst size and the memory
 * layout expected depends on the mode of operation.
 *
 * - Normal mode: The channels are expected to be packed in 32 bytes groups
 *  interleaved the buffer. AIU_MEM_I2S_MASKS_CH_MEM is a bitfield representing
 *  the channels present in memory. AIU_MEM_I2S_MASKS_CH_MEM represents the
 *  channels read by the DMA. This is very flexible but the unsual memory layout
 *  makes it less easy to deal with. The burst size is 32 bytes times the number
 *  of channels read.
 *
 * - Split mode:
 * Classical channel interleaved frame organisation. In this mode,
 * AIU_MEM_I2S_MASKS_CH_MEM and AIU_MEM_I2S_MASKS_CH_MEM must be set to 0xff and
 * the burst size is fixed to 256 bytes. The input can be either 2 or 8
 * channels.
 *
 * The following driver implements the split mode.
 */

#define AIU_I2S_DMA_BURST 256

static struct snd_pcm_hardware meson_aiu_i2s_dma_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE),

	.formats = (SNDRV_PCM_FMTBIT_S16_LE |
		    SNDRV_PCM_FMTBIT_S24_LE |
		    SNDRV_PCM_FMTBIT_S32_LE),

	/*
	 * TODO: The DMA can change the endianness, the msb position
	 * and deal with unsigned - support this later on
	 */

	.rate_min = 8000,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 8,
	.period_bytes_min = AIU_I2S_DMA_BURST,
	.period_bytes_max = AIU_I2S_DMA_BURST * 65535,
	.periods_min = 2,
	.periods_max = UINT_MAX,
	.buffer_bytes_max = 1 * 1024 * 1024,
	.fifo_size = 0,
};

static struct meson_aiu_i2s *meson_aiu_i2s_dma_priv(struct snd_pcm_substream *s)
{
	struct snd_soc_pcm_runtime *rtd = s->private_data;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, DRV_NAME);

	return snd_soc_component_get_drvdata(component);
}

static snd_pcm_uframes_t
meson_aiu_i2s_dma_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct meson_aiu_i2s *priv = meson_aiu_i2s_dma_priv(substream);
	unsigned int addr;
	int ret;

	ret = regmap_read(priv->core->aiu, AIU_MEM_I2S_RD_PTR,
			  &addr);
	if (ret)
		return 0;

	return bytes_to_frames(runtime, addr - (unsigned int)runtime->dma_addr);
}

static void __dma_enable(struct meson_aiu_i2s *priv, bool enable)
{
	unsigned int en_mask = (AIU_MEM_I2S_CONTROL_FILL_EN |
				AIU_MEM_I2S_CONTROL_EMPTY_EN);

	regmap_update_bits(priv->core->aiu, AIU_MEM_I2S_CONTROL, en_mask,
			   enable ? en_mask : 0);

}

static int meson_aiu_i2s_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct meson_aiu_i2s *priv = meson_aiu_i2s_dma_priv(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		__dma_enable(priv, true);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		__dma_enable(priv, false);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void __dma_init_mem(struct meson_aiu_i2s *priv)
{
	regmap_update_bits(priv->core->aiu, AIU_MEM_I2S_CONTROL,
			   AIU_MEM_I2S_CONTROL_INIT,
			   AIU_MEM_I2S_CONTROL_INIT);
	regmap_update_bits(priv->core->aiu, AIU_MEM_I2S_BUF_CNTL,
			   AIU_MEM_I2S_BUF_CNTL_INIT,
			   AIU_MEM_I2S_BUF_CNTL_INIT);

	regmap_update_bits(priv->core->aiu, AIU_MEM_I2S_CONTROL,
			   AIU_MEM_I2S_CONTROL_INIT,
			   0);
	regmap_update_bits(priv->core->aiu, AIU_MEM_I2S_BUF_CNTL,
			   AIU_MEM_I2S_BUF_CNTL_INIT,
			   0);
}

static int meson_aiu_i2s_dma_prepare(struct snd_pcm_substream *substream)
{
	struct meson_aiu_i2s *priv = meson_aiu_i2s_dma_priv(substream);

	__dma_init_mem(priv);

	return 0;
}

static int meson_aiu_i2s_dma_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct meson_aiu_i2s *priv = meson_aiu_i2s_dma_priv(substream);
	int ret;
	u32 burst_num, mem_ctl;
	dma_addr_t end_ptr;

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		return ret;

	/* Setup memory layout */
	if (params_physical_width(params) == 16)
		mem_ctl = AIU_MEM_I2S_CONTROL_MODE_16BIT;
	else
		mem_ctl = 0;

	regmap_update_bits(priv->core->aiu, AIU_MEM_I2S_CONTROL,
			   AIU_MEM_I2S_CONTROL_MODE_16BIT,
			   mem_ctl);

	/* Initialize memory pointers */
	regmap_write(priv->core->aiu, AIU_MEM_I2S_START_PTR, runtime->dma_addr);
	regmap_write(priv->core->aiu, AIU_MEM_I2S_RD_PTR, runtime->dma_addr);

	/* The end pointer is the address of the last valid block */
	end_ptr = runtime->dma_addr + runtime->dma_bytes - AIU_I2S_DMA_BURST;
	regmap_write(priv->core->aiu, AIU_MEM_I2S_END_PTR, end_ptr);

	/* Memory masks */
	burst_num = params_period_bytes(params) / AIU_I2S_DMA_BURST;
	regmap_write(priv->core->aiu, AIU_MEM_I2S_MASKS,
		     AIU_MEM_I2S_MASKS_CH_RD(0xff) |
		     AIU_MEM_I2S_MASKS_CH_MEM(0xff) |
		     AIU_MEM_I2S_MASKS_IRQ_BLOCK(burst_num));

	return 0;
}

static int meson_aiu_i2s_dma_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}


static irqreturn_t meson_aiu_i2s_dma_irq_block(int irq, void *dev_id)
{
	struct snd_pcm_substream *playback = dev_id;

	snd_pcm_period_elapsed(playback);

	return IRQ_HANDLED;
}

static int meson_aiu_i2s_dma_open(struct snd_pcm_substream *substream)
{
	struct meson_aiu_i2s *priv = meson_aiu_i2s_dma_priv(substream);
	int ret;

	snd_soc_set_runtime_hwparams(substream, &meson_aiu_i2s_dma_hw);

	/*
	 * Make sure the buffer and period size are multiple of the DMA burst
	 * size
	 */
	ret = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					 AIU_I2S_DMA_BURST);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					 AIU_I2S_DMA_BURST);
	if (ret)
		return ret;

	/* Request the I2S DDR irq */
	ret = request_irq(priv->irq, meson_aiu_i2s_dma_irq_block, 0,
			  DRV_NAME, substream);
	if (ret)
		return ret;

	/* Power up the i2s fast domain - can't write the registers w/o it */
	ret = clk_prepare_enable(priv->fast);
	if (ret)
		return ret;

	/* Make sure the dma is initially disabled */
	__dma_enable(priv, false);

	return 0;
}

static int meson_aiu_i2s_dma_close(struct snd_pcm_substream *substream)
{
	struct meson_aiu_i2s *priv = meson_aiu_i2s_dma_priv(substream);

	clk_disable_unprepare(priv->fast);
	free_irq(priv->irq, substream);

	return 0;
}

static const struct snd_pcm_ops meson_aiu_i2s_dma_ops = {
	.open =		meson_aiu_i2s_dma_open,
	.close =        meson_aiu_i2s_dma_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	meson_aiu_i2s_dma_hw_params,
	.hw_free =      meson_aiu_i2s_dma_hw_free,
	.prepare =      meson_aiu_i2s_dma_prepare,
	.pointer =	meson_aiu_i2s_dma_pointer,
	.trigger =	meson_aiu_i2s_dma_trigger,
};

static int meson_aiu_i2s_dma_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	size_t size = meson_aiu_i2s_dma_hw.buffer_bytes_max;

	snd_pcm_lib_preallocate_pages_for_all(rtd->pcm,
					      SNDRV_DMA_TYPE_DEV,
					      card->dev, size, size);

	return 0;
}

#define AIU_CLK_CTRL_I2S_DIV_EN			BIT(0)
#define AIU_CLK_CTRL_I2S_DIV_MASK		GENMASK(3, 2)
#define AIU_CLK_CTRL_AOCLK_POLARITY_MASK	BIT(6)
#define AIU_CLK_CTRL_AOCLK_POLARITY_NORMAL	(0 << 6)
#define AIU_CLK_CTRL_AOCLK_POLARITY_INVERTED	(1 << 6)
#define AIU_CLK_CTRL_ALRCLK_POLARITY_MASK	BIT(7)
#define AIU_CLK_CTRL_ALRCLK_POLARITY_NORMAL	(0 << 7)
#define AIU_CLK_CTRL_ALRCLK_POLARITY_INVERTED	(1 << 7)
#define AIU_CLK_CTRL_ALRCLK_SKEW_MASK		GENMASK(9, 8)
#define AIU_CLK_CTRL_ALRCLK_LEFT_J		(0 << 8)
#define AIU_CLK_CTRL_ALRCLK_I2S			(1 << 8)
#define AIU_CLK_CTRL_ALRCLK_RIGHT_J		(2 << 8)
#define AIU_CLK_CTRL_MORE_I2S_DIV_MASK		GENMASK(5, 0)
#define AIU_CLK_CTRL_MORE_I2S_DIV(div)		(((div) - 1) << 0)
#define AIU_CLK_CTRL_MORE_HDMI_TX_SEL_MASK     BIT(6)
#define AIU_CLK_CTRL_MORE_HDMI_TX_I958_CLK     (0 << 6)
#define AIU_CLK_CTRL_MORE_HDMI_TX_INT_CLK      (1 << 6)
#define AIU_CODEC_DAC_LRCLK_CTRL_DIV_MASK	GENMASK(11, 0)
#define AIU_CODEC_DAC_LRCLK_CTRL_DIV(div)	(((div) - 1) << 0)
#define AIU_HDMI_CLK_DATA_CTRL_CLK_SEL_MASK    GENMASK(1, 0)
#define AIU_HDMI_CLK_DATA_CTRL_CLK_DISABLE     (0 << 0)
#define AIU_HDMI_CLK_DATA_CTRL_CLK_PCM         (1 << 0)
#define AIU_HDMI_CLK_DATA_CTRL_CLK_I2S         (2 << 0)
#define AIU_HDMI_CLK_DATA_CTRL_DATA_SEL_MASK   GENMASK(5, 4)
#define AIU_HDMI_CLK_DATA_CTRL_DATA_MUTE       (0 << 4)
#define AIU_HDMI_CLK_DATA_CTRL_DATA_PCM                (1 << 4)
#define AIU_HDMI_CLK_DATA_CTRL_DATA_I2S                (2 << 4)
#define AIU_I2S_DAC_CFG_PAYLOAD_SIZE_MASK	GENMASK(1, 0)
#define AIU_I2S_DAC_CFG_AOCLK_32		(0 << 0)
#define AIU_I2S_DAC_CFG_AOCLK_48		(2 << 0)
#define AIU_I2S_DAC_CFG_AOCLK_64		(3 << 0)
#define AIU_I2S_MISC_HOLD_EN			BIT(2)
#define AIU_I2S_SOURCE_DESC_MODE_8CH		BIT(0)
#define AIU_I2S_SOURCE_DESC_MODE_24BIT		BIT(5)
#define AIU_I2S_SOURCE_DESC_MODE_32BIT		BIT(9)
#define AIU_I2S_SOURCE_DESC_MODE_SPLIT		BIT(11)

static void __hold(struct meson_aiu_i2s *priv, bool enable)
{
	regmap_update_bits(priv->core->aiu, AIU_I2S_MISC,
			   AIU_I2S_MISC_HOLD_EN,
			   enable ? AIU_I2S_MISC_HOLD_EN : 0);
}

static void __divider_enable(struct meson_aiu_i2s *priv, bool enable)
{
	regmap_update_bits(priv->core->aiu, AIU_CLK_CTRL,
			   AIU_CLK_CTRL_I2S_DIV_EN,
			   enable ? AIU_CLK_CTRL_I2S_DIV_EN : 0);
}

static void __playback_start(struct meson_aiu_i2s *priv)
{
	__divider_enable(priv, true);
	__hold(priv, false);
}

static void __playback_stop(struct meson_aiu_i2s *priv, bool clk_force)
{
	__hold(priv, true);
	/* Disable the bit clks if necessary */
	if (clk_force || !priv->bclks_idle)
		__divider_enable(priv, false);
}

static int meson_aiu_i2s_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	struct meson_aiu_i2s *priv = snd_soc_dai_get_drvdata(dai);
	bool clk_force_stop = false;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		__playback_start(priv);
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		clk_force_stop = true;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		__playback_stop(priv, clk_force_stop);
		return 0;

	default:
		return -EINVAL;
	}
}

static int __bclks_set_rate(struct meson_aiu_i2s *priv, unsigned int srate,
			    unsigned int width)
{
	unsigned int fs;

	/* Get the oversampling factor */
	fs = DIV_ROUND_CLOSEST(clk_get_rate(priv->mclk), srate);

	/*
	 * This DAI is usually connected to the dw-hdmi which does not support
	 * bclk being 32 * lrclk or 48 * lrclk
	 * Restrict to blck = 64 * lrclk
	 */
	if (fs % 64)
		return -EINVAL;

	/* Set the divider between lrclk and bclk */
	regmap_update_bits(priv->core->aiu, AIU_I2S_DAC_CFG,
			   AIU_I2S_DAC_CFG_PAYLOAD_SIZE_MASK,
			   AIU_I2S_DAC_CFG_AOCLK_64);

	regmap_update_bits(priv->core->aiu, AIU_CODEC_DAC_LRCLK_CTRL,
			   AIU_CODEC_DAC_LRCLK_CTRL_DIV_MASK,
			   AIU_CODEC_DAC_LRCLK_CTRL_DIV(64));

	/* Use CLK_MORE for the i2s divider */
	regmap_update_bits(priv->core->aiu, AIU_CLK_CTRL,
			   AIU_CLK_CTRL_I2S_DIV_MASK,
			   0);

	regmap_update_bits(priv->core->aiu, AIU_CLK_CTRL_MORE,
			   AIU_CLK_CTRL_MORE_I2S_DIV_MASK,
			   AIU_CLK_CTRL_MORE_I2S_DIV(fs / 64));

	return 0;
}

static int __setup_desc(struct meson_aiu_i2s *priv, unsigned int width,
			unsigned int channels)
{
	u32 desc = 0;

	switch (width) {
	case 24:
		/*
		 * For some reason, 24 bits wide audio don't play well
		 * if the 32 bits mode is not set
		 */
		desc |= (AIU_I2S_SOURCE_DESC_MODE_24BIT |
			 AIU_I2S_SOURCE_DESC_MODE_32BIT);
		break;
	case 16:
		break;

	default:
		return -EINVAL;
	}

	switch (channels) {
	case 2: /* Nothing to do */
		break;
	case 8:
		/* TODO: Still requires testing ... */
		desc |= AIU_I2S_SOURCE_DESC_MODE_8CH;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(priv->core->aiu, AIU_I2S_SOURCE_DESC,
			   AIU_I2S_SOURCE_DESC_MODE_8CH |
			   AIU_I2S_SOURCE_DESC_MODE_24BIT |
			   AIU_I2S_SOURCE_DESC_MODE_32BIT,
			   desc);

	return 0;
}

static int meson_aiu_i2s_dai_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct meson_aiu_i2s *priv = snd_soc_dai_get_drvdata(dai);
	unsigned int width = params_width(params);
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	int ret;

	ret = __setup_desc(priv, width, channels);
	if (ret) {
		dev_err(dai->dev, "Unable set to set i2s description\n");
		return ret;
	}

	ret = __bclks_set_rate(priv, rate, width);
	if (ret) {
		dev_err(dai->dev, "Unable set to the i2s clock rates\n");
		return ret;
	}

       /* Quick and dirty hack for HDMI */
	regmap_update_bits(priv->core->aiu, AIU_HDMI_CLK_DATA_CTRL,
			   AIU_HDMI_CLK_DATA_CTRL_CLK_SEL_MASK |
			   AIU_HDMI_CLK_DATA_CTRL_DATA_SEL_MASK,
			   AIU_HDMI_CLK_DATA_CTRL_CLK_I2S |
			   AIU_HDMI_CLK_DATA_CTRL_DATA_I2S);

	regmap_update_bits(priv->core->aiu, AIU_CLK_CTRL_MORE,
			   AIU_CLK_CTRL_MORE_HDMI_TX_SEL_MASK,
			   AIU_CLK_CTRL_MORE_HDMI_TX_INT_CLK);

	return 0;
}

static int meson_aiu_i2s_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct meson_aiu_i2s *priv = snd_soc_dai_get_drvdata(dai);
	u32 val;

	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS)
		return -EINVAL;

	/* DAI output mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		val = AIU_CLK_CTRL_ALRCLK_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val = AIU_CLK_CTRL_ALRCLK_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val = AIU_CLK_CTRL_ALRCLK_RIGHT_J;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(priv->core->aiu, AIU_CLK_CTRL,
			   AIU_CLK_CTRL_ALRCLK_SKEW_MASK,
			   val);

	/* DAI clock polarity */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		val = AIU_CLK_CTRL_ALRCLK_POLARITY_INVERTED |
			AIU_CLK_CTRL_AOCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		val = AIU_CLK_CTRL_ALRCLK_POLARITY_NORMAL |
			AIU_CLK_CTRL_AOCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		val = AIU_CLK_CTRL_ALRCLK_POLARITY_INVERTED |
			AIU_CLK_CTRL_AOCLK_POLARITY_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		/* Normal clocks */
		val = AIU_CLK_CTRL_ALRCLK_POLARITY_NORMAL |
			AIU_CLK_CTRL_AOCLK_POLARITY_NORMAL;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(priv->core->aiu, AIU_CLK_CTRL,
			   AIU_CLK_CTRL_ALRCLK_POLARITY_MASK |
			   AIU_CLK_CTRL_AOCLK_POLARITY_MASK,
			   val);

	switch (fmt & SND_SOC_DAIFMT_CLOCK_MASK) {
	case SND_SOC_DAIFMT_CONT:
		priv->bclks_idle = true;
		break;
	case SND_SOC_DAIFMT_GATED:
		priv->bclks_idle = false;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int meson_aiu_i2s_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				    unsigned int freq, int dir)
{
	struct meson_aiu_i2s *priv = snd_soc_dai_get_drvdata(dai);
	int ret;

	if (WARN_ON(clk_id != 0))
		return -EINVAL;

	if (dir == SND_SOC_CLOCK_IN)
		return 0;

	ret = clk_set_rate(priv->mclk, freq);
	if (ret) {
		dev_err(dai->dev, "Failed to set sysclk to %uHz", freq);
		return ret;
	}

	return 0;
}

static int meson_aiu_i2s_dai_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct meson_aiu_i2s *priv = snd_soc_dai_get_drvdata(dai);
	int ret;

	/* Power up the i2s fast domain - can't write the registers w/o it */
	ret = clk_prepare_enable(priv->fast);
	if (ret)
		goto out_clk_fast;

	/* Make sure nothing gets out of the DAI yet */
	__hold(priv, true);

	/* I2S encoder needs the mixer interface gate */
	ret = clk_prepare_enable(priv->iface);
	if (ret)
		goto out_clk_iface;

	/* Enable the i2s master clock */
	ret = clk_prepare_enable(priv->mclk);
	if (ret)
		goto out_mclk;

	/* Enable the bit clock gate */
	ret = clk_prepare_enable(priv->bclks);
	if (ret)
		goto out_bclks;

	/* Make sure the interface expect a memory layout we can work with */
	regmap_update_bits(priv->core->aiu, AIU_I2S_SOURCE_DESC,
			   AIU_I2S_SOURCE_DESC_MODE_SPLIT,
			   AIU_I2S_SOURCE_DESC_MODE_SPLIT);

	return 0;

out_bclks:
	clk_disable_unprepare(priv->mclk);
out_mclk:
	clk_disable_unprepare(priv->iface);
out_clk_iface:
	clk_disable_unprepare(priv->fast);
out_clk_fast:
	return ret;
}

static void meson_aiu_i2s_dai_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct meson_aiu_i2s *priv = snd_soc_dai_get_drvdata(dai);

	clk_disable_unprepare(priv->bclks);
	clk_disable_unprepare(priv->mclk);
	clk_disable_unprepare(priv->iface);
	clk_disable_unprepare(priv->fast);
}

static const struct snd_soc_dai_ops meson_aiu_i2s_dai_ops = {
	.startup    = meson_aiu_i2s_dai_startup,
	.shutdown   = meson_aiu_i2s_dai_shutdown,
	.trigger    = meson_aiu_i2s_dai_trigger,
	.hw_params  = meson_aiu_i2s_dai_hw_params,
	.set_fmt    = meson_aiu_i2s_dai_set_fmt,
	.set_sysclk = meson_aiu_i2s_dai_set_sysclk,
};

static struct snd_soc_dai_driver meson_aiu_i2s_dai = {
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S24_LE)
	},
	.ops = &meson_aiu_i2s_dai_ops,
};

static const struct snd_soc_component_driver meson_aiu_i2s_component = {
	.ops = &meson_aiu_i2s_dma_ops,
	.pcm_new = meson_aiu_i2s_dma_new,
	.name	= DRV_NAME,
};

static int meson_aiu_i2s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_aiu_i2s *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->core = dev_get_drvdata(dev->parent);

	priv->fast = devm_clk_get(dev, "fast");
	if (IS_ERR(priv->fast)) {
		if (PTR_ERR(priv->fast) != -EPROBE_DEFER)
			dev_err(dev, "Can't get the i2s fast domain clock\n");
		return PTR_ERR(priv->fast);
	}

	priv->iface = devm_clk_get(dev, "iface");
	if (IS_ERR(priv->iface)) {
		if (PTR_ERR(priv->iface) != -EPROBE_DEFER)
			dev_err(dev, "Can't get i2s dai clock gate\n");
		return PTR_ERR(priv->iface);
	}

	priv->bclks = devm_clk_get(dev, "bclks");
	if (IS_ERR(priv->bclks)) {
		if (PTR_ERR(priv->bclks) != -EPROBE_DEFER)
			dev_err(dev, "Can't get bit clocks gate\n");
		return PTR_ERR(priv->bclks);
	}

	priv->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(priv->mclk)) {
		if (PTR_ERR(priv->mclk) != -EPROBE_DEFER)
			dev_err(dev, "failed to get the i2s master clock\n");
		return PTR_ERR(priv->mclk);
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq <= 0) {
		dev_err(dev, "Can't get i2s ddr irq\n");
		return priv->irq;
	}

	return devm_snd_soc_register_component(dev, &meson_aiu_i2s_component,
					       &meson_aiu_i2s_dai, 1);
}

static const struct of_device_id meson_aiu_i2s_of_match[] = {
	{ .compatible = "amlogic,meson-aiu-i2s", },
	{}
};
MODULE_DEVICE_TABLE(of, meson_aiu_i2s_of_match);

static struct platform_driver meson_aiu_i2s_pdrv = {
	.probe = meson_aiu_i2s_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = meson_aiu_i2s_of_match,
	},
};
module_platform_driver(meson_aiu_i2s_pdrv);

MODULE_DESCRIPTION("Meson AIU i2s ASoC Driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
