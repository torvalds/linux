// SPDX-License-Identifier: GPL-2.0
//
// TI SRC4xxx Audio Codec driver
//
// Copyright 2021-2022 Deqx Pty Ltd
// Author: Matt Flax <flatmax@flatmax.com>

#include <linux/module.h>

#include <sound/soc.h>
#include <sound/tlv.h>

#include "src4xxx.h"

struct src4xxx {
	struct regmap *regmap;
	bool master[2];
	int mclk_hz;
	struct device *dev;
};

enum {SRC4XXX_PORTA, SRC4XXX_PORTB};

/* SRC attenuation */
static const DECLARE_TLV_DB_SCALE(src_tlv, -12750, 50, 0);

static const struct snd_kcontrol_new src4xxx_controls[] = {
	SOC_DOUBLE_R_TLV("SRC Volume",
		SRC4XXX_SCR_CTL_30, SRC4XXX_SCR_CTL_31, 0, 255, 1, src_tlv),
};

/* I2S port control */
static const char * const port_out_src_text[] = {
	"loopback", "other_port", "DIR", "SRC"
};
static SOC_ENUM_SINGLE_DECL(porta_out_src_enum, SRC4XXX_PORTA_CTL_03, 4,
	port_out_src_text);
static SOC_ENUM_SINGLE_DECL(portb_out_src_enum, SRC4XXX_PORTB_CTL_05, 4,
	port_out_src_text);
static const struct snd_kcontrol_new porta_out_control =
	SOC_DAPM_ENUM("Port A source select", porta_out_src_enum);
static const struct snd_kcontrol_new portb_out_control =
	SOC_DAPM_ENUM("Port B source select", portb_out_src_enum);

/* Digital audio transmitter control */
static const char * const dit_mux_text[] = {"Port A", "Port B", "DIR", "SRC"};
static SOC_ENUM_SINGLE_DECL(dit_mux_enum, SRC4XXX_TX_CTL_07, 3, dit_mux_text);
static const struct snd_kcontrol_new dit_mux_control =
	SOC_DAPM_ENUM("DIT source", dit_mux_enum);

/* SRC control */
static const char * const src_in_text[] = {"Port A", "Port B", "DIR"};
static SOC_ENUM_SINGLE_DECL(src_in_enum, SRC4XXX_SCR_CTL_2D, 0, src_in_text);
static const struct snd_kcontrol_new src_in_control =
	SOC_DAPM_ENUM("SRC source select", src_in_enum);

/* DIR control */
static const char * const dir_in_text[] = {"Ch 1", "Ch 2", "Ch 3", "Ch 4"};
static SOC_ENUM_SINGLE_DECL(dir_in_enum, SRC4XXX_RCV_CTL_0D, 0, dir_in_text);
static const struct snd_kcontrol_new dir_in_control =
	SOC_DAPM_ENUM("Digital Input", dir_in_enum);

static const struct snd_soc_dapm_widget src4xxx_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("loopback_A"),
	SND_SOC_DAPM_INPUT("other_port_A"),
	SND_SOC_DAPM_INPUT("DIR_A"),
	SND_SOC_DAPM_INPUT("SRC_A"),
	SND_SOC_DAPM_MUX("Port A source",
		SND_SOC_NOPM, 0, 0, &porta_out_control),

	SND_SOC_DAPM_INPUT("loopback_B"),
	SND_SOC_DAPM_INPUT("other_port_B"),
	SND_SOC_DAPM_INPUT("DIR_B"),
	SND_SOC_DAPM_INPUT("SRC_B"),
	SND_SOC_DAPM_MUX("Port B source",
		SND_SOC_NOPM, 0, 0, &portb_out_control),

	SND_SOC_DAPM_INPUT("Port_A"),
	SND_SOC_DAPM_INPUT("Port_B"),
	SND_SOC_DAPM_INPUT("DIR_"),

	/* Digital audio receivers and transmitters */
	SND_SOC_DAPM_OUTPUT("DIR_OUT"),
	SND_SOC_DAPM_OUTPUT("SRC_OUT"),
	SND_SOC_DAPM_MUX("DIT Out Src", SRC4XXX_PWR_RST_01,
		SRC4XXX_ENABLE_DIT_SHIFT, 1, &dit_mux_control),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF_A_RX", "Playback A", 0,
		SRC4XXX_PWR_RST_01, SRC4XXX_ENABLE_PORT_A_SHIFT, 1),
	SND_SOC_DAPM_AIF_OUT("AIF_A_TX", "Capture A", 0,
		SRC4XXX_PWR_RST_01, SRC4XXX_ENABLE_PORT_A_SHIFT, 1),
	SND_SOC_DAPM_AIF_IN("AIF_B_RX", "Playback B", 0,
		SRC4XXX_PWR_RST_01, SRC4XXX_ENABLE_PORT_B_SHIFT, 1),
	SND_SOC_DAPM_AIF_OUT("AIF_B_TX", "Capture B", 0,
		SRC4XXX_PWR_RST_01, SRC4XXX_ENABLE_PORT_B_SHIFT, 1),

	SND_SOC_DAPM_MUX("SRC source", SND_SOC_NOPM, 0, 0, &src_in_control),

	SND_SOC_DAPM_INPUT("MCLK"),
	SND_SOC_DAPM_INPUT("RXMCLKI"),
	SND_SOC_DAPM_INPUT("RXMCLKO"),

	SND_SOC_DAPM_INPUT("RX1"),
	SND_SOC_DAPM_INPUT("RX2"),
	SND_SOC_DAPM_INPUT("RX3"),
	SND_SOC_DAPM_INPUT("RX4"),
	SND_SOC_DAPM_MUX("Digital Input", SRC4XXX_PWR_RST_01,
		SRC4XXX_ENABLE_DIR_SHIFT, 1, &dir_in_control),
};

static const struct snd_soc_dapm_route src4xxx_audio_routes[] = {
	/* I2S Input to Output Routing */
	{"Port A source", "loopback", "loopback_A"},
	{"Port A source", "other_port", "other_port_A"},
	{"Port A source", "DIR", "DIR_A"},
	{"Port A source", "SRC", "SRC_A"},
	{"Port B source", "loopback", "loopback_B"},
	{"Port B source", "other_port", "other_port_B"},
	{"Port B source", "DIR", "DIR_B"},
	{"Port B source", "SRC", "SRC_B"},
	/* DIT muxing */
	{"DIT Out Src", "Port A", "Capture A"},
	{"DIT Out Src", "Port B", "Capture B"},
	{"DIT Out Src", "DIR", "DIR_OUT"},
	{"DIT Out Src", "SRC", "SRC_OUT"},

	/* SRC input selection */
	{"SRC source", "Port A", "Port_A"},
	{"SRC source", "Port B", "Port_B"},
	{"SRC source", "DIR", "DIR_"},
	/* SRC mclk selection */
	{"SRC mclk source", "Master (MCLK)", "MCLK"},
	{"SRC mclk source", "Master (RXCLKI)", "RXMCLKI"},
	{"SRC mclk source", "Recovered receiver clk", "RXMCLKO"},
	/* DIR input selection */
	{"Digital Input", "Ch 1", "RX1"},
	{"Digital Input", "Ch 2", "RX2"},
	{"Digital Input", "Ch 3", "RX3"},
	{"Digital Input", "Ch 4", "RX4"},
};


static const struct snd_soc_component_driver src4xxx_driver = {
	.controls = src4xxx_controls,
	.num_controls = ARRAY_SIZE(src4xxx_controls),

	.dapm_widgets = src4xxx_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(src4xxx_dapm_widgets),
	.dapm_routes = src4xxx_audio_routes,
	.num_dapm_routes = ARRAY_SIZE(src4xxx_audio_routes),
};

static int src4xxx_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct src4xxx *src4xxx = snd_soc_component_get_drvdata(component);
	unsigned int ctrl;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		ctrl = SRC4XXX_BUS_MASTER;
		src4xxx->master[dai->id] = true;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		ctrl = 0;
		src4xxx->master[dai->id] = false;
		break;
	default:
		return -EINVAL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ctrl |= SRC4XXX_BUS_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ctrl |= SRC4XXX_BUS_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ctrl |= SRC4XXX_BUS_RIGHT_J_24;
		break;
	default:
		return -EINVAL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
		break;
	}

	regmap_update_bits(src4xxx->regmap, SRC4XXX_BUS_FMT(dai->id),
		SRC4XXX_BUS_FMT_MS_MASK, ctrl);

	return 0;
}

static int src4xxx_set_mclk_hz(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct src4xxx *src4xxx = snd_soc_component_get_drvdata(component);

	dev_info(component->dev, "changing mclk rate from %d to %d Hz\n",
		src4xxx->mclk_hz, freq);
	src4xxx->mclk_hz = freq;

	return 0;
}

static int src4xxx_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct src4xxx *src4xxx = snd_soc_component_get_drvdata(component);
	unsigned int mclk_div;
	int val, pj, jd, d;
	int reg;
	int ret;

	switch (dai->id) {
	case SRC4XXX_PORTB:
		reg = SRC4XXX_PORTB_CTL_06;
		break;
	default:
		reg = SRC4XXX_PORTA_CTL_04;
		break;
	}

	if (src4xxx->master[dai->id]) {
		mclk_div = src4xxx->mclk_hz/params_rate(params);
		if (src4xxx->mclk_hz != mclk_div*params_rate(params)) {
			dev_err(component->dev,
				"mclk %d / rate %d has a remainder.\n",
				src4xxx->mclk_hz, params_rate(params));
			return -EINVAL;
		}

		val = ((int)mclk_div - 128) / 128;
		if ((val < 0) | (val > 3)) {
			dev_err(component->dev,
				"div register setting %d is out of range\n",
				val);
			dev_err(component->dev,
				"unsupported sample rate %d Hz for the master clock of %d Hz\n",
				params_rate(params), src4xxx->mclk_hz);
			return -EINVAL;
		}

		/* set the TX DIV */
		ret = regmap_update_bits(src4xxx->regmap,
			SRC4XXX_TX_CTL_07, SRC4XXX_TX_MCLK_DIV_MASK,
			val<<SRC4XXX_TX_MCLK_DIV_SHIFT);
		if (ret) {
			dev_err(component->dev,
				"Couldn't set the TX's div register to %d << %d = 0x%x\n",
				val, SRC4XXX_TX_MCLK_DIV_SHIFT,
				val<<SRC4XXX_TX_MCLK_DIV_SHIFT);
			return ret;
		}

		/* set the PLL for the digital receiver */
		switch (src4xxx->mclk_hz) {
		case 24576000:
			pj = 0x22;
			jd = 0x00;
			d = 0x00;
			break;
		case 22579200:
			pj = 0x22;
			jd = 0x1b;
			d = 0xa3;
			break;
		default:
			/* don't error out here,
			 * other parts of the chip are still functional
			 * Dummy initialize variables to avoid
			 * -Wsometimes-uninitialized from clang.
			 */
			dev_info(component->dev,
				"Couldn't set the RCV PLL as this master clock rate is unknown. Chosen regmap values may not match real world values.\n");
			pj = 0x0;
			jd = 0xff;
			d = 0xff;
			break;
		}
		ret = regmap_write(src4xxx->regmap, SRC4XXX_RCV_PLL_0F, pj);
		if (ret < 0)
			dev_err(component->dev,
				"Failed to update PLL register 0x%x\n",
				SRC4XXX_RCV_PLL_0F);
		ret = regmap_write(src4xxx->regmap, SRC4XXX_RCV_PLL_10, jd);
		if (ret < 0)
			dev_err(component->dev,
				"Failed to update PLL register 0x%x\n",
				SRC4XXX_RCV_PLL_10);
		ret = regmap_write(src4xxx->regmap, SRC4XXX_RCV_PLL_11, d);
		if (ret < 0)
			dev_err(component->dev,
				"Failed to update PLL register 0x%x\n",
				SRC4XXX_RCV_PLL_11);

		ret = regmap_update_bits(src4xxx->regmap,
			SRC4XXX_TX_CTL_07, SRC4XXX_TX_MCLK_DIV_MASK,
			val<<SRC4XXX_TX_MCLK_DIV_SHIFT);
		if (ret < 0) {
			dev_err(component->dev,
				"Couldn't set the TX's div register to %d << %d = 0x%x\n",
				val, SRC4XXX_TX_MCLK_DIV_SHIFT,
				val<<SRC4XXX_TX_MCLK_DIV_SHIFT);
			return ret;
		}

		return regmap_update_bits(src4xxx->regmap, reg,
					SRC4XXX_MCLK_DIV_MASK, val);
	} else {
		dev_info(dai->dev, "not setting up MCLK as not master\n");
	}

	return 0;
};

static const struct snd_soc_dai_ops src4xxx_dai_ops = {
	.hw_params	= src4xxx_hw_params,
	.set_sysclk	= src4xxx_set_mclk_hz,
	.set_fmt	= src4xxx_set_dai_fmt,
};

#define SRC4XXX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |	SNDRV_PCM_FMTBIT_S32_LE)
#define SRC4XXX_RATES (SNDRV_PCM_RATE_44100|SNDRV_PCM_RATE_48000|\
				SNDRV_PCM_RATE_88200|\
				SNDRV_PCM_RATE_96000|\
				SNDRV_PCM_RATE_176400|\
				SNDRV_PCM_RATE_192000)

static struct snd_soc_dai_driver src4xxx_dai_driver[] = {
	{
		.id = SRC4XXX_PORTA,
		.name = "src4xxx-portA",
		.playback = {
			.stream_name = "Playback A",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SRC4XXX_RATES,
			.formats = SRC4XXX_FORMATS,
		},
		.capture = {
			.stream_name = "Capture A",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SRC4XXX_RATES,
			.formats = SRC4XXX_FORMATS,
		},
		.ops = &src4xxx_dai_ops,
	},
	{
		.id = SRC4XXX_PORTB,
		.name = "src4xxx-portB",
		.playback = {
			.stream_name = "Playback B",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SRC4XXX_RATES,
			.formats = SRC4XXX_FORMATS,
		},
		.capture = {
			.stream_name = "Capture B",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SRC4XXX_RATES,
			.formats = SRC4XXX_FORMATS,
		},
		.ops = &src4xxx_dai_ops,
	},
};

static const struct reg_default src4xxx_reg_defaults[] = {
	{ SRC4XXX_PWR_RST_01,		0x00 }, /* all powered down intially */
	{ SRC4XXX_PORTA_CTL_03,		0x00 },
	{ SRC4XXX_PORTA_CTL_04,		0x00 },
	{ SRC4XXX_PORTB_CTL_05,		0x00 },
	{ SRC4XXX_PORTB_CTL_06,		0x00 },
	{ SRC4XXX_TX_CTL_07,		0x00 },
	{ SRC4XXX_TX_CTL_08,		0x00 },
	{ SRC4XXX_TX_CTL_09,		0x00 },
	{ SRC4XXX_SRC_DIT_IRQ_MSK_0B,	0x00 },
	{ SRC4XXX_SRC_DIT_IRQ_MODE_0C,	0x00 },
	{ SRC4XXX_RCV_CTL_0D,		0x00 },
	{ SRC4XXX_RCV_CTL_0E,		0x00 },
	{ SRC4XXX_RCV_PLL_0F,		0x00 }, /* not spec. in the datasheet */
	{ SRC4XXX_RCV_PLL_10,		0xff }, /* not spec. in the datasheet */
	{ SRC4XXX_RCV_PLL_11,		0xff }, /* not spec. in the datasheet */
	{ SRC4XXX_RVC_IRQ_MSK_16,	0x00 },
	{ SRC4XXX_RVC_IRQ_MSK_17,	0x00 },
	{ SRC4XXX_RVC_IRQ_MODE_18,	0x00 },
	{ SRC4XXX_RVC_IRQ_MODE_19,	0x00 },
	{ SRC4XXX_RVC_IRQ_MODE_1A,	0x00 },
	{ SRC4XXX_GPIO_1_1B,		0x00 },
	{ SRC4XXX_GPIO_2_1C,		0x00 },
	{ SRC4XXX_GPIO_3_1D,		0x00 },
	{ SRC4XXX_GPIO_4_1E,		0x00 },
	{ SRC4XXX_SCR_CTL_2D,		0x00 },
	{ SRC4XXX_SCR_CTL_2E,		0x00 },
	{ SRC4XXX_SCR_CTL_2F,		0x00 },
	{ SRC4XXX_SCR_CTL_30,		0x00 },
	{ SRC4XXX_SCR_CTL_31,		0x00 },
};

int src4xxx_probe(struct device *dev, struct regmap *regmap,
			void (*switch_mode)(struct device *dev))
{
	struct src4xxx *src4xxx;
	int ret;

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	src4xxx = devm_kzalloc(dev, sizeof(*src4xxx), GFP_KERNEL);
	if (!src4xxx)
		return -ENOMEM;

	src4xxx->regmap = regmap;
	src4xxx->dev = dev;
	src4xxx->mclk_hz = 0; /* mclk has not been configured yet */
	dev_set_drvdata(dev, src4xxx);

	ret = regmap_write(regmap, SRC4XXX_PWR_RST_01, SRC4XXX_RESET);
	if (ret < 0)
		dev_err(dev, "Failed to issue reset: %d\n", ret);
	usleep_range(1, 500); /* sleep for more then 500 ns */
	ret = regmap_write(regmap, SRC4XXX_PWR_RST_01, SRC4XXX_POWER_DOWN);
	if (ret < 0)
		dev_err(dev, "Failed to decommission reset: %d\n", ret);
	usleep_range(500, 1000); /* sleep for 500 us or more */

	ret = regmap_update_bits(src4xxx->regmap, SRC4XXX_PWR_RST_01,
		SRC4XXX_POWER_ENABLE, SRC4XXX_POWER_ENABLE);
	if (ret < 0)
		dev_err(dev, "Failed to port A and B : %d\n", ret);

	/* set receiver to use master clock (rcv mclk is most likely jittery) */
	ret = regmap_update_bits(src4xxx->regmap, SRC4XXX_RCV_CTL_0D,
		SRC4XXX_RXCLK_MCLK,	SRC4XXX_RXCLK_MCLK);
	if (ret < 0)
		dev_err(dev,
			"Failed to enable mclk as the PLL1 DIR reference : %d\n", ret);

	/* default to leaving the PLL2 running on loss of lock, divide by 8 */
	ret = regmap_update_bits(src4xxx->regmap, SRC4XXX_RCV_CTL_0E,
		SRC4XXX_PLL2_DIV_8 | SRC4XXX_REC_MCLK_EN | SRC4XXX_PLL2_LOL,
		SRC4XXX_PLL2_DIV_8 | SRC4XXX_REC_MCLK_EN | SRC4XXX_PLL2_LOL);
	if (ret < 0)
		dev_err(dev, "Failed to enable mclk rec and div : %d\n", ret);

	ret = devm_snd_soc_register_component(dev, &src4xxx_driver,
			src4xxx_dai_driver, ARRAY_SIZE(src4xxx_dai_driver));
	if (ret == 0)
		dev_info(dev, "src4392 probe ok %d\n", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(src4xxx_probe);

static bool src4xxx_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SRC4XXX_RES_00:
	case SRC4XXX_GLOBAL_ITR_STS_02:
	case SRC4XXX_SRC_DIT_STS_0A:
	case SRC4XXX_NON_AUDIO_D_12:
	case SRC4XXX_RVC_STS_13:
	case SRC4XXX_RVC_STS_14:
	case SRC4XXX_RVC_STS_15:
	case SRC4XXX_SUB_CODE_1F:
	case SRC4XXX_SUB_CODE_20:
	case SRC4XXX_SUB_CODE_21:
	case SRC4XXX_SUB_CODE_22:
	case SRC4XXX_SUB_CODE_23:
	case SRC4XXX_SUB_CODE_24:
	case SRC4XXX_SUB_CODE_25:
	case SRC4XXX_SUB_CODE_26:
	case SRC4XXX_SUB_CODE_27:
	case SRC4XXX_SUB_CODE_28:
	case SRC4XXX_PC_PREAMBLE_HI_29:
	case SRC4XXX_PC_PREAMBLE_LO_2A:
	case SRC4XXX_PD_PREAMBLE_HI_2B:
	case SRC4XXX_PC_PREAMBLE_LO_2C:
	case SRC4XXX_IO_RATIO_32:
	case SRC4XXX_IO_RATIO_33:
		return true;
	}

	if (reg > SRC4XXX_IO_RATIO_33 && reg < SRC4XXX_PAGE_SEL_7F)
		return true;

	return false;
}

const struct regmap_config src4xxx_regmap_config = {
	.val_bits = 8,
	.reg_bits = 8,
	.max_register = SRC4XXX_IO_RATIO_33,

	.reg_defaults = src4xxx_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(src4xxx_reg_defaults),
	.volatile_reg = src4xxx_volatile_register,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(src4xxx_regmap_config);

MODULE_DESCRIPTION("ASoC SRC4XXX CODEC driver");
MODULE_AUTHOR("Matt Flax <flatmax@flatmax.com>");
MODULE_LICENSE("GPL");
