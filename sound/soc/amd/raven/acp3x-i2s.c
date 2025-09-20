// SPDX-License-Identifier: GPL-2.0+
//
// AMD ALSA SoC PCM Driver
//
//Copyright 2016 Advanced Micro Devices, Inc.

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <linux/dma-mapping.h>

#include "acp3x.h"

#define DRV_NAME "acp3x_i2s_playcap"

static int acp3x_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
					unsigned int fmt)
{
	struct i2s_dev_data *adata;
	int mode;

	adata = snd_soc_dai_get_drvdata(cpu_dai);
	mode = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
	switch (mode) {
	case SND_SOC_DAIFMT_I2S:
		adata->tdm_mode = TDM_DISABLE;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		adata->tdm_mode = TDM_ENABLE;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int acp3x_i2s_set_tdm_slot(struct snd_soc_dai *cpu_dai,
		u32 tx_mask, u32 rx_mask, int slots, int slot_width)
{
	struct i2s_dev_data *adata;
	u32 frm_len;
	u16 slot_len;

	adata = snd_soc_dai_get_drvdata(cpu_dai);

	/* These values are as per Hardware Spec */
	switch (slot_width) {
	case SLOT_WIDTH_8:
		slot_len = 8;
		break;
	case SLOT_WIDTH_16:
		slot_len = 16;
		break;
	case SLOT_WIDTH_24:
		slot_len = 24;
		break;
	case SLOT_WIDTH_32:
		slot_len = 0;
		break;
	default:
		return -EINVAL;
	}
	frm_len = FRM_LEN | (slots << 15) | (slot_len << 18);
	adata->tdm_fmt = frm_len;
	return 0;
}

static int acp3x_i2s_hwparams(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct i2s_stream_instance *rtd;
	struct snd_soc_pcm_runtime *prtd;
	struct snd_soc_card *card;
	struct acp3x_platform_info *pinfo;
	struct i2s_dev_data *adata;
	u32 val;
	u32 reg_val, frmt_reg;

	prtd = snd_soc_substream_to_rtd(substream);
	rtd = substream->runtime->private_data;
	card = prtd->card;
	adata = snd_soc_dai_get_drvdata(dai);
	pinfo = snd_soc_card_get_drvdata(card);
	if (pinfo) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			rtd->i2s_instance = pinfo->play_i2s_instance;
		else
			rtd->i2s_instance = pinfo->cap_i2s_instance;
	}

	/* These values are as per Hardware Spec */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U8:
	case SNDRV_PCM_FORMAT_S8:
		rtd->xfer_resolution = 0x0;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		rtd->xfer_resolution = 0x02;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		rtd->xfer_resolution = 0x04;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		rtd->xfer_resolution = 0x05;
		break;
	default:
		return -EINVAL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (rtd->i2s_instance) {
		case I2S_BT_INSTANCE:
			reg_val = mmACP_BTTDM_ITER;
			frmt_reg = mmACP_BTTDM_TXFRMT;
			break;
		case I2S_SP_INSTANCE:
		default:
			reg_val = mmACP_I2STDM_ITER;
			frmt_reg = mmACP_I2STDM_TXFRMT;
		}
	} else {
		switch (rtd->i2s_instance) {
		case I2S_BT_INSTANCE:
			reg_val = mmACP_BTTDM_IRER;
			frmt_reg = mmACP_BTTDM_RXFRMT;
			break;
		case I2S_SP_INSTANCE:
		default:
			reg_val = mmACP_I2STDM_IRER;
			frmt_reg = mmACP_I2STDM_RXFRMT;
		}
	}
	if (adata->tdm_mode) {
		val = rv_readl(rtd->acp3x_base + reg_val);
		rv_writel(val | 0x2, rtd->acp3x_base + reg_val);
		rv_writel(adata->tdm_fmt, rtd->acp3x_base + frmt_reg);
	}
	val = rv_readl(rtd->acp3x_base + reg_val);
	val &= ~ACP3x_ITER_IRER_SAMP_LEN_MASK;
	val = val | (rtd->xfer_resolution  << 3);
	rv_writel(val, rtd->acp3x_base + reg_val);
	return 0;
}

static int acp3x_i2s_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct i2s_stream_instance *rtd;
	u32 val, period_bytes, reg_val, ier_val, water_val;
	u32 buf_size, buf_reg;
	int ret;

	rtd = substream->runtime->private_data;
	period_bytes = frames_to_bytes(substream->runtime,
			substream->runtime->period_size);
	buf_size = frames_to_bytes(substream->runtime,
			substream->runtime->buffer_size);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		rtd->bytescount = acp_get_byte_count(rtd,
						substream->stream);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			switch (rtd->i2s_instance) {
			case I2S_BT_INSTANCE:
				water_val =
					mmACP_BT_TX_INTR_WATERMARK_SIZE;
				reg_val = mmACP_BTTDM_ITER;
				ier_val = mmACP_BTTDM_IER;
				buf_reg = mmACP_BT_TX_RINGBUFSIZE;
				break;
			case I2S_SP_INSTANCE:
			default:
				water_val =
					mmACP_I2S_TX_INTR_WATERMARK_SIZE;
				reg_val = mmACP_I2STDM_ITER;
				ier_val = mmACP_I2STDM_IER;
				buf_reg = mmACP_I2S_TX_RINGBUFSIZE;
			}
		} else {
			switch (rtd->i2s_instance) {
			case I2S_BT_INSTANCE:
				water_val =
					mmACP_BT_RX_INTR_WATERMARK_SIZE;
				reg_val = mmACP_BTTDM_IRER;
				ier_val = mmACP_BTTDM_IER;
				buf_reg = mmACP_BT_RX_RINGBUFSIZE;
				break;
			case I2S_SP_INSTANCE:
			default:
				water_val =
					mmACP_I2S_RX_INTR_WATERMARK_SIZE;
				reg_val = mmACP_I2STDM_IRER;
				ier_val = mmACP_I2STDM_IER;
				buf_reg = mmACP_I2S_RX_RINGBUFSIZE;
			}
		}
		rv_writel(period_bytes, rtd->acp3x_base + water_val);
		rv_writel(buf_size, rtd->acp3x_base + buf_reg);
		val = rv_readl(rtd->acp3x_base + reg_val);
		val = val | BIT(0);
		rv_writel(val, rtd->acp3x_base + reg_val);
		rv_writel(1, rtd->acp3x_base + ier_val);
		ret = 0;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			switch (rtd->i2s_instance) {
			case I2S_BT_INSTANCE:
				reg_val = mmACP_BTTDM_ITER;
				break;
			case I2S_SP_INSTANCE:
			default:
				reg_val = mmACP_I2STDM_ITER;
			}

		} else {
			switch (rtd->i2s_instance) {
			case I2S_BT_INSTANCE:
				reg_val = mmACP_BTTDM_IRER;
				break;
			case I2S_SP_INSTANCE:
			default:
				reg_val = mmACP_I2STDM_IRER;
			}
		}
		val = rv_readl(rtd->acp3x_base + reg_val);
		val = val & ~BIT(0);
		rv_writel(val, rtd->acp3x_base + reg_val);

		if (!(rv_readl(rtd->acp3x_base + mmACP_BTTDM_ITER) & BIT(0)) &&
		     !(rv_readl(rtd->acp3x_base + mmACP_BTTDM_IRER) & BIT(0)))
			rv_writel(0, rtd->acp3x_base + mmACP_BTTDM_IER);
		if (!(rv_readl(rtd->acp3x_base + mmACP_I2STDM_ITER) & BIT(0)) &&
		     !(rv_readl(rtd->acp3x_base + mmACP_I2STDM_IRER) & BIT(0)))
			rv_writel(0, rtd->acp3x_base + mmACP_I2STDM_IER);
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct snd_soc_dai_ops acp3x_i2s_dai_ops = {
	.hw_params = acp3x_i2s_hwparams,
	.trigger = acp3x_i2s_trigger,
	.set_fmt = acp3x_i2s_set_fmt,
	.set_tdm_slot = acp3x_i2s_set_tdm_slot,
};

static const struct snd_soc_component_driver acp3x_dai_component = {
	.name			= DRV_NAME,
	.legacy_dai_naming	= 1,
};

static struct snd_soc_dai_driver acp3x_i2s_dai = {
	.playback = {
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 8,
		.rate_min = 8000,
		.rate_max = 96000,
	},
	.capture = {
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 8000,
		.rate_max = 48000,
	},
	.ops = &acp3x_i2s_dai_ops,
};

static int acp3x_dai_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct i2s_dev_data *adata;
	int ret;

	adata = devm_kzalloc(&pdev->dev, sizeof(struct i2s_dev_data),
			GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_MEM FAILED\n");
		return -ENOMEM;
	}
	adata->acp3x_base = devm_ioremap(&pdev->dev, res->start,
						resource_size(res));
	if (!adata->acp3x_base)
		return -ENOMEM;

	adata->i2s_irq = res->start;
	dev_set_drvdata(&pdev->dev, adata);
	ret = devm_snd_soc_register_component(&pdev->dev,
			&acp3x_dai_component, &acp3x_i2s_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Fail to register acp i2s dai\n");
		return -ENODEV;
	}
	return 0;
}

static struct platform_driver acp3x_dai_driver = {
	.probe = acp3x_dai_probe,
	.driver = {
		.name = "acp3x_i2s_playcap",
	},
};

module_platform_driver(acp3x_dai_driver);

MODULE_AUTHOR("Vishnuvardhanrao.Ravulapati@amd.com");
MODULE_DESCRIPTION("AMD ACP 3.x PCM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:"DRV_NAME);
