/*
 * rk2928-card.c  --  SoC audio for RockChip RK2928
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

#ifdef DEBUG
#define DBG(format, ...) \
		printk(KERN_INFO "RK2928 Card: " format "\n", ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

static int rk2928_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int pll_out = 0, dai_fmt = rtd->card->dai_link->dai_fmt;
	int div_bclk,div_mclk;
	int ret;
	  
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_fmt);
	if (ret < 0) {
		printk("%s():failed to set the format for codec side\n", __FUNCTION__);
		return ret;
	}

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		printk("%s():failed to set the format for cpu side\n", __FUNCTION__);
		return ret;
	}

	DBG("Enter:%s, %d, rate=%d\n",__FUNCTION__,__LINE__,params_rate(params));
	pll_out = 256 * params_rate(params);

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);

	if ((dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBS_CFS) {
		div_bclk = 63;
		div_mclk = pll_out/(params_rate(params)*64) - 1;

		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
		snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);
	}
	
	return 0;
}

static struct snd_soc_ops rk2928_dai_ops = {
	.hw_params = rk2928_dai_hw_params,
};

static struct snd_soc_dai_link rk2928_dai[] = {
	{
		.name = "RK2928",
		.stream_name = "RK2928",
		.codec_dai_name = "rk2928-codec",
		.ops = &rk2928_dai_ops,
	},
};

/* Audio machine driver */
static struct snd_soc_card rockchip_rk2928_snd_card = {
	.name = "RK2928",
	.dai_link = rk2928_dai,
	.num_links = ARRAY_SIZE(rk2928_dai),
};

static int rockchip_rk2928_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_rk2928_snd_card;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);

	if (ret)
		printk("%s() register card failed:%d\n", __FUNCTION__, ret);

	return ret;
}

static int rockchip_rk2928_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_rk2928_of_match[] = {
	{ .compatible = "rockchip-rk2928", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_rk2928_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_rk2928_audio_driver = {
	.driver         = {
		.name   = "rockchip-rk2928",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_rk2928_of_match),
	},
	.probe          = rockchip_rk2928_audio_probe,
	.remove         = rockchip_rk2928_audio_remove,
};

module_platform_driver(rockchip_rk2928_audio_driver);

MODULE_DESCRIPTION("ALSA SoC RK2928");
MODULE_LICENSE("GPL");
