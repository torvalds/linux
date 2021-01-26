// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8804.c  --  WM8804 S/PDIF transceiver driver
 *
 * Copyright 2010-11 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/soc-dapm.h>

#include "wm8804.h"

#define WM8804_NUM_SUPPLIES 2
static const char *wm8804_supply_names[WM8804_NUM_SUPPLIES] = {
	"PVDD",
	"DVDD"
};

static const struct reg_default wm8804_reg_defaults[] = {
	{ 3,  0x21 },     /* R3  - PLL1 */
	{ 4,  0xFD },     /* R4  - PLL2 */
	{ 5,  0x36 },     /* R5  - PLL3 */
	{ 6,  0x07 },     /* R6  - PLL4 */
	{ 7,  0x16 },     /* R7  - PLL5 */
	{ 8,  0x18 },     /* R8  - PLL6 */
	{ 9,  0xFF },     /* R9  - SPDMODE */
	{ 10, 0x00 },     /* R10 - INTMASK */
	{ 18, 0x00 },     /* R18 - SPDTX1 */
	{ 19, 0x00 },     /* R19 - SPDTX2 */
	{ 20, 0x00 },     /* R20 - SPDTX3 */
	{ 21, 0x71 },     /* R21 - SPDTX4 */
	{ 22, 0x0B },     /* R22 - SPDTX5 */
	{ 23, 0x70 },     /* R23 - GPO0 */
	{ 24, 0x57 },     /* R24 - GPO1 */
	{ 26, 0x42 },     /* R26 - GPO2 */
	{ 27, 0x06 },     /* R27 - AIFTX */
	{ 28, 0x06 },     /* R28 - AIFRX */
	{ 29, 0x80 },     /* R29 - SPDRX1 */
	{ 30, 0x07 },     /* R30 - PWRDN */
};

struct wm8804_priv {
	struct device *dev;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[WM8804_NUM_SUPPLIES];
	struct notifier_block disable_nb[WM8804_NUM_SUPPLIES];
	int mclk_div;

	struct gpio_desc *reset;

	int aif_pwr;
};

static int txsrc_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol);

static int wm8804_aif_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event);

/*
 * We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define WM8804_REGULATOR_EVENT(n) \
static int wm8804_regulator_event_##n(struct notifier_block *nb, \
				      unsigned long event, void *data)    \
{ \
	struct wm8804_priv *wm8804 = container_of(nb, struct wm8804_priv, \
						  disable_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		regcache_mark_dirty(wm8804->regmap);	\
	} \
	return 0; \
}

WM8804_REGULATOR_EVENT(0)
WM8804_REGULATOR_EVENT(1)

static const char *txsrc_text[] = { "S/PDIF RX", "AIF" };
static SOC_ENUM_SINGLE_DECL(txsrc, WM8804_SPDTX4, 6, txsrc_text);

static const struct snd_kcontrol_new wm8804_tx_source_mux[] = {
	SOC_DAPM_ENUM_EXT("Input Source", txsrc,
			  snd_soc_dapm_get_enum_double, txsrc_put),
};

static const struct snd_soc_dapm_widget wm8804_dapm_widgets[] = {
SND_SOC_DAPM_OUTPUT("SPDIF Out"),
SND_SOC_DAPM_INPUT("SPDIF In"),

SND_SOC_DAPM_PGA("SPDIFTX", WM8804_PWRDN, 2, 1, NULL, 0),
SND_SOC_DAPM_PGA("SPDIFRX", WM8804_PWRDN, 1, 1, NULL, 0),

SND_SOC_DAPM_MUX("Tx Source", SND_SOC_NOPM, 6, 0, wm8804_tx_source_mux),

SND_SOC_DAPM_AIF_OUT_E("AIFTX", NULL, 0, SND_SOC_NOPM, 0, 0, wm8804_aif_event,
		       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_AIF_IN_E("AIFRX", NULL, 0, SND_SOC_NOPM, 0, 0, wm8804_aif_event,
		      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route wm8804_dapm_routes[] = {
	{ "AIFRX", NULL, "Playback" },
	{ "Tx Source", "AIF", "AIFRX" },

	{ "SPDIFRX", NULL, "SPDIF In" },
	{ "Tx Source", "S/PDIF RX", "SPDIFRX" },

	{ "SPDIFTX", NULL, "Tx Source" },
	{ "SPDIF Out", NULL, "SPDIFTX" },

	{ "AIFTX", NULL, "SPDIFRX" },
	{ "Capture", NULL, "AIFTX" },
};

static int wm8804_aif_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm8804_priv *wm8804 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* power up the aif */
		if (!wm8804->aif_pwr)
			snd_soc_component_update_bits(component, WM8804_PWRDN, 0x10, 0x0);
		wm8804->aif_pwr++;
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* power down only both paths are disabled */
		wm8804->aif_pwr--;
		if (!wm8804->aif_pwr)
			snd_soc_component_update_bits(component, WM8804_PWRDN, 0x10, 0x10);
		break;
	}

	return 0;
}

static int txsrc_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val = ucontrol->value.enumerated.item[0] << e->shift_l;
	unsigned int mask = 1 << e->shift_l;
	unsigned int txpwr;

	if (val != 0 && val != mask)
		return -EINVAL;

	snd_soc_dapm_mutex_lock(dapm);

	if (snd_soc_component_test_bits(component, e->reg, mask, val)) {
		/* save the current power state of the transmitter */
		txpwr = snd_soc_component_read(component, WM8804_PWRDN) & 0x4;

		/* power down the transmitter */
		snd_soc_component_update_bits(component, WM8804_PWRDN, 0x4, 0x4);

		/* set the tx source */
		snd_soc_component_update_bits(component, e->reg, mask, val);

		/* restore the transmitter's configuration */
		snd_soc_component_update_bits(component, WM8804_PWRDN, 0x4, txpwr);
	}

	snd_soc_dapm_mutex_unlock(dapm);

	return 0;
}

static bool wm8804_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8804_RST_DEVID1:
	case WM8804_DEVID2:
	case WM8804_DEVREV:
	case WM8804_INTSTAT:
	case WM8804_SPDSTAT:
	case WM8804_RXCHAN1:
	case WM8804_RXCHAN2:
	case WM8804_RXCHAN3:
	case WM8804_RXCHAN4:
	case WM8804_RXCHAN5:
		return true;
	default:
		return false;
	}
}

static int wm8804_soft_reset(struct wm8804_priv *wm8804)
{
	return regmap_write(wm8804->regmap, WM8804_RST_DEVID1, 0x0);
}

static int wm8804_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component;
	u16 format, master, bcp, lrp;

	component = dai->component;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format = 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		format = 0x0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format = 0x1;
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		format = 0x3;
		break;
	default:
		dev_err(dai->dev, "Unknown dai format\n");
		return -EINVAL;
	}

	/* set data format */
	snd_soc_component_update_bits(component, WM8804_AIFTX, 0x3, format);
	snd_soc_component_update_bits(component, WM8804_AIFRX, 0x3, format);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		master = 0;
		break;
	default:
		dev_err(dai->dev, "Unknown master/slave configuration\n");
		return -EINVAL;
	}

	/* set master/slave mode */
	snd_soc_component_update_bits(component, WM8804_AIFRX, 0x40, master << 6);

	bcp = lrp = 0;
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bcp = lrp = 1;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bcp = 1;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrp = 1;
		break;
	default:
		dev_err(dai->dev, "Unknown polarity configuration\n");
		return -EINVAL;
	}

	/* set frame inversion */
	snd_soc_component_update_bits(component, WM8804_AIFTX, 0x10 | 0x20,
			    (bcp << 4) | (lrp << 5));
	snd_soc_component_update_bits(component, WM8804_AIFRX, 0x10 | 0x20,
			    (bcp << 4) | (lrp << 5));
	return 0;
}

static int wm8804_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component;
	u16 blen;

	component = dai->component;

	switch (params_width(params)) {
	case 16:
		blen = 0x0;
		break;
	case 20:
		blen = 0x1;
		break;
	case 24:
		blen = 0x2;
		break;
	default:
		dev_err(dai->dev, "Unsupported word length: %u\n",
			params_width(params));
		return -EINVAL;
	}

	/* set word length */
	snd_soc_component_update_bits(component, WM8804_AIFTX, 0xc, blen << 2);
	snd_soc_component_update_bits(component, WM8804_AIFRX, 0xc, blen << 2);

	return 0;
}

struct pll_div {
	u32 prescale:1;
	u32 mclkdiv:1;
	u32 freqmode:2;
	u32 n:4;
	u32 k:22;
};

/* PLL rate to output rate divisions */
static struct {
	unsigned int div;
	unsigned int freqmode;
	unsigned int mclkdiv;
} post_table[] = {
	{  2,  0, 0 },
	{  4,  0, 1 },
	{  4,  1, 0 },
	{  8,  1, 1 },
	{  8,  2, 0 },
	{ 16,  2, 1 },
	{ 12,  3, 0 },
	{ 24,  3, 1 }
};

#define FIXED_PLL_SIZE ((1ULL << 22) * 10)
static int pll_factors(struct pll_div *pll_div, unsigned int target,
		       unsigned int source, unsigned int mclk_div)
{
	u64 Kpart;
	unsigned long int K, Ndiv, Nmod, tmp;
	int i;

	/*
	 * Scale the output frequency up; the PLL should run in the
	 * region of 90-100MHz.
	 */
	for (i = 0; i < ARRAY_SIZE(post_table); i++) {
		tmp = target * post_table[i].div;
		if ((tmp >= 90000000 && tmp <= 100000000) &&
		    (mclk_div == post_table[i].mclkdiv)) {
			pll_div->freqmode = post_table[i].freqmode;
			pll_div->mclkdiv = post_table[i].mclkdiv;
			target *= post_table[i].div;
			break;
		}
	}

	if (i == ARRAY_SIZE(post_table)) {
		pr_err("%s: Unable to scale output frequency: %uHz\n",
		       __func__, target);
		return -EINVAL;
	}

	pll_div->prescale = 0;
	Ndiv = target / source;
	if (Ndiv < 5) {
		source >>= 1;
		pll_div->prescale = 1;
		Ndiv = target / source;
	}

	if (Ndiv < 5 || Ndiv > 13) {
		pr_err("%s: WM8804 N value is not within the recommended range: %lu\n",
		       __func__, Ndiv);
		return -EINVAL;
	}
	pll_div->n = Ndiv;

	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (u64)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xffffffff;
	if ((K % 10) >= 5)
		K += 5;
	K /= 10;
	pll_div->k = K;

	return 0;
}

static int wm8804_set_pll(struct snd_soc_dai *dai, int pll_id,
			  int source, unsigned int freq_in,
			  unsigned int freq_out)
{
	struct snd_soc_component *component = dai->component;
	struct wm8804_priv *wm8804 = snd_soc_component_get_drvdata(component);
	bool change;

	if (!freq_in || !freq_out) {
		/* disable the PLL */
		regmap_update_bits_check(wm8804->regmap, WM8804_PWRDN,
					 0x1, 0x1, &change);
		if (change)
			pm_runtime_put(wm8804->dev);
	} else {
		int ret;
		struct pll_div pll_div;

		ret = pll_factors(&pll_div, freq_out, freq_in,
				  wm8804->mclk_div);
		if (ret)
			return ret;

		/* power down the PLL before reprogramming it */
		regmap_update_bits_check(wm8804->regmap, WM8804_PWRDN,
					 0x1, 0x1, &change);
		if (!change)
			pm_runtime_get_sync(wm8804->dev);

		/* set PLLN and PRESCALE */
		snd_soc_component_update_bits(component, WM8804_PLL4, 0xf | 0x10,
				    pll_div.n | (pll_div.prescale << 4));
		/* set mclkdiv and freqmode */
		snd_soc_component_update_bits(component, WM8804_PLL5, 0x3 | 0x8,
				    pll_div.freqmode | (pll_div.mclkdiv << 3));
		/* set PLLK */
		snd_soc_component_write(component, WM8804_PLL1, pll_div.k & 0xff);
		snd_soc_component_write(component, WM8804_PLL2, (pll_div.k >> 8) & 0xff);
		snd_soc_component_write(component, WM8804_PLL3, pll_div.k >> 16);

		/* power up the PLL */
		snd_soc_component_update_bits(component, WM8804_PWRDN, 0x1, 0);
	}

	return 0;
}

static int wm8804_set_sysclk(struct snd_soc_dai *dai,
			     int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component;

	component = dai->component;

	switch (clk_id) {
	case WM8804_TX_CLKSRC_MCLK:
		if ((freq >= 10000000 && freq <= 14400000)
				|| (freq >= 16280000 && freq <= 27000000))
			snd_soc_component_update_bits(component, WM8804_PLL6, 0x80, 0x80);
		else {
			dev_err(dai->dev, "OSCCLOCK is not within the "
				"recommended range: %uHz\n", freq);
			return -EINVAL;
		}
		break;
	case WM8804_TX_CLKSRC_PLL:
		snd_soc_component_update_bits(component, WM8804_PLL6, 0x80, 0);
		break;
	case WM8804_CLKOUT_SRC_CLK1:
		snd_soc_component_update_bits(component, WM8804_PLL6, 0x8, 0);
		break;
	case WM8804_CLKOUT_SRC_OSCCLK:
		snd_soc_component_update_bits(component, WM8804_PLL6, 0x8, 0x8);
		break;
	default:
		dev_err(dai->dev, "Unknown clock source: %d\n", clk_id);
		return -EINVAL;
	}

	return 0;
}

static int wm8804_set_clkdiv(struct snd_soc_dai *dai,
			     int div_id, int div)
{
	struct snd_soc_component *component;
	struct wm8804_priv *wm8804;

	component = dai->component;
	switch (div_id) {
	case WM8804_CLKOUT_DIV:
		snd_soc_component_update_bits(component, WM8804_PLL5, 0x30,
				    (div & 0x3) << 4);
		break;
	case WM8804_MCLK_DIV:
		wm8804 = snd_soc_component_get_drvdata(component);
		wm8804->mclk_div = div;
		break;
	default:
		dev_err(dai->dev, "Unknown clock divider: %d\n", div_id);
		return -EINVAL;
	}
	return 0;
}

static const struct snd_soc_dai_ops wm8804_dai_ops = {
	.hw_params = wm8804_hw_params,
	.set_fmt = wm8804_set_fmt,
	.set_sysclk = wm8804_set_sysclk,
	.set_clkdiv = wm8804_set_clkdiv,
	.set_pll = wm8804_set_pll
};

#define WM8804_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

#define WM8804_RATES (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
		      SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_64000 | \
		      SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | \
		      SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

static struct snd_soc_dai_driver wm8804_dai = {
	.name = "wm8804-spdif",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8804_RATES,
		.formats = WM8804_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8804_RATES,
		.formats = WM8804_FORMATS,
	},
	.ops = &wm8804_dai_ops,
	.symmetric_rates = 1
};

static const struct snd_soc_component_driver soc_component_dev_wm8804 = {
	.dapm_widgets		= wm8804_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8804_dapm_widgets),
	.dapm_routes		= wm8804_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(wm8804_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

const struct regmap_config wm8804_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = WM8804_MAX_REGISTER,
	.volatile_reg = wm8804_volatile,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm8804_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8804_reg_defaults),
};
EXPORT_SYMBOL_GPL(wm8804_regmap_config);

int wm8804_probe(struct device *dev, struct regmap *regmap)
{
	struct wm8804_priv *wm8804;
	unsigned int id1, id2;
	int i, ret;

	wm8804 = devm_kzalloc(dev, sizeof(*wm8804), GFP_KERNEL);
	if (!wm8804)
		return -ENOMEM;

	dev_set_drvdata(dev, wm8804);

	wm8804->dev = dev;
	wm8804->regmap = regmap;

	wm8804->reset = devm_gpiod_get_optional(dev, "wlf,reset",
						GPIOD_OUT_LOW);
	if (IS_ERR(wm8804->reset)) {
		ret = PTR_ERR(wm8804->reset);
		dev_err(dev, "Failed to get reset line: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm8804->supplies); i++)
		wm8804->supplies[i].supply = wm8804_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(wm8804->supplies),
				      wm8804->supplies);
	if (ret) {
		dev_err(dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	wm8804->disable_nb[0].notifier_call = wm8804_regulator_event_0;
	wm8804->disable_nb[1].notifier_call = wm8804_regulator_event_1;

	/* This should really be moved into the regulator core */
	for (i = 0; i < ARRAY_SIZE(wm8804->supplies); i++) {
		struct regulator *regulator = wm8804->supplies[i].consumer;

		ret = devm_regulator_register_notifier(regulator,
						       &wm8804->disable_nb[i]);
		if (ret != 0) {
			dev_err(dev,
				"Failed to register regulator notifier: %d\n",
				ret);
			return ret;
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8804->supplies),
				    wm8804->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(wm8804->reset, 1);

	ret = regmap_read(regmap, WM8804_RST_DEVID1, &id1);
	if (ret < 0) {
		dev_err(dev, "Failed to read device ID: %d\n", ret);
		goto err_reg_enable;
	}

	ret = regmap_read(regmap, WM8804_DEVID2, &id2);
	if (ret < 0) {
		dev_err(dev, "Failed to read device ID: %d\n", ret);
		goto err_reg_enable;
	}

	id2 = (id2 << 8) | id1;

	if (id2 != 0x8805) {
		dev_err(dev, "Invalid device ID: %#x\n", id2);
		ret = -EINVAL;
		goto err_reg_enable;
	}

	ret = regmap_read(regmap, WM8804_DEVREV, &id1);
	if (ret < 0) {
		dev_err(dev, "Failed to read device revision: %d\n",
			ret);
		goto err_reg_enable;
	}
	dev_info(dev, "revision %c\n", id1 + 'A');

	if (!wm8804->reset) {
		ret = wm8804_soft_reset(wm8804);
		if (ret < 0) {
			dev_err(dev, "Failed to issue reset: %d\n", ret);
			goto err_reg_enable;
		}
	}

	ret = devm_snd_soc_register_component(dev, &soc_component_dev_wm8804,
				     &wm8804_dai, 1);
	if (ret < 0) {
		dev_err(dev, "Failed to register CODEC: %d\n", ret);
		goto err_reg_enable;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_reg_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8804->supplies), wm8804->supplies);
	return ret;
}
EXPORT_SYMBOL_GPL(wm8804_probe);

void wm8804_remove(struct device *dev)
{
	pm_runtime_disable(dev);
}
EXPORT_SYMBOL_GPL(wm8804_remove);

#if IS_ENABLED(CONFIG_PM)
static int wm8804_runtime_resume(struct device *dev)
{
	struct wm8804_priv *wm8804 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8804->supplies),
				    wm8804->supplies);
	if (ret) {
		dev_err(wm8804->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	regcache_sync(wm8804->regmap);

	/* Power up OSCCLK */
	regmap_update_bits(wm8804->regmap, WM8804_PWRDN, 0x8, 0x0);

	return 0;
}

static int wm8804_runtime_suspend(struct device *dev)
{
	struct wm8804_priv *wm8804 = dev_get_drvdata(dev);

	/* Power down OSCCLK */
	regmap_update_bits(wm8804->regmap, WM8804_PWRDN, 0x8, 0x8);

	regulator_bulk_disable(ARRAY_SIZE(wm8804->supplies),
			       wm8804->supplies);

	return 0;
}
#endif

const struct dev_pm_ops wm8804_pm = {
	SET_RUNTIME_PM_OPS(wm8804_runtime_suspend, wm8804_runtime_resume, NULL)
};
EXPORT_SYMBOL_GPL(wm8804_pm);

MODULE_DESCRIPTION("ASoC WM8804 driver");
MODULE_AUTHOR("Dimitris Papastamos <dp@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
