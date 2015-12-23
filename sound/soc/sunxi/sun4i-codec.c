/*
 * Copyright 2014 Emilio López <emilio@elopez.com.ar>
 * Copyright 2014 Jon Smirl <jonsmirl@gmail.com>
 * Copyright 2015 Maxime Ripard <maxime.ripard@free-electrons.com>
 * Copyright 2015 Adam Sampson <ats@offog.org>
 *
 * Based on the Allwinner SDK driver, released under the GPL.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>

/* Codec DAC register offsets and bit fields */
#define SUN4I_CODEC_DAC_DPC			(0x00)
#define SUN4I_CODEC_DAC_DPC_EN_DA			(31)
#define SUN4I_CODEC_DAC_DPC_DVOL			(12)
#define SUN4I_CODEC_DAC_FIFOC			(0x04)
#define SUN4I_CODEC_DAC_FIFOC_DAC_FS			(29)
#define SUN4I_CODEC_DAC_FIFOC_FIR_VERSION		(28)
#define SUN4I_CODEC_DAC_FIFOC_SEND_LASAT		(26)
#define SUN4I_CODEC_DAC_FIFOC_TX_FIFO_MODE		(24)
#define SUN4I_CODEC_DAC_FIFOC_DRQ_CLR_CNT		(21)
#define SUN4I_CODEC_DAC_FIFOC_TX_TRIG_LEVEL		(8)
#define SUN4I_CODEC_DAC_FIFOC_MONO_EN			(6)
#define SUN4I_CODEC_DAC_FIFOC_TX_SAMPLE_BITS		(5)
#define SUN4I_CODEC_DAC_FIFOC_DAC_DRQ_EN		(4)
#define SUN4I_CODEC_DAC_FIFOC_FIFO_FLUSH		(0)
#define SUN4I_CODEC_DAC_FIFOS			(0x08)
#define SUN4I_CODEC_DAC_TXDATA			(0x0c)
#define SUN4I_CODEC_DAC_ACTL			(0x10)
#define SUN4I_CODEC_DAC_ACTL_DACAENR			(31)
#define SUN4I_CODEC_DAC_ACTL_DACAENL			(30)
#define SUN4I_CODEC_DAC_ACTL_MIXEN			(29)
#define SUN4I_CODEC_DAC_ACTL_LDACLMIXS			(15)
#define SUN4I_CODEC_DAC_ACTL_RDACRMIXS			(14)
#define SUN4I_CODEC_DAC_ACTL_LDACRMIXS			(13)
#define SUN4I_CODEC_DAC_ACTL_DACPAS			(8)
#define SUN4I_CODEC_DAC_ACTL_MIXPAS			(7)
#define SUN4I_CODEC_DAC_ACTL_PA_MUTE			(6)
#define SUN4I_CODEC_DAC_ACTL_PA_VOL			(0)
#define SUN4I_CODEC_DAC_TUNE			(0x14)
#define SUN4I_CODEC_DAC_DEBUG			(0x18)

/* Codec ADC register offsets and bit fields */
#define SUN4I_CODEC_ADC_FIFOC			(0x1c)
#define SUN4I_CODEC_ADC_FIFOC_ADC_FS			(29)
#define SUN4I_CODEC_ADC_FIFOC_EN_AD			(28)
#define SUN4I_CODEC_ADC_FIFOC_RX_FIFO_MODE		(24)
#define SUN4I_CODEC_ADC_FIFOC_RX_TRIG_LEVEL		(8)
#define SUN4I_CODEC_ADC_FIFOC_MONO_EN			(7)
#define SUN4I_CODEC_ADC_FIFOC_RX_SAMPLE_BITS		(6)
#define SUN4I_CODEC_ADC_FIFOC_ADC_DRQ_EN		(4)
#define SUN4I_CODEC_ADC_FIFOC_FIFO_FLUSH		(0)
#define SUN4I_CODEC_ADC_FIFOS			(0x20)
#define SUN4I_CODEC_ADC_RXDATA			(0x24)
#define SUN4I_CODEC_ADC_ACTL			(0x28)
#define SUN4I_CODEC_ADC_ACTL_ADC_R_EN			(31)
#define SUN4I_CODEC_ADC_ACTL_ADC_L_EN			(30)
#define SUN4I_CODEC_ADC_ACTL_PREG1EN			(29)
#define SUN4I_CODEC_ADC_ACTL_PREG2EN			(28)
#define SUN4I_CODEC_ADC_ACTL_VMICEN			(27)
#define SUN4I_CODEC_ADC_ACTL_VADCG			(20)
#define SUN4I_CODEC_ADC_ACTL_ADCIS			(17)
#define SUN4I_CODEC_ADC_ACTL_PA_EN			(4)
#define SUN4I_CODEC_ADC_ACTL_DDE			(3)
#define SUN4I_CODEC_ADC_DEBUG			(0x2c)

/* Other various ADC registers */
#define SUN4I_CODEC_DAC_TXCNT			(0x30)
#define SUN4I_CODEC_ADC_RXCNT			(0x34)
#define SUN4I_CODEC_AC_SYS_VERI			(0x38)
#define SUN4I_CODEC_AC_MIC_PHONE_CAL		(0x3c)

struct sun4i_codec {
	struct device	*dev;
	struct regmap	*regmap;
	struct clk	*clk_apb;
	struct clk	*clk_module;
	struct gpio_desc *gpio_pa;

	struct snd_dmaengine_dai_dma_data	capture_dma_data;
	struct snd_dmaengine_dai_dma_data	playback_dma_data;
};

static void sun4i_codec_start_playback(struct sun4i_codec *scodec)
{
	/* Flush TX FIFO */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   BIT(SUN4I_CODEC_DAC_FIFOC_FIFO_FLUSH),
			   BIT(SUN4I_CODEC_DAC_FIFOC_FIFO_FLUSH));

	/* Enable DAC DRQ */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   BIT(SUN4I_CODEC_DAC_FIFOC_DAC_DRQ_EN),
			   BIT(SUN4I_CODEC_DAC_FIFOC_DAC_DRQ_EN));
}

static void sun4i_codec_stop_playback(struct sun4i_codec *scodec)
{
	/* Disable DAC DRQ */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   BIT(SUN4I_CODEC_DAC_FIFOC_DAC_DRQ_EN),
			   0);
}

static void sun4i_codec_start_capture(struct sun4i_codec *scodec)
{
	/* Enable ADC DRQ */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_ADC_FIFOC,
			   BIT(SUN4I_CODEC_ADC_FIFOC_ADC_DRQ_EN),
			   BIT(SUN4I_CODEC_ADC_FIFOC_ADC_DRQ_EN));
}

static void sun4i_codec_stop_capture(struct sun4i_codec *scodec)
{
	/* Disable ADC DRQ */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_ADC_FIFOC,
			   BIT(SUN4I_CODEC_ADC_FIFOC_ADC_DRQ_EN), 0);
}

static int sun4i_codec_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_codec *scodec = snd_soc_card_get_drvdata(rtd->card);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sun4i_codec_start_playback(scodec);
		else
			sun4i_codec_start_capture(scodec);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sun4i_codec_stop_playback(scodec);
		else
			sun4i_codec_stop_capture(scodec);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int sun4i_codec_prepare_capture(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_codec *scodec = snd_soc_card_get_drvdata(rtd->card);


	/* Flush RX FIFO */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_ADC_FIFOC,
			   BIT(SUN4I_CODEC_ADC_FIFOC_FIFO_FLUSH),
			   BIT(SUN4I_CODEC_ADC_FIFOC_FIFO_FLUSH));


	/* Set RX FIFO trigger level */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_ADC_FIFOC,
			   0xf << SUN4I_CODEC_ADC_FIFOC_RX_TRIG_LEVEL,
			   0x7 << SUN4I_CODEC_ADC_FIFOC_RX_TRIG_LEVEL);

	/*
	 * FIXME: Undocumented in the datasheet, but
	 *        Allwinner's code mentions that it is related
	 *        related to microphone gain
	 */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_ADC_ACTL,
			   0x3 << 25,
			   0x1 << 25);

	if (of_device_is_compatible(scodec->dev->of_node,
				    "allwinner,sun7i-a20-codec"))
		/* FIXME: Undocumented bits */
		regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_TUNE,
				   0x3 << 8,
				   0x1 << 8);

	/* Fill most significant bits with valid data MSB */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_ADC_FIFOC,
			   BIT(SUN4I_CODEC_ADC_FIFOC_RX_FIFO_MODE),
			   BIT(SUN4I_CODEC_ADC_FIFOC_RX_FIFO_MODE));

	return 0;
}

static int sun4i_codec_prepare_playback(struct snd_pcm_substream *substream,
					struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_codec *scodec = snd_soc_card_get_drvdata(rtd->card);
	u32 val;

	/* Flush the TX FIFO */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   BIT(SUN4I_CODEC_DAC_FIFOC_FIFO_FLUSH),
			   BIT(SUN4I_CODEC_DAC_FIFOC_FIFO_FLUSH));

	/* Set TX FIFO Empty Trigger Level */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   0x3f << SUN4I_CODEC_DAC_FIFOC_TX_TRIG_LEVEL,
			   0xf << SUN4I_CODEC_DAC_FIFOC_TX_TRIG_LEVEL);

	if (substream->runtime->rate > 32000)
		/* Use 64 bits FIR filter */
		val = 0;
	else
		/* Use 32 bits FIR filter */
		val = BIT(SUN4I_CODEC_DAC_FIFOC_FIR_VERSION);

	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   BIT(SUN4I_CODEC_DAC_FIFOC_FIR_VERSION),
			   val);

	/* Send zeros when we have an underrun */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   BIT(SUN4I_CODEC_DAC_FIFOC_SEND_LASAT),
			   0);

	return 0;
};

static int sun4i_codec_prepare(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return sun4i_codec_prepare_playback(substream, dai);

	return sun4i_codec_prepare_capture(substream, dai);
}

static unsigned long sun4i_codec_get_mod_freq(struct snd_pcm_hw_params *params)
{
	unsigned int rate = params_rate(params);

	switch (rate) {
	case 176400:
	case 88200:
	case 44100:
	case 33075:
	case 22050:
	case 14700:
	case 11025:
	case 7350:
		return 22579200;

	case 192000:
	case 96000:
	case 48000:
	case 32000:
	case 24000:
	case 16000:
	case 12000:
	case 8000:
		return 24576000;

	default:
		return 0;
	}
}

static int sun4i_codec_get_hw_rate(struct snd_pcm_hw_params *params)
{
	unsigned int rate = params_rate(params);

	switch (rate) {
	case 192000:
	case 176400:
		return 6;

	case 96000:
	case 88200:
		return 7;

	case 48000:
	case 44100:
		return 0;

	case 32000:
	case 33075:
		return 1;

	case 24000:
	case 22050:
		return 2;

	case 16000:
	case 14700:
		return 3;

	case 12000:
	case 11025:
		return 4;

	case 8000:
	case 7350:
		return 5;

	default:
		return -EINVAL;
	}
}

static int sun4i_codec_hw_params_capture(struct sun4i_codec *scodec,
					 struct snd_pcm_hw_params *params,
					 unsigned int hwrate)
{
	/* Set ADC sample rate */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_ADC_FIFOC,
			   7 << SUN4I_CODEC_ADC_FIFOC_ADC_FS,
			   hwrate << SUN4I_CODEC_ADC_FIFOC_ADC_FS);

	/* Set the number of channels we want to use */
	if (params_channels(params) == 1)
		regmap_update_bits(scodec->regmap, SUN4I_CODEC_ADC_FIFOC,
				   BIT(SUN4I_CODEC_ADC_FIFOC_MONO_EN),
				   BIT(SUN4I_CODEC_ADC_FIFOC_MONO_EN));
	else
		regmap_update_bits(scodec->regmap, SUN4I_CODEC_ADC_FIFOC,
				   BIT(SUN4I_CODEC_ADC_FIFOC_MONO_EN), 0);

	return 0;
}

static int sun4i_codec_hw_params_playback(struct sun4i_codec *scodec,
					  struct snd_pcm_hw_params *params,
					  unsigned int hwrate)
{
	u32 val;

	/* Set DAC sample rate */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   7 << SUN4I_CODEC_DAC_FIFOC_DAC_FS,
			   hwrate << SUN4I_CODEC_DAC_FIFOC_DAC_FS);

	/* Set the number of channels we want to use */
	if (params_channels(params) == 1)
		val = BIT(SUN4I_CODEC_DAC_FIFOC_MONO_EN);
	else
		val = 0;

	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   BIT(SUN4I_CODEC_DAC_FIFOC_MONO_EN),
			   val);

	/* Set the number of sample bits to either 16 or 24 bits */
	if (hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS)->min == 32) {
		regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
				   BIT(SUN4I_CODEC_DAC_FIFOC_TX_SAMPLE_BITS),
				   BIT(SUN4I_CODEC_DAC_FIFOC_TX_SAMPLE_BITS));

		/* Set TX FIFO mode to padding the LSBs with 0 */
		regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
				   BIT(SUN4I_CODEC_DAC_FIFOC_TX_FIFO_MODE),
				   0);

		scodec->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	} else {
		regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
				   BIT(SUN4I_CODEC_DAC_FIFOC_TX_SAMPLE_BITS),
				   0);

		/* Set TX FIFO mode to repeat the MSB */
		regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
				   BIT(SUN4I_CODEC_DAC_FIFOC_TX_FIFO_MODE),
				   BIT(SUN4I_CODEC_DAC_FIFOC_TX_FIFO_MODE));

		scodec->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	}

	return 0;
}

static int sun4i_codec_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_codec *scodec = snd_soc_card_get_drvdata(rtd->card);
	unsigned long clk_freq;
	int ret, hwrate;

	clk_freq = sun4i_codec_get_mod_freq(params);
	if (!clk_freq)
		return -EINVAL;

	ret = clk_set_rate(scodec->clk_module, clk_freq);
	if (ret)
		return ret;

	hwrate = sun4i_codec_get_hw_rate(params);
	if (hwrate < 0)
		return hwrate;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return sun4i_codec_hw_params_playback(scodec, params,
						      hwrate);

	return sun4i_codec_hw_params_capture(scodec, params,
					     hwrate);
}

static int sun4i_codec_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_codec *scodec = snd_soc_card_get_drvdata(rtd->card);

	/*
	 * Stop issuing DRQ when we have room for less than 16 samples
	 * in our TX FIFO
	 */
	regmap_update_bits(scodec->regmap, SUN4I_CODEC_DAC_FIFOC,
			   3 << SUN4I_CODEC_DAC_FIFOC_DRQ_CLR_CNT,
			   3 << SUN4I_CODEC_DAC_FIFOC_DRQ_CLR_CNT);

	return clk_prepare_enable(scodec->clk_module);
}

static void sun4i_codec_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_codec *scodec = snd_soc_card_get_drvdata(rtd->card);

	clk_disable_unprepare(scodec->clk_module);
}

static const struct snd_soc_dai_ops sun4i_codec_dai_ops = {
	.startup	= sun4i_codec_startup,
	.shutdown	= sun4i_codec_shutdown,
	.trigger	= sun4i_codec_trigger,
	.hw_params	= sun4i_codec_hw_params,
	.prepare	= sun4i_codec_prepare,
};

static struct snd_soc_dai_driver sun4i_codec_dai = {
	.name	= "Codec",
	.ops	= &sun4i_codec_dai_ops,
	.playback = {
		.stream_name	= "Codec Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rate_min	= 8000,
		.rate_max	= 192000,
		.rates		= SNDRV_PCM_RATE_8000_48000 |
				  SNDRV_PCM_RATE_96000 |
				  SNDRV_PCM_RATE_192000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits	= 24,
	},
	.capture = {
		.stream_name	= "Codec Capture",
		.channels_min	= 1,
		.channels_max	= 2,
		.rate_min	= 8000,
		.rate_max	= 192000,
		.rates		= SNDRV_PCM_RATE_8000_48000 |
				  SNDRV_PCM_RATE_96000 |
				  SNDRV_PCM_RATE_192000 |
				  SNDRV_PCM_RATE_KNOT,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits	= 24,
	},
};

/*** Codec ***/
static const struct snd_kcontrol_new sun4i_codec_pa_mute =
	SOC_DAPM_SINGLE("Switch", SUN4I_CODEC_DAC_ACTL,
			SUN4I_CODEC_DAC_ACTL_PA_MUTE, 1, 0);

static DECLARE_TLV_DB_SCALE(sun4i_codec_pa_volume_scale, -6300, 100, 1);

static const struct snd_kcontrol_new sun4i_codec_widgets[] = {
	SOC_SINGLE_TLV("Power Amplifier Volume", SUN4I_CODEC_DAC_ACTL,
		       SUN4I_CODEC_DAC_ACTL_PA_VOL, 0x3F, 0,
		       sun4i_codec_pa_volume_scale),
};

static const struct snd_kcontrol_new sun4i_codec_left_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left DAC Playback Switch", SUN4I_CODEC_DAC_ACTL,
			SUN4I_CODEC_DAC_ACTL_LDACLMIXS, 1, 0),
};

static const struct snd_kcontrol_new sun4i_codec_right_mixer_controls[] = {
	SOC_DAPM_SINGLE("Right DAC Playback Switch", SUN4I_CODEC_DAC_ACTL,
			SUN4I_CODEC_DAC_ACTL_RDACRMIXS, 1, 0),
	SOC_DAPM_SINGLE("Left DAC Playback Switch", SUN4I_CODEC_DAC_ACTL,
			SUN4I_CODEC_DAC_ACTL_LDACRMIXS, 1, 0),
};

static const struct snd_kcontrol_new sun4i_codec_pa_mixer_controls[] = {
	SOC_DAPM_SINGLE("DAC Playback Switch", SUN4I_CODEC_DAC_ACTL,
			SUN4I_CODEC_DAC_ACTL_DACPAS, 1, 0),
	SOC_DAPM_SINGLE("Mixer Playback Switch", SUN4I_CODEC_DAC_ACTL,
			SUN4I_CODEC_DAC_ACTL_MIXPAS, 1, 0),
};

static const struct snd_soc_dapm_widget sun4i_codec_codec_dapm_widgets[] = {
	/* Digital parts of the ADCs */
	SND_SOC_DAPM_SUPPLY("ADC", SUN4I_CODEC_ADC_FIFOC,
			    SUN4I_CODEC_ADC_FIFOC_EN_AD, 0,
			    NULL, 0),

	/* Digital parts of the DACs */
	SND_SOC_DAPM_SUPPLY("DAC", SUN4I_CODEC_DAC_DPC,
			    SUN4I_CODEC_DAC_DPC_EN_DA, 0,
			    NULL, 0),

	/* Analog parts of the ADCs */
	SND_SOC_DAPM_ADC("Left ADC", "Codec Capture", SUN4I_CODEC_ADC_ACTL,
			 SUN4I_CODEC_ADC_ACTL_ADC_L_EN, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Codec Capture", SUN4I_CODEC_ADC_ACTL,
			 SUN4I_CODEC_ADC_ACTL_ADC_R_EN, 0),

	/* Analog parts of the DACs */
	SND_SOC_DAPM_DAC("Left DAC", "Codec Playback", SUN4I_CODEC_DAC_ACTL,
			 SUN4I_CODEC_DAC_ACTL_DACAENL, 0),
	SND_SOC_DAPM_DAC("Right DAC", "Codec Playback", SUN4I_CODEC_DAC_ACTL,
			 SUN4I_CODEC_DAC_ACTL_DACAENR, 0),

	/* Mixers */
	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
			   sun4i_codec_left_mixer_controls,
			   ARRAY_SIZE(sun4i_codec_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
			   sun4i_codec_right_mixer_controls,
			   ARRAY_SIZE(sun4i_codec_right_mixer_controls)),

	/* Global Mixer Enable */
	SND_SOC_DAPM_SUPPLY("Mixer Enable", SUN4I_CODEC_DAC_ACTL,
			    SUN4I_CODEC_DAC_ACTL_MIXEN, 0, NULL, 0),

	/* VMIC */
	SND_SOC_DAPM_SUPPLY("VMIC", SUN4I_CODEC_ADC_ACTL,
			    SUN4I_CODEC_ADC_ACTL_VMICEN, 0, NULL, 0),

	/* Mic Pre-Amplifiers */
	SND_SOC_DAPM_PGA("MIC1 Pre-Amplifier", SUN4I_CODEC_ADC_ACTL,
			 SUN4I_CODEC_ADC_ACTL_PREG1EN, 0, NULL, 0),

	/* Power Amplifier */
	SND_SOC_DAPM_MIXER("Power Amplifier", SUN4I_CODEC_ADC_ACTL,
			   SUN4I_CODEC_ADC_ACTL_PA_EN, 0,
			   sun4i_codec_pa_mixer_controls,
			   ARRAY_SIZE(sun4i_codec_pa_mixer_controls)),
	SND_SOC_DAPM_SWITCH("Power Amplifier Mute", SND_SOC_NOPM, 0, 0,
			    &sun4i_codec_pa_mute),

	SND_SOC_DAPM_INPUT("Mic1"),

	SND_SOC_DAPM_OUTPUT("HP Right"),
	SND_SOC_DAPM_OUTPUT("HP Left"),
};

static const struct snd_soc_dapm_route sun4i_codec_codec_dapm_routes[] = {
	/* Left ADC / DAC Routes */
	{ "Left ADC", NULL, "ADC" },
	{ "Left DAC", NULL, "DAC" },

	/* Right ADC / DAC Routes */
	{ "Right ADC", NULL, "ADC" },
	{ "Right DAC", NULL, "DAC" },

	/* Right Mixer Routes */
	{ "Right Mixer", NULL, "Mixer Enable" },
	{ "Right Mixer", "Left DAC Playback Switch", "Left DAC" },
	{ "Right Mixer", "Right DAC Playback Switch", "Right DAC" },

	/* Left Mixer Routes */
	{ "Left Mixer", NULL, "Mixer Enable" },
	{ "Left Mixer", "Left DAC Playback Switch", "Left DAC" },

	/* Power Amplifier Routes */
	{ "Power Amplifier", "Mixer Playback Switch", "Left Mixer" },
	{ "Power Amplifier", "Mixer Playback Switch", "Right Mixer" },
	{ "Power Amplifier", "DAC Playback Switch", "Left DAC" },
	{ "Power Amplifier", "DAC Playback Switch", "Right DAC" },

	/* Headphone Output Routes */
	{ "Power Amplifier Mute", "Switch", "Power Amplifier" },
	{ "HP Right", NULL, "Power Amplifier Mute" },
	{ "HP Left", NULL, "Power Amplifier Mute" },

	/* Mic1 Routes */
	{ "Left ADC", NULL, "MIC1 Pre-Amplifier" },
	{ "Right ADC", NULL, "MIC1 Pre-Amplifier" },
	{ "MIC1 Pre-Amplifier", NULL, "Mic1"},
	{ "Mic1", NULL, "VMIC" },
};

static struct snd_soc_codec_driver sun4i_codec_codec = {
	.controls		= sun4i_codec_widgets,
	.num_controls		= ARRAY_SIZE(sun4i_codec_widgets),
	.dapm_widgets		= sun4i_codec_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sun4i_codec_codec_dapm_widgets),
	.dapm_routes		= sun4i_codec_codec_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(sun4i_codec_codec_dapm_routes),
};

static const struct snd_soc_component_driver sun4i_codec_component = {
	.name = "sun4i-codec",
};

#define SUN4I_CODEC_RATES	SNDRV_PCM_RATE_8000_192000
#define SUN4I_CODEC_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_S32_LE)

static int sun4i_codec_dai_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(dai);
	struct sun4i_codec *scodec = snd_soc_card_get_drvdata(card);

	snd_soc_dai_init_dma_data(dai, &scodec->playback_dma_data,
				  &scodec->capture_dma_data);

	return 0;
}

static struct snd_soc_dai_driver dummy_cpu_dai = {
	.name	= "sun4i-codec-cpu-dai",
	.probe	= sun4i_codec_dai_probe,
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= SUN4I_CODEC_RATES,
		.formats	= SUN4I_CODEC_FORMATS,
		.sig_bits	= 24,
	},
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates 		= SUN4I_CODEC_RATES,
		.formats 	= SUN4I_CODEC_FORMATS,
		.sig_bits	= 24,
	 },
};

static const struct regmap_config sun4i_codec_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= SUN4I_CODEC_AC_MIC_PHONE_CAL,
};

static const struct of_device_id sun4i_codec_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-codec" },
	{ .compatible = "allwinner,sun7i-a20-codec" },
	{}
};
MODULE_DEVICE_TABLE(of, sun4i_codec_of_match);

static struct snd_soc_dai_link *sun4i_codec_create_link(struct device *dev,
							int *num_links)
{
	struct snd_soc_dai_link *link = devm_kzalloc(dev, sizeof(*link),
						     GFP_KERNEL);
	if (!link)
		return NULL;

	link->name		= "cdc";
	link->stream_name	= "CDC PCM";
	link->codec_dai_name	= "Codec";
	link->cpu_dai_name	= dev_name(dev);
	link->codec_name	= dev_name(dev);
	link->platform_name	= dev_name(dev);
	link->dai_fmt		= SND_SOC_DAIFMT_I2S;

	*num_links = 1;

	return link;
};

static int sun4i_codec_spk_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *k, int event)
{
	struct sun4i_codec *scodec = snd_soc_card_get_drvdata(w->dapm->card);

	if (scodec->gpio_pa)
		gpiod_set_value_cansleep(scodec->gpio_pa,
					 !!SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static const struct snd_soc_dapm_widget sun4i_codec_card_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", sun4i_codec_spk_event),
};

static const struct snd_soc_dapm_route sun4i_codec_card_dapm_routes[] = {
	{ "Speaker", NULL, "HP Right" },
	{ "Speaker", NULL, "HP Left" },
};

static struct snd_soc_card *sun4i_codec_create_card(struct device *dev)
{
	struct snd_soc_card *card;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return NULL;

	card->dai_link = sun4i_codec_create_link(dev, &card->num_links);
	if (!card->dai_link)
		return NULL;

	card->dev		= dev;
	card->name		= "sun4i-codec";
	card->dapm_widgets	= sun4i_codec_card_dapm_widgets;
	card->num_dapm_widgets	= ARRAY_SIZE(sun4i_codec_card_dapm_widgets);
	card->dapm_routes	= sun4i_codec_card_dapm_routes;
	card->num_dapm_routes	= ARRAY_SIZE(sun4i_codec_card_dapm_routes);

	return card;
};

static int sun4i_codec_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct sun4i_codec *scodec;
	struct resource *res;
	void __iomem *base;
	int ret;

	scodec = devm_kzalloc(&pdev->dev, sizeof(*scodec), GFP_KERNEL);
	if (!scodec)
		return -ENOMEM;

	scodec->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "Failed to map the registers\n");
		return PTR_ERR(base);
	}

	scodec->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					     &sun4i_codec_regmap_config);
	if (IS_ERR(scodec->regmap)) {
		dev_err(&pdev->dev, "Failed to create our regmap\n");
		return PTR_ERR(scodec->regmap);
	}

	/* Get the clocks from the DT */
	scodec->clk_apb = devm_clk_get(&pdev->dev, "apb");
	if (IS_ERR(scodec->clk_apb)) {
		dev_err(&pdev->dev, "Failed to get the APB clock\n");
		return PTR_ERR(scodec->clk_apb);
	}

	scodec->clk_module = devm_clk_get(&pdev->dev, "codec");
	if (IS_ERR(scodec->clk_module)) {
		dev_err(&pdev->dev, "Failed to get the module clock\n");
		return PTR_ERR(scodec->clk_module);
	}

	/* Enable the bus clock */
	if (clk_prepare_enable(scodec->clk_apb)) {
		dev_err(&pdev->dev, "Failed to enable the APB clock\n");
		return -EINVAL;
	}

	scodec->gpio_pa = devm_gpiod_get_optional(&pdev->dev, "allwinner,pa",
						  GPIOD_OUT_LOW);
	if (IS_ERR(scodec->gpio_pa)) {
		ret = PTR_ERR(scodec->gpio_pa);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get pa gpio: %d\n", ret);
		return ret;
	}

	/* DMA configuration for TX FIFO */
	scodec->playback_dma_data.addr = res->start + SUN4I_CODEC_DAC_TXDATA;
	scodec->playback_dma_data.maxburst = 4;
	scodec->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	/* DMA configuration for RX FIFO */
	scodec->capture_dma_data.addr = res->start + SUN4I_CODEC_ADC_RXDATA;
	scodec->capture_dma_data.maxburst = 4;
	scodec->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	ret = snd_soc_register_codec(&pdev->dev, &sun4i_codec_codec,
				     &sun4i_codec_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register our codec\n");
		goto err_clk_disable;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &sun4i_codec_component,
					      &dummy_cpu_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register our DAI\n");
		goto err_unregister_codec;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register against DMAEngine\n");
		goto err_unregister_codec;
	}

	card = sun4i_codec_create_card(&pdev->dev);
	if (!card) {
		dev_err(&pdev->dev, "Failed to create our card\n");
		goto err_unregister_codec;
	}

	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, scodec);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register our card\n");
		goto err_unregister_codec;
	}

	return 0;

err_unregister_codec:
	snd_soc_unregister_codec(&pdev->dev);
err_clk_disable:
	clk_disable_unprepare(scodec->clk_apb);
	return ret;
}

static int sun4i_codec_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct sun4i_codec *scodec = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	snd_soc_unregister_codec(&pdev->dev);
	clk_disable_unprepare(scodec->clk_apb);

	return 0;
}

static struct platform_driver sun4i_codec_driver = {
	.driver = {
		.name = "sun4i-codec",
		.of_match_table = sun4i_codec_of_match,
	},
	.probe = sun4i_codec_probe,
	.remove = sun4i_codec_remove,
};
module_platform_driver(sun4i_codec_driver);

MODULE_DESCRIPTION("Allwinner A10 codec driver");
MODULE_AUTHOR("Emilio López <emilio@elopez.com.ar>");
MODULE_AUTHOR("Jon Smirl <jonsmirl@gmail.com>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_LICENSE("GPL");
