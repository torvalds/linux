// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments PCM186x Universal Audio ADC
 *
 * Copyright (C) 2015-2017 Texas Instruments Incorporated - http://www.ti.com
 *	Andreas Dannenberg <dannenberg@ti.com>
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "pcm186x.h"

static const char * const pcm186x_supply_names[] = {
	"avdd",		/* Analog power supply. Connect to 3.3-V supply. */
	"dvdd",		/* Digital power supply. Connect to 3.3-V supply. */
	"iovdd",	/* I/O power supply. Connect to 3.3-V or 1.8-V. */
};
#define PCM186x_NUM_SUPPLIES ARRAY_SIZE(pcm186x_supply_names)

struct pcm186x_priv {
	struct regmap *regmap;
	struct regulator_bulk_data supplies[PCM186x_NUM_SUPPLIES];
	unsigned int sysclk;
	unsigned int tdm_offset;
	bool is_tdm_mode;
	bool is_master_mode;
};

static const DECLARE_TLV_DB_SCALE(pcm186x_pga_tlv, -1200, 50, 0);

static const struct snd_kcontrol_new pcm1863_snd_controls[] = {
	SOC_DOUBLE_R_S_TLV("ADC Capture Volume", PCM186X_PGA_VAL_CH1_L,
			   PCM186X_PGA_VAL_CH1_R, 0, -24, 80, 7, 0,
			   pcm186x_pga_tlv),
};

static const struct snd_kcontrol_new pcm1865_snd_controls[] = {
	SOC_DOUBLE_R_S_TLV("ADC1 Capture Volume", PCM186X_PGA_VAL_CH1_L,
			   PCM186X_PGA_VAL_CH1_R, 0, -24, 80, 7, 0,
			   pcm186x_pga_tlv),
	SOC_DOUBLE_R_S_TLV("ADC2 Capture Volume", PCM186X_PGA_VAL_CH2_L,
			   PCM186X_PGA_VAL_CH2_R, 0, -24, 80, 7, 0,
			   pcm186x_pga_tlv),
};

static const unsigned int pcm186x_adc_input_channel_sel_value[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x20, 0x30
};

static const char * const pcm186x_adcl_input_channel_sel_text[] = {
	"No Select",
	"VINL1[SE]",					/* Default for ADC1L */
	"VINL2[SE]",					/* Default for ADC2L */
	"VINL2[SE] + VINL1[SE]",
	"VINL3[SE]",
	"VINL3[SE] + VINL1[SE]",
	"VINL3[SE] + VINL2[SE]",
	"VINL3[SE] + VINL2[SE] + VINL1[SE]",
	"VINL4[SE]",
	"VINL4[SE] + VINL1[SE]",
	"VINL4[SE] + VINL2[SE]",
	"VINL4[SE] + VINL2[SE] + VINL1[SE]",
	"VINL4[SE] + VINL3[SE]",
	"VINL4[SE] + VINL3[SE] + VINL1[SE]",
	"VINL4[SE] + VINL3[SE] + VINL2[SE]",
	"VINL4[SE] + VINL3[SE] + VINL2[SE] + VINL1[SE]",
	"{VIN1P, VIN1M}[DIFF]",
	"{VIN4P, VIN4M}[DIFF]",
	"{VIN1P, VIN1M}[DIFF] + {VIN4P, VIN4M}[DIFF]"
};

static const char * const pcm186x_adcr_input_channel_sel_text[] = {
	"No Select",
	"VINR1[SE]",					/* Default for ADC1R */
	"VINR2[SE]",					/* Default for ADC2R */
	"VINR2[SE] + VINR1[SE]",
	"VINR3[SE]",
	"VINR3[SE] + VINR1[SE]",
	"VINR3[SE] + VINR2[SE]",
	"VINR3[SE] + VINR2[SE] + VINR1[SE]",
	"VINR4[SE]",
	"VINR4[SE] + VINR1[SE]",
	"VINR4[SE] + VINR2[SE]",
	"VINR4[SE] + VINR2[SE] + VINR1[SE]",
	"VINR4[SE] + VINR3[SE]",
	"VINR4[SE] + VINR3[SE] + VINR1[SE]",
	"VINR4[SE] + VINR3[SE] + VINR2[SE]",
	"VINR4[SE] + VINR3[SE] + VINR2[SE] + VINR1[SE]",
	"{VIN2P, VIN2M}[DIFF]",
	"{VIN3P, VIN3M}[DIFF]",
	"{VIN2P, VIN2M}[DIFF] + {VIN3P, VIN3M}[DIFF]"
};

static const struct soc_enum pcm186x_adc_input_channel_sel[] = {
	SOC_VALUE_ENUM_SINGLE(PCM186X_ADC1_INPUT_SEL_L, 0,
			      PCM186X_ADC_INPUT_SEL_MASK,
			      ARRAY_SIZE(pcm186x_adcl_input_channel_sel_text),
			      pcm186x_adcl_input_channel_sel_text,
			      pcm186x_adc_input_channel_sel_value),
	SOC_VALUE_ENUM_SINGLE(PCM186X_ADC1_INPUT_SEL_R, 0,
			      PCM186X_ADC_INPUT_SEL_MASK,
			      ARRAY_SIZE(pcm186x_adcr_input_channel_sel_text),
			      pcm186x_adcr_input_channel_sel_text,
			      pcm186x_adc_input_channel_sel_value),
	SOC_VALUE_ENUM_SINGLE(PCM186X_ADC2_INPUT_SEL_L, 0,
			      PCM186X_ADC_INPUT_SEL_MASK,
			      ARRAY_SIZE(pcm186x_adcl_input_channel_sel_text),
			      pcm186x_adcl_input_channel_sel_text,
			      pcm186x_adc_input_channel_sel_value),
	SOC_VALUE_ENUM_SINGLE(PCM186X_ADC2_INPUT_SEL_R, 0,
			      PCM186X_ADC_INPUT_SEL_MASK,
			      ARRAY_SIZE(pcm186x_adcr_input_channel_sel_text),
			      pcm186x_adcr_input_channel_sel_text,
			      pcm186x_adc_input_channel_sel_value),
};

static const struct snd_kcontrol_new pcm186x_adc_mux_controls[] = {
	SOC_DAPM_ENUM("ADC1 Left Input", pcm186x_adc_input_channel_sel[0]),
	SOC_DAPM_ENUM("ADC1 Right Input", pcm186x_adc_input_channel_sel[1]),
	SOC_DAPM_ENUM("ADC2 Left Input", pcm186x_adc_input_channel_sel[2]),
	SOC_DAPM_ENUM("ADC2 Right Input", pcm186x_adc_input_channel_sel[3]),
};

static const struct snd_soc_dapm_widget pcm1863_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("VINL1"),
	SND_SOC_DAPM_INPUT("VINR1"),
	SND_SOC_DAPM_INPUT("VINL2"),
	SND_SOC_DAPM_INPUT("VINR2"),
	SND_SOC_DAPM_INPUT("VINL3"),
	SND_SOC_DAPM_INPUT("VINR3"),
	SND_SOC_DAPM_INPUT("VINL4"),
	SND_SOC_DAPM_INPUT("VINR4"),

	SND_SOC_DAPM_MUX("ADC Left Capture Source", SND_SOC_NOPM, 0, 0,
			 &pcm186x_adc_mux_controls[0]),
	SND_SOC_DAPM_MUX("ADC Right Capture Source", SND_SOC_NOPM, 0, 0,
			 &pcm186x_adc_mux_controls[1]),

	/*
	 * Put the codec into SLEEP mode when not in use, allowing the
	 * Energysense mechanism to operate.
	 */
	SND_SOC_DAPM_ADC("ADC", "HiFi Capture", PCM186X_POWER_CTRL, 1,  1),
};

static const struct snd_soc_dapm_widget pcm1865_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("VINL1"),
	SND_SOC_DAPM_INPUT("VINR1"),
	SND_SOC_DAPM_INPUT("VINL2"),
	SND_SOC_DAPM_INPUT("VINR2"),
	SND_SOC_DAPM_INPUT("VINL3"),
	SND_SOC_DAPM_INPUT("VINR3"),
	SND_SOC_DAPM_INPUT("VINL4"),
	SND_SOC_DAPM_INPUT("VINR4"),

	SND_SOC_DAPM_MUX("ADC1 Left Capture Source", SND_SOC_NOPM, 0, 0,
			 &pcm186x_adc_mux_controls[0]),
	SND_SOC_DAPM_MUX("ADC1 Right Capture Source", SND_SOC_NOPM, 0, 0,
			 &pcm186x_adc_mux_controls[1]),
	SND_SOC_DAPM_MUX("ADC2 Left Capture Source", SND_SOC_NOPM, 0, 0,
			 &pcm186x_adc_mux_controls[2]),
	SND_SOC_DAPM_MUX("ADC2 Right Capture Source", SND_SOC_NOPM, 0, 0,
			 &pcm186x_adc_mux_controls[3]),

	/*
	 * Put the codec into SLEEP mode when not in use, allowing the
	 * Energysense mechanism to operate.
	 */
	SND_SOC_DAPM_ADC("ADC1", "HiFi Capture 1", PCM186X_POWER_CTRL, 1,  1),
	SND_SOC_DAPM_ADC("ADC2", "HiFi Capture 2", PCM186X_POWER_CTRL, 1,  1),
};

static const struct snd_soc_dapm_route pcm1863_dapm_routes[] = {
	{ "ADC Left Capture Source", NULL, "VINL1" },
	{ "ADC Left Capture Source", NULL, "VINR1" },
	{ "ADC Left Capture Source", NULL, "VINL2" },
	{ "ADC Left Capture Source", NULL, "VINR2" },
	{ "ADC Left Capture Source", NULL, "VINL3" },
	{ "ADC Left Capture Source", NULL, "VINR3" },
	{ "ADC Left Capture Source", NULL, "VINL4" },
	{ "ADC Left Capture Source", NULL, "VINR4" },

	{ "ADC", NULL, "ADC Left Capture Source" },

	{ "ADC Right Capture Source", NULL, "VINL1" },
	{ "ADC Right Capture Source", NULL, "VINR1" },
	{ "ADC Right Capture Source", NULL, "VINL2" },
	{ "ADC Right Capture Source", NULL, "VINR2" },
	{ "ADC Right Capture Source", NULL, "VINL3" },
	{ "ADC Right Capture Source", NULL, "VINR3" },
	{ "ADC Right Capture Source", NULL, "VINL4" },
	{ "ADC Right Capture Source", NULL, "VINR4" },

	{ "ADC", NULL, "ADC Right Capture Source" },
};

static const struct snd_soc_dapm_route pcm1865_dapm_routes[] = {
	{ "ADC1 Left Capture Source", NULL, "VINL1" },
	{ "ADC1 Left Capture Source", NULL, "VINR1" },
	{ "ADC1 Left Capture Source", NULL, "VINL2" },
	{ "ADC1 Left Capture Source", NULL, "VINR2" },
	{ "ADC1 Left Capture Source", NULL, "VINL3" },
	{ "ADC1 Left Capture Source", NULL, "VINR3" },
	{ "ADC1 Left Capture Source", NULL, "VINL4" },
	{ "ADC1 Left Capture Source", NULL, "VINR4" },

	{ "ADC1", NULL, "ADC1 Left Capture Source" },

	{ "ADC1 Right Capture Source", NULL, "VINL1" },
	{ "ADC1 Right Capture Source", NULL, "VINR1" },
	{ "ADC1 Right Capture Source", NULL, "VINL2" },
	{ "ADC1 Right Capture Source", NULL, "VINR2" },
	{ "ADC1 Right Capture Source", NULL, "VINL3" },
	{ "ADC1 Right Capture Source", NULL, "VINR3" },
	{ "ADC1 Right Capture Source", NULL, "VINL4" },
	{ "ADC1 Right Capture Source", NULL, "VINR4" },

	{ "ADC1", NULL, "ADC1 Right Capture Source" },

	{ "ADC2 Left Capture Source", NULL, "VINL1" },
	{ "ADC2 Left Capture Source", NULL, "VINR1" },
	{ "ADC2 Left Capture Source", NULL, "VINL2" },
	{ "ADC2 Left Capture Source", NULL, "VINR2" },
	{ "ADC2 Left Capture Source", NULL, "VINL3" },
	{ "ADC2 Left Capture Source", NULL, "VINR3" },
	{ "ADC2 Left Capture Source", NULL, "VINL4" },
	{ "ADC2 Left Capture Source", NULL, "VINR4" },

	{ "ADC2", NULL, "ADC2 Left Capture Source" },

	{ "ADC2 Right Capture Source", NULL, "VINL1" },
	{ "ADC2 Right Capture Source", NULL, "VINR1" },
	{ "ADC2 Right Capture Source", NULL, "VINL2" },
	{ "ADC2 Right Capture Source", NULL, "VINR2" },
	{ "ADC2 Right Capture Source", NULL, "VINL3" },
	{ "ADC2 Right Capture Source", NULL, "VINR3" },
	{ "ADC2 Right Capture Source", NULL, "VINL4" },
	{ "ADC2 Right Capture Source", NULL, "VINR4" },

	{ "ADC2", NULL, "ADC2 Right Capture Source" },
};

static int pcm186x_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm186x_priv *priv = snd_soc_component_get_drvdata(component);
	unsigned int rate = params_rate(params);
	snd_pcm_format_t format = params_format(params);
	unsigned int width = params_width(params);
	unsigned int channels = params_channels(params);
	unsigned int div_lrck;
	unsigned int div_bck;
	u8 tdm_tx_sel = 0;
	u8 pcm_cfg = 0;

	dev_dbg(component->dev, "%s() rate=%u format=0x%x width=%u channels=%u\n",
		__func__, rate, format, width, channels);

	switch (width) {
	case 16:
		pcm_cfg = PCM186X_PCM_CFG_RX_WLEN_16 <<
			  PCM186X_PCM_CFG_RX_WLEN_SHIFT |
			  PCM186X_PCM_CFG_TX_WLEN_16 <<
			  PCM186X_PCM_CFG_TX_WLEN_SHIFT;
		break;
	case 20:
		pcm_cfg = PCM186X_PCM_CFG_RX_WLEN_20 <<
			  PCM186X_PCM_CFG_RX_WLEN_SHIFT |
			  PCM186X_PCM_CFG_TX_WLEN_20 <<
			  PCM186X_PCM_CFG_TX_WLEN_SHIFT;
		break;
	case 24:
		pcm_cfg = PCM186X_PCM_CFG_RX_WLEN_24 <<
			  PCM186X_PCM_CFG_RX_WLEN_SHIFT |
			  PCM186X_PCM_CFG_TX_WLEN_24 <<
			  PCM186X_PCM_CFG_TX_WLEN_SHIFT;
		break;
	case 32:
		pcm_cfg = PCM186X_PCM_CFG_RX_WLEN_32 <<
			  PCM186X_PCM_CFG_RX_WLEN_SHIFT |
			  PCM186X_PCM_CFG_TX_WLEN_32 <<
			  PCM186X_PCM_CFG_TX_WLEN_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, PCM186X_PCM_CFG,
			    PCM186X_PCM_CFG_RX_WLEN_MASK |
			    PCM186X_PCM_CFG_TX_WLEN_MASK,
			    pcm_cfg);

	div_lrck = width * channels;

	if (priv->is_tdm_mode) {
		/* Select TDM transmission data */
		switch (channels) {
		case 2:
			tdm_tx_sel = PCM186X_TDM_TX_SEL_2CH;
			break;
		case 4:
			tdm_tx_sel = PCM186X_TDM_TX_SEL_4CH;
			break;
		case 6:
			tdm_tx_sel = PCM186X_TDM_TX_SEL_6CH;
			break;
		default:
			return -EINVAL;
		}

		snd_soc_component_update_bits(component, PCM186X_TDM_TX_SEL,
				    PCM186X_TDM_TX_SEL_MASK, tdm_tx_sel);

		/* In DSP/TDM mode, the LRCLK divider must be 256 */
		div_lrck = 256;

		/* Configure 1/256 duty cycle for LRCK */
		snd_soc_component_update_bits(component, PCM186X_PCM_CFG,
				    PCM186X_PCM_CFG_TDM_LRCK_MODE,
				    PCM186X_PCM_CFG_TDM_LRCK_MODE);
	}

	/* Only configure clock dividers in master mode. */
	if (priv->is_master_mode) {
		div_bck = priv->sysclk / (div_lrck * rate);

		dev_dbg(component->dev,
			"%s() master_clk=%u div_bck=%u div_lrck=%u\n",
			__func__, priv->sysclk, div_bck, div_lrck);

		snd_soc_component_write(component, PCM186X_BCK_DIV, div_bck - 1);
		snd_soc_component_write(component, PCM186X_LRK_DIV, div_lrck - 1);
	}

	return 0;
}

static int pcm186x_set_fmt(struct snd_soc_dai *dai, unsigned int format)
{
	struct snd_soc_component *component = dai->component;
	struct pcm186x_priv *priv = snd_soc_component_get_drvdata(component);
	u8 clk_ctrl = 0;
	u8 pcm_cfg = 0;

	dev_dbg(component->dev, "%s() format=0x%x\n", __func__, format);

	/* set master/slave audio interface */
	switch (format & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		if (!priv->sysclk) {
			dev_err(component->dev, "operating in master mode requires sysclock to be configured\n");
			return -EINVAL;
		}
		clk_ctrl |= PCM186X_CLK_CTRL_MST_MODE;
		priv->is_master_mode = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		priv->is_master_mode = false;
		break;
	default:
		dev_err(component->dev, "Invalid DAI master/slave interface\n");
		return -EINVAL;
	}

	/* set interface polarity */
	switch (format & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		dev_err(component->dev, "Inverted DAI clocks not supported\n");
		return -EINVAL;
	}

	/* set interface format */
	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		pcm_cfg = PCM186X_PCM_CFG_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		pcm_cfg = PCM186X_PCM_CFG_FMT_LEFTJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		priv->tdm_offset += 1;
		/* fall through */
		/* DSP_A uses the same basic config as DSP_B
		 * except we need to shift the TDM output by one BCK cycle
		 */
	case SND_SOC_DAIFMT_DSP_B:
		priv->is_tdm_mode = true;
		pcm_cfg = PCM186X_PCM_CFG_FMT_TDM;
		break;
	default:
		dev_err(component->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, PCM186X_CLK_CTRL,
			    PCM186X_CLK_CTRL_MST_MODE, clk_ctrl);

	snd_soc_component_write(component, PCM186X_TDM_TX_OFFSET, priv->tdm_offset);

	snd_soc_component_update_bits(component, PCM186X_PCM_CFG,
			    PCM186X_PCM_CFG_FMT_MASK, pcm_cfg);

	return 0;
}

static int pcm186x_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct pcm186x_priv *priv = snd_soc_component_get_drvdata(component);
	unsigned int first_slot, last_slot, tdm_offset;

	dev_dbg(component->dev,
		"%s() tx_mask=0x%x rx_mask=0x%x slots=%d slot_width=%d\n",
		__func__, tx_mask, rx_mask, slots, slot_width);

	if (!tx_mask) {
		dev_err(component->dev, "tdm tx mask must not be 0\n");
		return -EINVAL;
	}

	first_slot = __ffs(tx_mask);
	last_slot = __fls(tx_mask);

	if (last_slot - first_slot != hweight32(tx_mask) - 1) {
		dev_err(component->dev, "tdm tx mask must be contiguous\n");
		return -EINVAL;
	}

	tdm_offset = first_slot * slot_width;

	if (tdm_offset > 255) {
		dev_err(component->dev, "tdm tx slot selection out of bounds\n");
		return -EINVAL;
	}

	priv->tdm_offset = tdm_offset;

	return 0;
}

static int pcm186x_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				  unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct pcm186x_priv *priv = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s() clk_id=%d freq=%u dir=%d\n",
		__func__, clk_id, freq, dir);

	priv->sysclk = freq;

	return 0;
}

static const struct snd_soc_dai_ops pcm186x_dai_ops = {
	.set_sysclk = pcm186x_set_dai_sysclk,
	.set_tdm_slot = pcm186x_set_tdm_slot,
	.set_fmt = pcm186x_set_fmt,
	.hw_params = pcm186x_hw_params,
};

static struct snd_soc_dai_driver pcm1863_dai = {
	.name = "pcm1863-aif",
	.capture = {
		 .stream_name = "Capture",
		 .channels_min = 1,
		 .channels_max = 2,
		 .rates = PCM186X_RATES,
		 .formats = PCM186X_FORMATS,
	 },
	.ops = &pcm186x_dai_ops,
};

static struct snd_soc_dai_driver pcm1865_dai = {
	.name = "pcm1865-aif",
	.capture = {
		 .stream_name = "Capture",
		 .channels_min = 1,
		 .channels_max = 4,
		 .rates = PCM186X_RATES,
		 .formats = PCM186X_FORMATS,
	 },
	.ops = &pcm186x_dai_ops,
};

static int pcm186x_power_on(struct snd_soc_component *component)
{
	struct pcm186x_priv *priv = snd_soc_component_get_drvdata(component);
	int ret = 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(priv->supplies),
				    priv->supplies);
	if (ret)
		return ret;

	regcache_cache_only(priv->regmap, false);
	ret = regcache_sync(priv->regmap);
	if (ret) {
		dev_err(component->dev, "Failed to restore cache\n");
		regcache_cache_only(priv->regmap, true);
		regulator_bulk_disable(ARRAY_SIZE(priv->supplies),
				       priv->supplies);
		return ret;
	}

	snd_soc_component_update_bits(component, PCM186X_POWER_CTRL,
			    PCM186X_PWR_CTRL_PWRDN, 0);

	return 0;
}

static int pcm186x_power_off(struct snd_soc_component *component)
{
	struct pcm186x_priv *priv = snd_soc_component_get_drvdata(component);
	int ret;

	snd_soc_component_update_bits(component, PCM186X_POWER_CTRL,
			    PCM186X_PWR_CTRL_PWRDN, PCM186X_PWR_CTRL_PWRDN);

	regcache_cache_only(priv->regmap, true);

	ret = regulator_bulk_disable(ARRAY_SIZE(priv->supplies),
				     priv->supplies);
	if (ret)
		return ret;

	return 0;
}

static int pcm186x_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	dev_dbg(component->dev, "## %s: %d -> %d\n", __func__,
		snd_soc_component_get_bias_level(component), level);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			pcm186x_power_on(component);
		break;
	case SND_SOC_BIAS_OFF:
		pcm186x_power_off(component);
		break;
	}

	return 0;
}

static struct snd_soc_component_driver soc_codec_dev_pcm1863 = {
	.set_bias_level		= pcm186x_set_bias_level,
	.controls		= pcm1863_snd_controls,
	.num_controls		= ARRAY_SIZE(pcm1863_snd_controls),
	.dapm_widgets		= pcm1863_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm1863_dapm_widgets),
	.dapm_routes		= pcm1863_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm1863_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct snd_soc_component_driver soc_codec_dev_pcm1865 = {
	.set_bias_level		= pcm186x_set_bias_level,
	.controls		= pcm1865_snd_controls,
	.num_controls		= ARRAY_SIZE(pcm1865_snd_controls),
	.dapm_widgets		= pcm1865_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm1865_dapm_widgets),
	.dapm_routes		= pcm1865_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm1865_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static bool pcm186x_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PCM186X_PAGE:
	case PCM186X_DEVICE_STATUS:
	case PCM186X_FSAMPLE_STATUS:
	case PCM186X_DIV_STATUS:
	case PCM186X_CLK_STATUS:
	case PCM186X_SUPPLY_STATUS:
	case PCM186X_MMAP_STAT_CTRL:
	case PCM186X_MMAP_ADDRESS:
		return true;
	}

	return false;
}

static const struct regmap_range_cfg pcm186x_range = {
	.name = "Pages",
	.range_max = PCM186X_MAX_REGISTER,
	.selector_reg = PCM186X_PAGE,
	.selector_mask = 0xff,
	.window_len = PCM186X_PAGE_LEN,
};

const struct regmap_config pcm186x_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.volatile_reg = pcm186x_volatile,

	.ranges = &pcm186x_range,
	.num_ranges = 1,

	.max_register = PCM186X_MAX_REGISTER,

	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(pcm186x_regmap);

int pcm186x_probe(struct device *dev, enum pcm186x_type type, int irq,
		  struct regmap *regmap)
{
	struct pcm186x_priv *priv;
	int i, ret;

	priv = devm_kzalloc(dev, sizeof(struct pcm186x_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->regmap = regmap;

	for (i = 0; i < ARRAY_SIZE(priv->supplies); i++)
		priv->supplies[i].supply = pcm186x_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(priv->supplies),
				      priv->supplies);
	if (ret) {
		dev_err(dev, "failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(priv->supplies),
				    priv->supplies);
	if (ret) {
		dev_err(dev, "failed enable supplies: %d\n", ret);
		return ret;
	}

	/* Reset device registers for a consistent power-on like state */
	ret = regmap_write(regmap, PCM186X_PAGE, PCM186X_RESET);
	if (ret) {
		dev_err(dev, "failed to write device: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(priv->supplies),
				     priv->supplies);
	if (ret) {
		dev_err(dev, "failed disable supplies: %d\n", ret);
		return ret;
	}

	switch (type) {
	case PCM1865:
	case PCM1864:
		ret = devm_snd_soc_register_component(dev, &soc_codec_dev_pcm1865,
					     &pcm1865_dai, 1);
		break;
	case PCM1863:
	case PCM1862:
	default:
		ret = devm_snd_soc_register_component(dev, &soc_codec_dev_pcm1863,
					     &pcm1863_dai, 1);
	}
	if (ret) {
		dev_err(dev, "failed to register CODEC: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pcm186x_probe);

MODULE_AUTHOR("Andreas Dannenberg <dannenberg@ti.com>");
MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("PCM186x Universal Audio ADC driver");
MODULE_LICENSE("GPL v2");
