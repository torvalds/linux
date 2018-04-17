// SPDX-License-Identifier: GPL-2.0
//
// Socionext UniPhier AIO ALSA CPU DAI driver.
//
// Copyright (c) 2016-2018 Socionext Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; version 2
// of the License.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "aio.h"

static bool is_valid_pll(struct uniphier_aio_chip *chip, int pll_id)
{
	struct device *dev = &chip->pdev->dev;

	if (pll_id < 0 || chip->num_plls <= pll_id) {
		dev_err(dev, "PLL(%d) is not supported\n", pll_id);
		return false;
	}

	return chip->plls[pll_id].enable;
}

static bool match_spec(const struct uniphier_aio_spec *spec,
		       const char *name, int dir)
{
	if (dir == SNDRV_PCM_STREAM_PLAYBACK &&
	    spec->swm.dir != PORT_DIR_OUTPUT) {
		return false;
	}

	if (dir == SNDRV_PCM_STREAM_CAPTURE &&
	    spec->swm.dir != PORT_DIR_INPUT) {
		return false;
	}

	if (spec->name && strcmp(spec->name, name) == 0)
		return true;

	if (spec->gname && strcmp(spec->gname, name) == 0)
		return true;

	return false;
}

/**
 * find_spec - find HW specification info by name
 * @aio: the AIO device pointer
 * @name: name of device
 * @direction: the direction of substream, SNDRV_PCM_STREAM_*
 *
 * Find hardware specification information from list by device name. This
 * information is used for telling the difference of SoCs to driver.
 *
 * Specification list is array of 'struct uniphier_aio_spec' which is defined
 * in each drivers (see: aio-i2s.c).
 *
 * Return: The pointer of hardware specification of AIO if successful,
 * otherwise NULL on error.
 */
static const struct uniphier_aio_spec *find_spec(struct uniphier_aio *aio,
						 const char *name,
						 int direction)
{
	const struct uniphier_aio_chip_spec *chip_spec = aio->chip->chip_spec;
	int i;

	for (i = 0; i < chip_spec->num_specs; i++) {
		const struct uniphier_aio_spec *spec = &chip_spec->specs[i];

		if (match_spec(spec, name, direction))
			return spec;
	}

	return NULL;
}

/**
 * find_divider - find clock divider by frequency
 * @aio: the AIO device pointer
 * @pll_id: PLL ID, should be AUD_PLL_XX
 * @freq: required frequency
 *
 * Find suitable clock divider by frequency.
 *
 * Return: The ID of PLL if successful, otherwise negative error value.
 */
static int find_divider(struct uniphier_aio *aio, int pll_id, unsigned int freq)
{
	struct uniphier_aio_pll *pll;
	int mul[] = { 1, 1, 1, 2, };
	int div[] = { 2, 3, 1, 3, };
	int i;

	if (!is_valid_pll(aio->chip, pll_id))
		return -EINVAL;

	pll = &aio->chip->plls[pll_id];
	for (i = 0; i < ARRAY_SIZE(mul); i++)
		if (pll->freq * mul[i] / div[i] == freq)
			return i;

	return -ENOTSUPP;
}

static int uniphier_aio_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				   unsigned int freq, int dir)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	struct device *dev = &aio->chip->pdev->dev;
	bool pll_auto = false;
	int pll_id, div_id;

	switch (clk_id) {
	case AUD_CLK_IO:
		return -ENOTSUPP;
	case AUD_CLK_A1:
		pll_id = AUD_PLL_A1;
		break;
	case AUD_CLK_F1:
		pll_id = AUD_PLL_F1;
		break;
	case AUD_CLK_A2:
		pll_id = AUD_PLL_A2;
		break;
	case AUD_CLK_F2:
		pll_id = AUD_PLL_F2;
		break;
	case AUD_CLK_A:
		pll_id = AUD_PLL_A1;
		pll_auto = true;
		break;
	case AUD_CLK_F:
		pll_id = AUD_PLL_F1;
		pll_auto = true;
		break;
	case AUD_CLK_APLL:
		pll_id = AUD_PLL_APLL;
		break;
	case AUD_CLK_RX0:
		pll_id = AUD_PLL_RX0;
		break;
	case AUD_CLK_USB0:
		pll_id = AUD_PLL_USB0;
		break;
	case AUD_CLK_HSC0:
		pll_id = AUD_PLL_HSC0;
		break;
	default:
		dev_err(dev, "Sysclk(%d) is not supported\n", clk_id);
		return -EINVAL;
	}

	if (pll_auto) {
		for (pll_id = 0; pll_id < aio->chip->num_plls; pll_id++) {
			div_id = find_divider(aio, pll_id, freq);
			if (div_id >= 0) {
				aio->plldiv = div_id;
				break;
			}
		}
		if (pll_id == aio->chip->num_plls) {
			dev_err(dev, "Sysclk frequency is not supported(%d)\n",
				freq);
			return -EINVAL;
		}
	}

	if (dir == SND_SOC_CLOCK_OUT)
		aio->pll_out = pll_id;
	else
		aio->pll_in = pll_id;

	return 0;
}

static int uniphier_aio_set_pll(struct snd_soc_dai *dai, int pll_id,
				int source, unsigned int freq_in,
				unsigned int freq_out)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	struct device *dev = &aio->chip->pdev->dev;
	int ret;

	if (!is_valid_pll(aio->chip, pll_id))
		return -EINVAL;
	if (!aio->chip->plls[pll_id].enable) {
		dev_err(dev, "PLL(%d) is not implemented\n", pll_id);
		return -ENOTSUPP;
	}

	ret = aio_chip_set_pll(aio->chip, pll_id, freq_out);
	if (ret < 0)
		return ret;

	return 0;
}

static int uniphier_aio_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	struct device *dev = &aio->chip->pdev->dev;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_I2S:
		aio->fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		dev_err(dev, "Format is not supported(%d)\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	return 0;
}

static int uniphier_aio_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	struct uniphier_aio_sub *sub = &aio->sub[substream->stream];
	int ret;

	sub->substream = substream;
	sub->pass_through = 0;
	sub->use_mmap = true;

	ret = aio_init(sub);
	if (ret)
		return ret;

	return 0;
}

static void uniphier_aio_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	struct uniphier_aio_sub *sub = &aio->sub[substream->stream];

	sub->substream = NULL;
}

static int uniphier_aio_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	struct uniphier_aio_sub *sub = &aio->sub[substream->stream];
	struct device *dev = &aio->chip->pdev->dev;
	int freq, ret;

	switch (params_rate(params)) {
	case 48000:
	case 32000:
	case 24000:
		freq = 12288000;
		break;
	case 44100:
	case 22050:
		freq = 11289600;
		break;
	default:
		dev_err(dev, "Rate is not supported(%d)\n",
			params_rate(params));
		return -EINVAL;
	}
	ret = snd_soc_dai_set_sysclk(dai, AUD_CLK_A,
				     freq, SND_SOC_CLOCK_OUT);
	if (ret)
		return ret;

	sub->params = *params;
	sub->setting = 1;

	aio_port_reset(sub);
	aio_src_reset(sub);

	return 0;
}

static int uniphier_aio_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	struct uniphier_aio_sub *sub = &aio->sub[substream->stream];

	sub->setting = 0;

	return 0;
}

static int uniphier_aio_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	struct uniphier_aio_sub *sub = &aio->sub[substream->stream];
	int ret;

	ret = aio_port_set_param(sub, sub->pass_through, &sub->params);
	if (ret)
		return ret;
	ret = aio_src_set_param(sub, &sub->params);
	if (ret)
		return ret;
	aio_port_set_enable(sub, 1);

	ret = aio_if_set_param(sub, sub->pass_through);
	if (ret)
		return ret;

	if (sub->swm->type == PORT_TYPE_CONV) {
		ret = aio_srcif_set_param(sub);
		if (ret)
			return ret;
		ret = aio_srcch_set_param(sub);
		if (ret)
			return ret;
		aio_srcch_set_enable(sub, 1);
	}

	return 0;
}

const struct snd_soc_dai_ops uniphier_aio_i2s_ops = {
	.set_sysclk  = uniphier_aio_set_sysclk,
	.set_pll     = uniphier_aio_set_pll,
	.set_fmt     = uniphier_aio_set_fmt,
	.startup     = uniphier_aio_startup,
	.shutdown    = uniphier_aio_shutdown,
	.hw_params   = uniphier_aio_hw_params,
	.hw_free     = uniphier_aio_hw_free,
	.prepare     = uniphier_aio_prepare,
};
EXPORT_SYMBOL_GPL(uniphier_aio_i2s_ops);

const struct snd_soc_dai_ops uniphier_aio_spdif_ops = {
	.set_sysclk  = uniphier_aio_set_sysclk,
	.set_pll     = uniphier_aio_set_pll,
	.startup     = uniphier_aio_startup,
	.shutdown    = uniphier_aio_shutdown,
	.hw_params   = uniphier_aio_hw_params,
	.hw_free     = uniphier_aio_hw_free,
	.prepare     = uniphier_aio_prepare,
};
EXPORT_SYMBOL_GPL(uniphier_aio_spdif_ops);

int uniphier_aio_dai_probe(struct snd_soc_dai *dai)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	int i;

	for (i = 0; i < ARRAY_SIZE(aio->sub); i++) {
		struct uniphier_aio_sub *sub = &aio->sub[i];
		const struct uniphier_aio_spec *spec;

		spec = find_spec(aio, dai->name, i);
		if (!spec)
			continue;

		sub->swm = &spec->swm;
		sub->spec = spec;
	}

	aio_iecout_set_enable(aio->chip, true);
	aio_chip_init(aio->chip);
	aio->chip->active = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(uniphier_aio_dai_probe);

int uniphier_aio_dai_remove(struct snd_soc_dai *dai)
{
	struct uniphier_aio *aio = uniphier_priv(dai);

	aio->chip->active = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(uniphier_aio_dai_remove);

int uniphier_aio_dai_suspend(struct snd_soc_dai *dai)
{
	struct uniphier_aio *aio = uniphier_priv(dai);

	reset_control_assert(aio->chip->rst);
	clk_disable_unprepare(aio->chip->clk);

	return 0;
}
EXPORT_SYMBOL_GPL(uniphier_aio_dai_suspend);

int uniphier_aio_dai_resume(struct snd_soc_dai *dai)
{
	struct uniphier_aio *aio = uniphier_priv(dai);
	int ret, i;

	if (!aio->chip->active)
		return 0;

	ret = clk_prepare_enable(aio->chip->clk);
	if (ret)
		return ret;

	ret = reset_control_deassert(aio->chip->rst);
	if (ret)
		goto err_out_clock;

	aio_iecout_set_enable(aio->chip, true);
	aio_chip_init(aio->chip);

	for (i = 0; i < ARRAY_SIZE(aio->sub); i++) {
		struct uniphier_aio_sub *sub = &aio->sub[i];

		if (!sub->spec || !sub->substream)
			continue;

		ret = aio_init(sub);
		if (ret)
			goto err_out_clock;

		if (!sub->setting)
			continue;

		aio_port_reset(sub);
		aio_src_reset(sub);
	}

	return 0;

err_out_clock:
	clk_disable_unprepare(aio->chip->clk);

	return ret;
}
EXPORT_SYMBOL_GPL(uniphier_aio_dai_resume);

static const struct snd_soc_component_driver uniphier_aio_component = {
	.name = "uniphier-aio",
};

int uniphier_aio_probe(struct platform_device *pdev)
{
	struct uniphier_aio_chip *chip;
	struct device *dev = &pdev->dev;
	int ret, i, j;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->chip_spec = of_device_get_match_data(dev);
	if (!chip->chip_spec)
		return -EINVAL;

	chip->regmap_sg = syscon_regmap_lookup_by_phandle(dev->of_node,
							  "socionext,syscon");
	if (IS_ERR(chip->regmap_sg)) {
		if (PTR_ERR(chip->regmap_sg) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		chip->regmap_sg = NULL;
	}

	chip->clk = devm_clk_get(dev, "aio");
	if (IS_ERR(chip->clk))
		return PTR_ERR(chip->clk);

	chip->rst = devm_reset_control_get_shared(dev, "aio");
	if (IS_ERR(chip->rst))
		return PTR_ERR(chip->rst);

	chip->num_aios = chip->chip_spec->num_dais;
	chip->aios = devm_kzalloc(dev,
				  sizeof(struct uniphier_aio) * chip->num_aios,
				  GFP_KERNEL);
	if (!chip->aios)
		return -ENOMEM;

	chip->num_plls = chip->chip_spec->num_plls;
	chip->plls = devm_kzalloc(dev, sizeof(struct uniphier_aio_pll) *
				  chip->num_plls, GFP_KERNEL);
	if (!chip->plls)
		return -ENOMEM;
	memcpy(chip->plls, chip->chip_spec->plls,
	       sizeof(struct uniphier_aio_pll) * chip->num_plls);

	for (i = 0; i < chip->num_aios; i++) {
		struct uniphier_aio *aio = &chip->aios[i];

		aio->chip = chip;
		aio->fmt = SND_SOC_DAIFMT_I2S;

		for (j = 0; j < ARRAY_SIZE(aio->sub); j++) {
			struct uniphier_aio_sub *sub = &aio->sub[j];

			sub->aio = aio;
			spin_lock_init(&sub->lock);
		}
	}

	chip->pdev = pdev;
	platform_set_drvdata(pdev, chip);

	ret = clk_prepare_enable(chip->clk);
	if (ret)
		return ret;

	ret = reset_control_deassert(chip->rst);
	if (ret)
		goto err_out_clock;

	ret = devm_snd_soc_register_component(dev, &uniphier_aio_component,
					      chip->chip_spec->dais,
					      chip->chip_spec->num_dais);
	if (ret) {
		dev_err(dev, "Register component failed.\n");
		goto err_out_reset;
	}

	ret = uniphier_aiodma_soc_register_platform(pdev);
	if (ret) {
		dev_err(dev, "Register platform failed.\n");
		goto err_out_reset;
	}

	return 0;

err_out_reset:
	reset_control_assert(chip->rst);

err_out_clock:
	clk_disable_unprepare(chip->clk);

	return ret;
}
EXPORT_SYMBOL_GPL(uniphier_aio_probe);

int uniphier_aio_remove(struct platform_device *pdev)
{
	struct uniphier_aio_chip *chip = platform_get_drvdata(pdev);

	reset_control_assert(chip->rst);
	clk_disable_unprepare(chip->clk);

	return 0;
}
EXPORT_SYMBOL_GPL(uniphier_aio_remove);

MODULE_AUTHOR("Katsuhiro Suzuki <suzuki.katsuhiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier AIO CPU DAI driver.");
MODULE_LICENSE("GPL v2");
