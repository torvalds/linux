// SPDX-License-Identifier: GPL-2.0-or-later
/* Atmel ALSA SoC Audio Class D Amplifier (CLASSD) driver
 *
 * Copyright (C) 2015 Atmel
 *
 * Author: Songjun Wu <songjun.wu@atmel.com>
 */

#include <linux/of.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "atmel-classd.h"

struct atmel_classd_pdata {
	bool non_overlap_enable;
	int non_overlap_time;
	int pwm_type;
	const char *card_name;
};

struct atmel_classd {
	dma_addr_t phy_base;
	struct regmap *regmap;
	struct clk *pclk;
	struct clk *gclk;
	struct device *dev;
	int irq;
	const struct atmel_classd_pdata *pdata;
};

#ifdef CONFIG_OF
static const struct of_device_id atmel_classd_of_match[] = {
	{
		.compatible = "atmel,sama5d2-classd",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, atmel_classd_of_match);

static struct atmel_classd_pdata *atmel_classd_dt_init(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct atmel_classd_pdata *pdata;
	const char *pwm_type_s;
	int ret;

	if (!np) {
		dev_err(dev, "device node not found\n");
		return ERR_PTR(-EINVAL);
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	ret = of_property_read_string(np, "atmel,pwm-type", &pwm_type_s);
	if ((ret == 0) && (strcmp(pwm_type_s, "diff") == 0))
		pdata->pwm_type = CLASSD_MR_PWMTYP_DIFF;
	else
		pdata->pwm_type = CLASSD_MR_PWMTYP_SINGLE;

	ret = of_property_read_u32(np,
			"atmel,non-overlap-time", &pdata->non_overlap_time);
	if (ret)
		pdata->non_overlap_enable = false;
	else
		pdata->non_overlap_enable = true;

	ret = of_property_read_string(np, "atmel,model", &pdata->card_name);
	if (ret)
		pdata->card_name = "CLASSD";

	return pdata;
}
#else
static inline struct atmel_classd_pdata *
atmel_classd_dt_init(struct device *dev)
{
	return ERR_PTR(-EINVAL);
}
#endif

#define ATMEL_CLASSD_RATES (SNDRV_PCM_RATE_8000 \
			| SNDRV_PCM_RATE_16000	| SNDRV_PCM_RATE_22050 \
			| SNDRV_PCM_RATE_32000	| SNDRV_PCM_RATE_44100 \
			| SNDRV_PCM_RATE_48000	| SNDRV_PCM_RATE_88200 \
			| SNDRV_PCM_RATE_96000)

static const struct snd_pcm_hardware atmel_classd_hw = {
	.info			= SNDRV_PCM_INFO_MMAP
				| SNDRV_PCM_INFO_MMAP_VALID
				| SNDRV_PCM_INFO_INTERLEAVED
				| SNDRV_PCM_INFO_RESUME
				| SNDRV_PCM_INFO_PAUSE,
	.formats		= (SNDRV_PCM_FMTBIT_S16_LE),
	.rates			= ATMEL_CLASSD_RATES,
	.rate_min		= 8000,
	.rate_max		= 96000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 256,
	.period_bytes_max	= 32 * 1024,
	.periods_min		= 2,
	.periods_max		= 256,
};

#define ATMEL_CLASSD_PREALLOC_BUF_SIZE  (64 * 1024)

/* cpu dai component */
static int atmel_classd_cpu_dai_startup(struct snd_pcm_substream *substream,
					struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct atmel_classd *dd = snd_soc_card_get_drvdata(rtd->card);
	int err;

	regmap_write(dd->regmap, CLASSD_THR, 0x0);

	err = clk_prepare_enable(dd->pclk);
	if (err)
		return err;
	err = clk_prepare_enable(dd->gclk);
	if (err) {
		clk_disable_unprepare(dd->pclk);
		return err;
	}
	return 0;
}

/* platform */
static int
atmel_classd_platform_configure_dma(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct dma_slave_config *slave_config)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct atmel_classd *dd = snd_soc_card_get_drvdata(rtd->card);

	if (params_physical_width(params) != 16) {
		dev_err(dd->dev,
			"only supports 16-bit audio data\n");
		return -EINVAL;
	}

	if (params_channels(params) == 1)
		slave_config->dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	else
		slave_config->dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	slave_config->direction		= DMA_MEM_TO_DEV;
	slave_config->dst_addr		= dd->phy_base + CLASSD_THR;
	slave_config->dst_maxburst	= 1;
	slave_config->src_maxburst	= 1;
	slave_config->device_fc		= false;

	return 0;
}

static const struct snd_dmaengine_pcm_config
atmel_classd_dmaengine_pcm_config = {
	.prepare_slave_config	= atmel_classd_platform_configure_dma,
	.pcm_hardware		= &atmel_classd_hw,
	.prealloc_buffer_size	= ATMEL_CLASSD_PREALLOC_BUF_SIZE,
};

/* codec */
static const char * const mono_mode_text[] = {
	"mix", "sat", "left", "right"
};

static SOC_ENUM_SINGLE_DECL(classd_mono_mode_enum,
			CLASSD_INTPMR, CLASSD_INTPMR_MONO_MODE_SHIFT,
			mono_mode_text);

static const char * const eqcfg_text[] = {
	"Treble-12dB", "Treble-6dB",
	"Medium-8dB", "Medium-3dB",
	"Bass-12dB", "Bass-6dB",
	"0 dB",
	"Bass+6dB", "Bass+12dB",
	"Medium+3dB", "Medium+8dB",
	"Treble+6dB", "Treble+12dB",
};

static const unsigned int eqcfg_value[] = {
	CLASSD_INTPMR_EQCFG_T_CUT_12, CLASSD_INTPMR_EQCFG_T_CUT_6,
	CLASSD_INTPMR_EQCFG_M_CUT_8, CLASSD_INTPMR_EQCFG_M_CUT_3,
	CLASSD_INTPMR_EQCFG_B_CUT_12, CLASSD_INTPMR_EQCFG_B_CUT_6,
	CLASSD_INTPMR_EQCFG_FLAT,
	CLASSD_INTPMR_EQCFG_B_BOOST_6, CLASSD_INTPMR_EQCFG_B_BOOST_12,
	CLASSD_INTPMR_EQCFG_M_BOOST_3, CLASSD_INTPMR_EQCFG_M_BOOST_8,
	CLASSD_INTPMR_EQCFG_T_BOOST_6, CLASSD_INTPMR_EQCFG_T_BOOST_12,
};

static SOC_VALUE_ENUM_SINGLE_DECL(classd_eqcfg_enum,
		CLASSD_INTPMR, CLASSD_INTPMR_EQCFG_SHIFT, 0xf,
		eqcfg_text, eqcfg_value);

static const DECLARE_TLV_DB_SCALE(classd_digital_tlv, -7800, 100, 1);

static const struct snd_kcontrol_new atmel_classd_snd_controls[] = {
SOC_DOUBLE_TLV("Playback Volume", CLASSD_INTPMR,
		CLASSD_INTPMR_ATTL_SHIFT, CLASSD_INTPMR_ATTR_SHIFT,
		78, 1, classd_digital_tlv),

SOC_SINGLE("Deemphasis Switch", CLASSD_INTPMR,
		CLASSD_INTPMR_DEEMP_SHIFT, 1, 0),

SOC_SINGLE("Mono Switch", CLASSD_INTPMR, CLASSD_INTPMR_MONO_SHIFT, 1, 0),

SOC_SINGLE("Swap Switch", CLASSD_INTPMR, CLASSD_INTPMR_SWAP_SHIFT, 1, 0),

SOC_ENUM("Mono Mode", classd_mono_mode_enum),

SOC_ENUM("EQ", classd_eqcfg_enum),
};

static const char * const pwm_type[] = {
	"Single ended", "Differential"
};

static int atmel_classd_component_probe(struct snd_soc_component *component)
{
	struct snd_soc_card *card = snd_soc_component_get_drvdata(component);
	struct atmel_classd *dd = snd_soc_card_get_drvdata(card);
	const struct atmel_classd_pdata *pdata = dd->pdata;
	u32 mask, val;

	mask = CLASSD_MR_PWMTYP_MASK;
	val = pdata->pwm_type << CLASSD_MR_PWMTYP_SHIFT;

	mask |= CLASSD_MR_NON_OVERLAP_MASK;
	if (pdata->non_overlap_enable) {
		val |= (CLASSD_MR_NON_OVERLAP_EN
			<< CLASSD_MR_NON_OVERLAP_SHIFT);

		mask |= CLASSD_MR_NOVR_VAL_MASK;
		switch (pdata->non_overlap_time) {
		case 5:
			val |= (CLASSD_MR_NOVR_VAL_5NS
				<< CLASSD_MR_NOVR_VAL_SHIFT);
			break;
		case 10:
			val |= (CLASSD_MR_NOVR_VAL_10NS
				<< CLASSD_MR_NOVR_VAL_SHIFT);
			break;
		case 15:
			val |= (CLASSD_MR_NOVR_VAL_15NS
				<< CLASSD_MR_NOVR_VAL_SHIFT);
			break;
		case 20:
			val |= (CLASSD_MR_NOVR_VAL_20NS
				<< CLASSD_MR_NOVR_VAL_SHIFT);
			break;
		default:
			val |= (CLASSD_MR_NOVR_VAL_10NS
				<< CLASSD_MR_NOVR_VAL_SHIFT);
			dev_warn(component->dev,
				"non-overlapping value %d is invalid, the default value 10 is specified\n",
				pdata->non_overlap_time);
			break;
		}
	}

	snd_soc_component_update_bits(component, CLASSD_MR, mask, val);

	dev_info(component->dev,
		"PWM modulation type is %s, non-overlapping is %s\n",
		pwm_type[pdata->pwm_type],
		pdata->non_overlap_enable?"enabled":"disabled");

	return 0;
}

static int atmel_classd_component_resume(struct snd_soc_component *component)
{
	struct snd_soc_card *card = snd_soc_component_get_drvdata(component);
	struct atmel_classd *dd = snd_soc_card_get_drvdata(card);

	return regcache_sync(dd->regmap);
}

static int atmel_classd_cpu_dai_mute_stream(struct snd_soc_dai *cpu_dai,
					    int mute, int direction)
{
	struct snd_soc_component *component = cpu_dai->component;
	u32 mask, val;

	mask = CLASSD_MR_LMUTE_MASK | CLASSD_MR_RMUTE_MASK;

	if (mute)
		val = mask;
	else
		val = 0;

	snd_soc_component_update_bits(component, CLASSD_MR, mask, val);

	return 0;
}

#define CLASSD_GCLK_RATE_11M2896_MPY_8 (112896 * 100 * 8)
#define CLASSD_GCLK_RATE_12M288_MPY_8  (12288 * 1000 * 8)

static struct {
	int rate;
	int sample_rate;
	int dsp_clk;
	unsigned long gclk_rate;
} const sample_rates[] = {
	{ 8000,  CLASSD_INTPMR_FRAME_8K,
	CLASSD_INTPMR_DSP_CLK_FREQ_12M288, CLASSD_GCLK_RATE_12M288_MPY_8 },
	{ 16000, CLASSD_INTPMR_FRAME_16K,
	CLASSD_INTPMR_DSP_CLK_FREQ_12M288, CLASSD_GCLK_RATE_12M288_MPY_8 },
	{ 32000, CLASSD_INTPMR_FRAME_32K,
	CLASSD_INTPMR_DSP_CLK_FREQ_12M288, CLASSD_GCLK_RATE_12M288_MPY_8 },
	{ 48000, CLASSD_INTPMR_FRAME_48K,
	CLASSD_INTPMR_DSP_CLK_FREQ_12M288, CLASSD_GCLK_RATE_12M288_MPY_8 },
	{ 96000, CLASSD_INTPMR_FRAME_96K,
	CLASSD_INTPMR_DSP_CLK_FREQ_12M288, CLASSD_GCLK_RATE_12M288_MPY_8 },
	{ 22050, CLASSD_INTPMR_FRAME_22K,
	CLASSD_INTPMR_DSP_CLK_FREQ_11M2896, CLASSD_GCLK_RATE_11M2896_MPY_8 },
	{ 44100, CLASSD_INTPMR_FRAME_44K,
	CLASSD_INTPMR_DSP_CLK_FREQ_11M2896, CLASSD_GCLK_RATE_11M2896_MPY_8 },
	{ 88200, CLASSD_INTPMR_FRAME_88K,
	CLASSD_INTPMR_DSP_CLK_FREQ_11M2896, CLASSD_GCLK_RATE_11M2896_MPY_8 },
};

static int
atmel_classd_cpu_dai_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct atmel_classd *dd = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = cpu_dai->component;
	int fs;
	int i, best, best_val, cur_val, ret;
	u32 mask, val;

	fs = params_rate(params);

	best = 0;
	best_val = abs(fs - sample_rates[0].rate);
	for (i = 1; i < ARRAY_SIZE(sample_rates); i++) {
		/* Closest match */
		cur_val = abs(fs - sample_rates[i].rate);
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}

	dev_dbg(component->dev,
		"Selected SAMPLE_RATE of %dHz, GCLK_RATE of %ldHz\n",
		sample_rates[best].rate, sample_rates[best].gclk_rate);

	clk_disable_unprepare(dd->gclk);

	ret = clk_set_rate(dd->gclk, sample_rates[best].gclk_rate);
	if (ret)
		return ret;

	mask = CLASSD_INTPMR_DSP_CLK_FREQ_MASK | CLASSD_INTPMR_FRAME_MASK;
	val = (sample_rates[best].dsp_clk << CLASSD_INTPMR_DSP_CLK_FREQ_SHIFT)
	| (sample_rates[best].sample_rate << CLASSD_INTPMR_FRAME_SHIFT);

	snd_soc_component_update_bits(component, CLASSD_INTPMR, mask, val);

	return clk_prepare_enable(dd->gclk);
}

static void
atmel_classd_cpu_dai_shutdown(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct atmel_classd *dd = snd_soc_card_get_drvdata(rtd->card);

	clk_disable_unprepare(dd->gclk);
}

static int atmel_classd_cpu_dai_prepare(struct snd_pcm_substream *substream,
					struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_component *component = cpu_dai->component;

	snd_soc_component_update_bits(component, CLASSD_MR,
				CLASSD_MR_LEN_MASK | CLASSD_MR_REN_MASK,
				(CLASSD_MR_LEN_DIS << CLASSD_MR_LEN_SHIFT)
				|(CLASSD_MR_REN_DIS << CLASSD_MR_REN_SHIFT));

	return 0;
}

static int atmel_classd_cpu_dai_trigger(struct snd_pcm_substream *substream,
					int cmd, struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_component *component = cpu_dai->component;
	u32 mask, val;

	mask = CLASSD_MR_LEN_MASK | CLASSD_MR_REN_MASK;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = mask;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = (CLASSD_MR_LEN_DIS << CLASSD_MR_LEN_SHIFT)
			| (CLASSD_MR_REN_DIS << CLASSD_MR_REN_SHIFT);
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, CLASSD_MR, mask, val);

	return 0;
}

static const struct snd_soc_dai_ops atmel_classd_cpu_dai_ops = {
	.startup        = atmel_classd_cpu_dai_startup,
	.shutdown       = atmel_classd_cpu_dai_shutdown,
	.mute_stream	= atmel_classd_cpu_dai_mute_stream,
	.hw_params	= atmel_classd_cpu_dai_hw_params,
	.prepare	= atmel_classd_cpu_dai_prepare,
	.trigger	= atmel_classd_cpu_dai_trigger,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver atmel_classd_cpu_dai = {
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= ATMEL_CLASSD_RATES,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &atmel_classd_cpu_dai_ops,
};

static const struct snd_soc_component_driver atmel_classd_cpu_dai_component = {
	.name			= "atmel-classd",
	.probe			= atmel_classd_component_probe,
	.resume			= atmel_classd_component_resume,
	.controls		= atmel_classd_snd_controls,
	.num_controls		= ARRAY_SIZE(atmel_classd_snd_controls),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.legacy_dai_naming	= 1,
};

/* ASoC sound card */
static int atmel_classd_asoc_card_init(struct device *dev,
					struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	struct atmel_classd *dd = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai_link_component *comp;

	dai_link = devm_kzalloc(dev, sizeof(*dai_link), GFP_KERNEL);
	if (!dai_link)
		return -ENOMEM;

	comp = devm_kzalloc(dev, sizeof(*comp), GFP_KERNEL);
	if (!comp)
		return -ENOMEM;

	dai_link->cpus		= comp;
	dai_link->codecs	= &asoc_dummy_dlc;

	dai_link->num_cpus	= 1;
	dai_link->num_codecs	= 1;

	dai_link->name			= "CLASSD";
	dai_link->stream_name		= "CLASSD PCM";
	dai_link->cpus->dai_name	= dev_name(dev);

	card->dai_link	= dai_link;
	card->num_links	= 1;
	card->name	= dd->pdata->card_name;
	card->dev	= dev;

	return 0;
};

/* regmap configuration */
static const struct reg_default atmel_classd_reg_defaults[] = {
	{ CLASSD_INTPMR,   0x00301212 },
};

#define ATMEL_CLASSD_REG_MAX    0xE4
static const struct regmap_config atmel_classd_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= ATMEL_CLASSD_REG_MAX,

	.cache_type		= REGCACHE_FLAT,
	.reg_defaults		= atmel_classd_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(atmel_classd_reg_defaults),
};

static int atmel_classd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct atmel_classd *dd;
	struct resource *res;
	void __iomem *io_base;
	const struct atmel_classd_pdata *pdata;
	struct snd_soc_card *card;
	int ret;

	pdata = dev_get_platdata(dev);
	if (!pdata) {
		pdata = atmel_classd_dt_init(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	dd = devm_kzalloc(dev, sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;

	dd->pdata = pdata;

	dd->irq = platform_get_irq(pdev, 0);
	if (dd->irq < 0)
		return dd->irq;

	dd->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dd->pclk)) {
		ret = PTR_ERR(dd->pclk);
		dev_err(dev, "failed to get peripheral clock: %d\n", ret);
		return ret;
	}

	dd->gclk = devm_clk_get(dev, "gclk");
	if (IS_ERR(dd->gclk)) {
		ret = PTR_ERR(dd->gclk);
		dev_err(dev, "failed to get GCK clock: %d\n", ret);
		return ret;
	}

	io_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	dd->phy_base = res->start;
	dd->dev = dev;

	dd->regmap = devm_regmap_init_mmio(dev, io_base,
					&atmel_classd_regmap_config);
	if (IS_ERR(dd->regmap)) {
		ret = PTR_ERR(dd->regmap);
		dev_err(dev, "failed to init register map: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(dev,
					&atmel_classd_cpu_dai_component,
					&atmel_classd_cpu_dai, 1);
	if (ret) {
		dev_err(dev, "could not register CPU DAI: %d\n", ret);
		return ret;
	}

	ret = devm_snd_dmaengine_pcm_register(dev,
					&atmel_classd_dmaengine_pcm_config,
					0);
	if (ret) {
		dev_err(dev, "could not register platform: %d\n", ret);
		return ret;
	}

	/* register sound card */
	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card) {
		ret = -ENOMEM;
		goto unregister_codec;
	}

	snd_soc_card_set_drvdata(card, dd);

	ret = atmel_classd_asoc_card_init(dev, card);
	if (ret) {
		dev_err(dev, "failed to init sound card\n");
		goto unregister_codec;
	}

	ret = devm_snd_soc_register_card(dev, card);
	if (ret) {
		dev_err(dev, "failed to register sound card: %d\n", ret);
		goto unregister_codec;
	}

	return 0;

unregister_codec:
	return ret;
}

static struct platform_driver atmel_classd_driver = {
	.driver	= {
		.name		= "atmel-classd",
		.of_match_table	= of_match_ptr(atmel_classd_of_match),
		.pm		= &snd_soc_pm_ops,
	},
	.probe	= atmel_classd_probe,
};
module_platform_driver(atmel_classd_driver);

MODULE_DESCRIPTION("Atmel ClassD driver under ALSA SoC architecture");
MODULE_AUTHOR("Songjun Wu <songjun.wu@atmel.com>");
MODULE_LICENSE("GPL");
