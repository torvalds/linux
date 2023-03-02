// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip Audio Delta-sigma Digital Converter Interface
 *
 * Copyright (C) 2023 Rockchip Electronics Co.,Ltd
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/tlv.h>
#include "rk_dsm.h"

#define RK3562_GRF_PERI_AUDIO_CON	(0x0070)

struct rk_dsm_soc_data {
	int (*init)(struct device *dev);
	void (*deinit)(struct device *dev);
};

struct rk_dsm_priv {
	struct regmap *grf;
	struct regmap *regmap;
	struct clk *clk_dac;
	struct clk *pclk;
	unsigned int pa_ctl_delay_ms;
	struct gpio_desc *pa_ctl;
	struct reset_control *rc;
	const struct rk_dsm_soc_data *data;
};

/* DAC digital gain */
static const DECLARE_TLV_DB_SCALE(dac_tlv, -95625, 375, 0);

/* DAC Cutoff for High Pass Filter */
static const char * const dac_hpf_cutoff_text[] = {
	"80Hz", "100Hz", "120Hz", "140Hz",
};

static SOC_ENUM_SINGLE_DECL(dac_hpf_cutoff_enum, DACHPF, 4,
			    dac_hpf_cutoff_text);

static const char * const pa_ctl[] = {"Off", "On"};

static const struct soc_enum pa_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pa_ctl), pa_ctl);

static int rk_dsm_dac_vol_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int val = snd_soc_component_read(component, mc->reg);
	unsigned int sign = snd_soc_component_read(component, DACVOGP);
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int shift = mc->shift;
	int mid = mc->max / 2;
	int uv;

	uv = (val >> shift) & mask;
	if (sign)
		uv = mid + uv;
	else
		uv = mid - uv;

	ucontrol->value.integer.value[0] = uv;
	ucontrol->value.integer.value[1] = uv;

	return 0;
}

static int rk_dsm_dac_vol_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int rreg = mc->rreg;
	unsigned int shift = mc->shift;
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int val, val_mask, sign;
	int uv = ucontrol->value.integer.value[0];
	int min = mc->min;
	int mid = mc->max / 2;

	if (uv > mid) {
		sign = DSM_DACVOGP_VOLGPL0_POS | DSM_DACVOGP_VOLGPR0_POS;
		uv = uv - mid;
	} else {
		sign = DSM_DACVOGP_VOLGPL0_NEG | DSM_DACVOGP_VOLGPR0_NEG;
		uv = mid - uv;
	}

	val = ((uv + min) & mask);
	val_mask = mask << shift;
	val = val << shift;

	snd_soc_component_update_bits(component, reg, val_mask, val);
	snd_soc_component_update_bits(component, rreg, val_mask, val);
	snd_soc_component_write(component, DACVOGP, sign);

	return 0;
}

static int rk_dsm_dac_pa_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rk_dsm_priv *rd = snd_soc_component_get_drvdata(component);

	if (!rd->pa_ctl)
		return -EINVAL;

	ucontrol->value.enumerated.item[0] = gpiod_get_value(rd->pa_ctl);

	return 0;
}

static int rk_dsm_dac_pa_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rk_dsm_priv *rd = snd_soc_component_get_drvdata(component);

	if (!rd->pa_ctl)
		return -EINVAL;

	gpiod_set_value(rd->pa_ctl, ucontrol->value.enumerated.item[0]);

	return 0;
}

static const struct snd_kcontrol_new rk_dsm_snd_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("DAC Digital Volume",
			     DACVOLL0, DACVOLR0, 0, 0x1fe, 0,
			     rk_dsm_dac_vol_get,
			     rk_dsm_dac_vol_put,
			     dac_tlv),

	SOC_ENUM("DAC HPF Cutoff", dac_hpf_cutoff_enum),
	SOC_SINGLE("DAC HPF Switch", DACHPF, 0, 1, 0),
	SOC_ENUM_EXT("Power Amplifier", pa_enum,
		     rk_dsm_dac_pa_get,
		     rk_dsm_dac_pa_put),
};

/*
 * ACDC_CLK  D2A_CLK   D2A_SYNC Sample rates supported
 * 49.152MHz 49.152MHz 6.144MHz 12/24/48/96/192kHz
 * 45.154MHz 45.154MHz 5.644MHz 11.025/22.05/44.1/88.2/176.4kHz
 * 32.768MHz 32.768MHz 4.096MHz 8/16/32/64/128kHz
 */
static void rk_dsm_get_clk(unsigned int samplerate,
			   unsigned int *mclk,
			   unsigned int *sclk)
{
	switch (samplerate) {
	case 12000:
	case 24000:
	case 48000:
	case 96000:
	case 192000:
		*mclk = 49152000;
		*sclk = 6144000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
		*mclk = 45158400;
		*sclk = 5644800;
		break;
	case 8000:
	case 16000:
	case 32000:
	case 64000:
	case 128000:
		*mclk = 32768000;
		*sclk = 4096000;
		break;
	default:
		*mclk = 0;
		*sclk = 0;
		break;
	}
}

static void rk_dsm_enable_clk_dac(struct rk_dsm_priv *rd)
{
	regmap_update_bits(rd->regmap, DACCLKCTRL,
			   DSM_DACCLKCTRL_DAC_CKE_MASK |
			   DSM_DACCLKCTRL_I2SRX_CKE_MASK |
			   DSM_DACCLKCTRL_CKE_BCLKRX_MASK |
			   DSM_DACCLKCTRL_DAC_SYNC_ENA_MASK |
			   DSM_DACCLKCTRL_DAC_MODE_ATTENU_MASK,
			   DSM_DACCLKCTRL_DAC_CKE_EN |
			   DSM_DACCLKCTRL_I2SRX_CKE_EN |
			   DSM_DACCLKCTRL_CKE_BCLKRX_EN |
			   DSM_DACCLKCTRL_DAC_SYNC_ENA_EN |
			   DSM_DACCLKCTRL_DAC_MODE_ATTENU_EN);
}

static int rk_dsm_set_clk(struct rk_dsm_priv *rd,
			  struct snd_pcm_substream *substream,
			  unsigned int samplerate)
{
	unsigned int mclk, sclk, bclk;
	unsigned int div_bclk;

	rk_dsm_get_clk(samplerate, &mclk, &sclk);
	if (!mclk || !sclk)
		return -EINVAL;

	bclk = 64 * samplerate;
	div_bclk = DIV_ROUND_CLOSEST(mclk, bclk);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		clk_set_rate(rd->clk_dac, mclk);

		rk_dsm_enable_clk_dac(rd);

		regmap_update_bits(rd->regmap, DACSCLKRXINT_DIV,
				   DSM_DACSCLKRXINT_DIV_SCKRXDIV_MASK,
				   DSM_DACSCLKRXINT_DIV_SCKRXDIV(div_bclk));
		regmap_update_bits(rd->regmap, I2S_CKR0,
				   DSM_I2S_CKR0_RSD_MASK,
				   DSM_I2S_CKR0_RSD_64);
	}

	return 0;
}

static int rk_dsm_set_dai_fmt(struct snd_soc_dai *dai,
			      unsigned int fmt)
{
	struct rk_dsm_priv *rd =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int mask = 0, val = 0;

	/* master mode only */
	regmap_update_bits(rd->regmap, I2S_CKR1,
			   DSM_I2S_CKR1_MSS_MASK,
			   DSM_I2S_CKR1_MSS_MASTER);

	mask = DSM_I2S_CKR1_CKP_MASK |
	       DSM_I2S_CKR1_RLP_MASK;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		val = DSM_I2S_CKR1_CKP_NORMAL |
		      DSM_I2S_CKR1_RLP_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		val = DSM_I2S_CKR1_CKP_INVERTED |
		      DSM_I2S_CKR1_RLP_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val = DSM_I2S_CKR1_CKP_INVERTED |
		      DSM_I2S_CKR1_RLP_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		val = DSM_I2S_CKR1_CKP_NORMAL |
		      DSM_I2S_CKR1_RLP_INVERTED;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(rd->regmap, I2S_CKR1, mask, val);

	return 0;
}

static int rk_dsm_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct rk_dsm_priv *rd =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int srt = 0, val = 0;

	rk_dsm_set_clk(rd, substream, params_rate(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (params_rate(params)) {
		case 8000:
		case 11025:
		case 12000:
			srt = 0;
			break;
		case 16000:
		case 22050:
		case 24000:
			srt = 1;
			break;
		case 32000:
		case 44100:
		case 48000:
			srt = 2;
			break;
		case 64000:
		case 88200:
		case 96000:
			srt = 3;
			break;
		case 128000:
		case 176400:
		case 192000:
			srt = 4;
			break;
		default:
			return -EINVAL;
		}

		regmap_update_bits(rd->regmap, DACCFG1,
				   DSM_DACCFG1_DACSRT_MASK,
				   DSM_DACCFG1_DACSRT(srt));

		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			val = 16;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
		case SNDRV_PCM_FORMAT_S32_LE:
			val = 24;
			break;
		default:
			return -EINVAL;
		}

		regmap_update_bits(rd->regmap, I2S_RXCR0,
				   DSM_I2S_RXCR0_VDW_MASK,
				   DSM_I2S_RXCR0_VDW(val));
		regmap_update_bits(rd->regmap, DACPWM_CTRL,
				   DSM_DACPWM_CTRL_PWM_MODE_CKE_MASK |
				   DSM_DACPWM_CTRL_PWM_EN_MASK,
				   DSM_DACPWM_CTRL_PWM_MODE_CKE_EN |
				   DSM_DACPWM_CTRL_PWM_EN);
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(rd->regmap, I2S_XFER,
				   DSM_I2S_XFER_RXS_MASK,
				   DSM_I2S_XFER_RXS_START);
		regmap_update_bits(rd->regmap, DACDIGEN,
				   DSM_DACDIGEN_DAC_GLBEN_MASK |
				   DSM_DACDIGEN_DACEN_L0R1_MASK,
				   DSM_DACDIGEN_DAC_GLBEN_EN |
				   DSM_DACDIGEN_DACEN_L0R1_EN);
	}

	return 0;
}

static int rk_dsm_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct rk_dsm_priv *rd =
		snd_soc_component_get_drvdata(dai->component);

	gpiod_set_value(rd->pa_ctl, 1);
	if (rd->pa_ctl_delay_ms)
		msleep(rd->pa_ctl_delay_ms);

	return 0;
}

static void rk_dsm_reset(struct rk_dsm_priv *rd)
{
	if (IS_ERR(rd->rc))
		return;

	reset_control_assert(rd->rc);
	udelay(5);
	reset_control_deassert(rd->rc);
	udelay(5);
}

static void rk_dsm_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct rk_dsm_priv *rd =
		snd_soc_component_get_drvdata(dai->component);

	gpiod_set_value(rd->pa_ctl, 0);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(rd->regmap, DACPWM_CTRL,
				   DSM_DACPWM_CTRL_PWM_MODE_CKE_MASK |
				   DSM_DACPWM_CTRL_PWM_EN_MASK,
				   DSM_DACPWM_CTRL_PWM_MODE_CKE_DIS |
				   DSM_DACPWM_CTRL_PWM_DIS);
		regmap_update_bits(rd->regmap, I2S_XFER,
				   DSM_I2S_XFER_RXS_MASK,
				   DSM_I2S_XFER_RXS_STOP);
		regmap_update_bits(rd->regmap, I2S_CLR,
				   DSM_I2S_CLR_RXC_MASK,
				   DSM_I2S_CLR_RXC_CLR);
		regmap_update_bits(rd->regmap, DACDIGEN,
				   DSM_DACDIGEN_DAC_GLBEN_MASK |
				   DSM_DACDIGEN_DACEN_L0R1_MASK,
				   DSM_DACDIGEN_DAC_GLBEN_DIS |
				   DSM_DACDIGEN_DACEN_L0R1_DIS);
	}

	rk_dsm_reset(rd);
}

static const struct snd_soc_dai_ops rd_dai_ops = {
	.hw_params = rk_dsm_hw_params,
	.set_fmt = rk_dsm_set_dai_fmt,
	.startup = rk_dsm_pcm_startup,
	.shutdown = rk_dsm_pcm_shutdown,
};

static struct snd_soc_dai_driver rd_dai[] = {
	{
		.name = "rk_dsm",
		.id = 0,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &rd_dai_ops,
	},
};

static const struct snd_soc_component_driver soc_codec_dev_rd = {
	.controls = rk_dsm_snd_controls,
	.num_controls = ARRAY_SIZE(rk_dsm_snd_controls),
};

static const struct regmap_config rd_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = VERSION,
	.cache_type = REGCACHE_FLAT,
};

static int rk3562_soc_init(struct device *dev)
{
	struct rk_dsm_priv *rd = dev_get_drvdata(dev);

	if (IS_ERR(rd->grf))
		return PTR_ERR(rd->grf);

	/* enable internal codec to i2s1 */
	return regmap_write(rd->grf, RK3562_GRF_PERI_AUDIO_CON,
			    (BIT(14) << 16 | BIT(14) | 0x0a100a10));
}

static void rk3562_soc_deinit(struct device *dev)
{
	struct rk_dsm_priv *rd = dev_get_drvdata(dev);

	if (IS_ERR(rd->grf))
		return;

	regmap_write(rd->grf, RK3562_GRF_PERI_AUDIO_CON, (BIT(14) << 16) | 0x0a100a10);
}

static const struct rk_dsm_soc_data rk3562_data = {
	.init = rk3562_soc_init,
	.deinit = rk3562_soc_deinit,
};

#ifdef CONFIG_OF
static const struct of_device_id rd_of_match[] = {
	{ .compatible = "rockchip,rk3562-dsm", .data = &rk3562_data },
	{},
};
MODULE_DEVICE_TABLE(of, rd_of_match);
#endif

#ifdef CONFIG_PM
static int rk_dsm_runtime_resume(struct device *dev)
{
	struct rk_dsm_priv *rd = dev_get_drvdata(dev);
	int ret = 0;

	ret = clk_prepare_enable(rd->pclk);
	if (ret)
		return ret;

	regcache_cache_only(rd->regmap, false);
	regcache_mark_dirty(rd->regmap);

	ret = clk_prepare_enable(rd->clk_dac);
	if (ret)
		goto err;

	return 0;
err:
	clk_disable_unprepare(rd->pclk);

	return ret;
}

static int rk_dsm_runtime_suspend(struct device *dev)
{
	struct rk_dsm_priv *rd = dev_get_drvdata(dev);

	regcache_cache_only(rd->regmap, true);
	clk_disable_unprepare(rd->clk_dac);
	clk_disable_unprepare(rd->pclk);

	return 0;
}
#endif

static int rk_dsm_platform_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rk_dsm_priv *rd;
	void __iomem *base;
	int ret = 0;

	rd = devm_kzalloc(&pdev->dev, sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return -ENOMEM;

	rd->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(rd->grf))
		return PTR_ERR(rd->grf);

	if (device_property_read_u32(&pdev->dev, "rockchip,pa-ctl-delay-ms",
				     &rd->pa_ctl_delay_ms))
		rd->pa_ctl_delay_ms = 0;

	rd->rc = devm_reset_control_get(&pdev->dev, "reset");

	rd->clk_dac = devm_clk_get(&pdev->dev, "dac");
	if (IS_ERR(rd->clk_dac))
		return PTR_ERR(rd->clk_dac);

	rd->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(rd->pclk))
		return PTR_ERR(rd->pclk);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	rd->regmap =
		devm_regmap_init_mmio(&pdev->dev, base, &rd_regmap_config);
	if (IS_ERR(rd->regmap))
		return PTR_ERR(rd->regmap);

	platform_set_drvdata(pdev, rd);

	rd->data = device_get_match_data(&pdev->dev);
	if (rd->data && rd->data->init) {
		ret = rd->data->init(&pdev->dev);
		if (ret)
			return ret;
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = rk_dsm_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	regmap_update_bits(rd->regmap, DACPWM_CTRL,
			   DSM_DACPWM_CTRL_PWM_MODE_MASK,
			   DSM_DACPWM_CTRL_PWM_MODE_0);

	rd->pa_ctl = devm_gpiod_get_optional(&pdev->dev, "pa-ctl",
					     GPIOD_OUT_LOW);

	if (!rd->pa_ctl) {
		dev_info(&pdev->dev, "no need pa-ctl gpio\n");
	} else if (IS_ERR(rd->pa_ctl)) {
		ret = PTR_ERR(rd->pa_ctl);
		dev_err(&pdev->dev, "fail to request gpio pa-ctl\n");
		goto err_suspend;
	}

	ret = devm_snd_soc_register_component(&pdev->dev, &soc_codec_dev_rd,
					      rd_dai, ARRAY_SIZE(rd_dai));

	if (ret)
		goto err_suspend;

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		rk_dsm_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	if (rd->data && rd->data->deinit)
		rd->data->deinit(&pdev->dev);

	return ret;
}

static int rk_dsm_platform_remove(struct platform_device *pdev)
{
	struct rk_dsm_priv *rd = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		rk_dsm_runtime_suspend(&pdev->dev);

	if (rd->data && rd->data->deinit)
		rd->data->deinit(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops rd_pm = {
	SET_RUNTIME_PM_OPS(rk_dsm_runtime_suspend,
			   rk_dsm_runtime_resume, NULL)
};

static struct platform_driver rk_dsm_driver = {
	.driver = {
		.name = "rk_dsm",
		.of_match_table = of_match_ptr(rd_of_match),
		.pm = &rd_pm,
	},
	.probe = rk_dsm_platform_probe,
	.remove = rk_dsm_platform_remove,
};
module_platform_driver(rk_dsm_driver);

MODULE_AUTHOR("Jason Zhu <jason.zhu@rock-chips.com>");
MODULE_DESCRIPTION("ASoC Rockchip Delta-sigma Digital Converter Driver");
MODULE_LICENSE("GPL");
