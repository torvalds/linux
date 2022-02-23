// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Audio Codec Digital Interface
 *
 * Copyright (C) 2020 Rockchip Electronics Co.,Ltd
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
#include "rk_codec_digital.h"

#define RK3568_GRF_SOC_CON2 (0x0508)
#define RK3588_GRF_SOC_CON6 (0x0318)
#define RV1106_GRF_SOC_CON1 (0x0004)
#define RV1126_GRF_SOC_CON2 (0x0008)

struct rk_codec_digital_soc_data {
	int (*init)(struct device *dev);
	void (*deinit)(struct device *dev);
};

struct rk_codec_digital_priv {
	struct regmap *grf;
	struct regmap *regmap;
	struct clk *clk_adc;
	struct clk *clk_dac;
	struct clk *clk_i2c;
	struct clk *pclk;
	bool pwmout;
	bool sync;
	unsigned int pa_ctl_delay_ms;
	struct gpio_desc *pa_ctl;
	struct reset_control *rc;
	const struct rk_codec_digital_soc_data *data;
};

/* ADC digital gain */
static const DECLARE_TLV_DB_SCALE(adc_tlv, -95625, 375, 0);
/* PGA gain */
static const DECLARE_TLV_DB_SCALE(pga_tlv, -18, 3, 0);
/* DAC digital gain */
static const DECLARE_TLV_DB_SCALE(dac_tlv, -95625, 375, 0);

/* ADC Cutoff Freq for High Pass Filter */
static const char * const adc_hpf_cutoff_text[] = {
	"3.79Hz", "60Hz", "243Hz", "493Hz",
};

static SOC_ENUM_SINGLE_DECL(adc_hpf_cutoff_enum, ADCHPFCF, 0,
			    adc_hpf_cutoff_text);

/* DAC Cutoff for High Pass Filter */
static const char * const dac_hpf_cutoff_text[] = {
	"80Hz", "100Hz", "120Hz", "140Hz",
};

static SOC_ENUM_SINGLE_DECL(dac_hpf_cutoff_enum, DACHPF, 4,
			    dac_hpf_cutoff_text);

static const char * const pa_ctl[] = {"Off", "On"};

static const struct soc_enum pa_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pa_ctl), pa_ctl);

static int rk_codec_digital_adc_vol_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int val = snd_soc_component_read(component, mc->reg);
	unsigned int sign = snd_soc_component_read(component, ADCVOGP);
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int shift = mc->shift;
	int mid = mc->max / 2;
	int uv;

	switch (mc->reg) {
	case ADCVOLL0:
		sign &= ACDCDIG_ADCVOGP_VOLGPL0_MASK;
		break;
	case ADCVOLL1:
		sign &= ACDCDIG_ADCVOGP_VOLGPL1_MASK;
		break;
	case ADCVOLR0:
		sign &= ACDCDIG_ADCVOGP_VOLGPR0_MASK;
		break;
	default:
		return -EINVAL;
	}

	uv = (val >> shift) & mask;
	if (sign)
		uv = mid + uv;
	else
		uv = mid - uv;

	ucontrol->value.integer.value[0] = uv;

	return 0;
}

static int rk_codec_digital_adc_vol_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int val, val_mask, sign, sign_mask;
	int uv = ucontrol->value.integer.value[0];
	int min = mc->min;
	int mid = mc->max / 2;
	bool pos = (uv > mid);

	switch (mc->reg) {
	case ADCVOLL0:
		sign_mask = ACDCDIG_ADCVOGP_VOLGPL0_MASK;
		sign = pos ? ACDCDIG_ADCVOGP_VOLGPL0_POS : ACDCDIG_ADCVOGP_VOLGPL0_NEG;
		break;
	case ADCVOLL1:
		sign_mask = ACDCDIG_ADCVOGP_VOLGPL1_MASK;
		sign = pos ? ACDCDIG_ADCVOGP_VOLGPL1_POS : ACDCDIG_ADCVOGP_VOLGPL1_NEG;
		break;
	case ADCVOLR0:
		sign_mask = ACDCDIG_ADCVOGP_VOLGPR0_MASK;
		sign = pos ? ACDCDIG_ADCVOGP_VOLGPR0_POS : ACDCDIG_ADCVOGP_VOLGPR0_NEG;
		break;
	default:
		return -EINVAL;
	}

	uv = pos ? (uv - mid) : (mid - uv);

	val = ((uv + min) & mask);
	val_mask = mask << shift;
	val = val << shift;

	snd_soc_component_update_bits(component, reg, val_mask, val);
	snd_soc_component_update_bits(component, ADCVOGP, sign_mask, sign);

	return 0;
}

static int rk_codec_digital_dac_vol_get(struct snd_kcontrol *kcontrol,
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

static int rk_codec_digital_dac_vol_put(struct snd_kcontrol *kcontrol,
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
		sign = ACDCDIG_DACVOGP_VOLGPL0_POS | ACDCDIG_DACVOGP_VOLGPR0_POS;
		uv = uv - mid;
	} else {
		sign = ACDCDIG_DACVOGP_VOLGPL0_NEG | ACDCDIG_DACVOGP_VOLGPR0_NEG;
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

static int rk_codec_digital_dac_pa_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rk_codec_digital_priv *rcd = snd_soc_component_get_drvdata(component);

	if (!rcd->pa_ctl)
		return -EINVAL;

	ucontrol->value.enumerated.item[0] = gpiod_get_value(rcd->pa_ctl);

	return 0;
}

static int rk_codec_digital_dac_pa_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct rk_codec_digital_priv *rcd = snd_soc_component_get_drvdata(component);

	if (!rcd->pa_ctl)
		return -EINVAL;

	gpiod_set_value(rcd->pa_ctl, ucontrol->value.enumerated.item[0]);

	return 0;
}

static const struct snd_kcontrol_new rk_codec_digital_snd_controls[] = {
	SOC_SINGLE_EXT_TLV("ADCL0 Digital Volume",
			   ADCVOLL0, 0, 0X1fe, 0,
			   rk_codec_digital_adc_vol_get,
			   rk_codec_digital_adc_vol_put,
			   adc_tlv),
	SOC_SINGLE_EXT_TLV("ADCL1 Digital Volume",
			   ADCVOLL1, 0, 0x1fe, 0,
			   rk_codec_digital_adc_vol_get,
			   rk_codec_digital_adc_vol_put,
			   adc_tlv),
	SOC_SINGLE_EXT_TLV("ADCR0 Digital Volume",
			   ADCVOLR0, 0, 0x1fe, 0,
			   rk_codec_digital_adc_vol_get,
			   rk_codec_digital_adc_vol_put,
			   adc_tlv),

	SOC_SINGLE_TLV("ADCL0 PGA Gain",
		       ADCPGL0, 0, 0Xf, 0, pga_tlv),
	SOC_SINGLE_TLV("ADCL1 PGA Gain",
		       ADCPGL1, 0, 0xf, 0, pga_tlv),
	SOC_SINGLE_TLV("ADCR0 PGA Gain",
		       ADCPGR0, 0, 0xf, 0, pga_tlv),

	SOC_DOUBLE_R_EXT_TLV("DAC Digital Volume",
			     DACVOLL0, DACVOLR0, 0, 0x1fe, 0,
			     rk_codec_digital_dac_vol_get,
			     rk_codec_digital_dac_vol_put,
			     dac_tlv),

	SOC_ENUM("ADC HPF Cutoff", adc_hpf_cutoff_enum),
	SOC_SINGLE("ADC L0 HPF Switch", ADCHPFEN, 0, 1, 0),
	SOC_SINGLE("ADC R0 HPF Switch", ADCHPFEN, 1, 1, 0),
	SOC_SINGLE("ADC L1 HPF Switch", ADCHPFEN, 2, 1, 0),

	SOC_ENUM("DAC HPF Cutoff", dac_hpf_cutoff_enum),
	SOC_SINGLE("DAC HPF Switch", DACHPF, 0, 1, 0),
	SOC_ENUM_EXT("Power Amplifier", pa_enum,
		     rk_codec_digital_dac_pa_get,
		     rk_codec_digital_dac_pa_put),
};

static void rk_codec_digital_reset(struct rk_codec_digital_priv *rcd)
{
	if (IS_ERR(rcd->rc))
		return;

	reset_control_assert(rcd->rc);
	udelay(1);
	reset_control_deassert(rcd->rc);
}

/*
 * ACDC_CLK  D2A_CLK   D2A_SYNC Sample rates supported
 * 49.152MHz 49.152MHz 6.144MHz 12/24/48/96/192kHz
 * 45.154MHz 45.154MHz 5.644MHz 11.025/22.05/44.1/88.2/176.4kHz
 * 32.768MHz 32.768MHz 4.096MHz 8/16/32/64/128kHz
 *
 */
static void rk_codec_digital_get_clk(unsigned int samplerate,
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

static void rk_codec_digital_enable_clk_adc(struct rk_codec_digital_priv *rcd)
{
	regmap_update_bits(rcd->regmap, ADCCLKCTRL,
			   ACDCDIG_ADCCLKCTRL_CIC_DS_RATIO_MASK |
			   ACDCDIG_ADCCLKCTRL_ADC_CKE_MASK |
			   ACDCDIG_ADCCLKCTRL_I2STX_CKE_MASK |
			   ACDCDIG_ADCCLKCTRL_CKE_BCLKTX_MASK |
			   ACDCDIG_ADCCLKCTRL_FILTER_GATE_EN_MASK |
			   ACDCDIG_ADCCLKCTRL_ADC_SYNC_ENA_MASK,
			   ACDCDIG_ADCCLKCTRL_CIC_DS_RATIO_16 |
			   ACDCDIG_ADCCLKCTRL_ADC_CKE_EN |
			   ACDCDIG_ADCCLKCTRL_I2STX_CKE_EN |
			   ACDCDIG_ADCCLKCTRL_CKE_BCLKTX_EN |
			   ACDCDIG_ADCCLKCTRL_FILTER_GATE_EN |
			   ACDCDIG_ADCCLKCTRL_ADC_SYNC_ENA_EN);
}

static void rk_codec_digital_enable_clk_dac(struct rk_codec_digital_priv *rcd)
{
	regmap_update_bits(rcd->regmap, DACCLKCTRL,
			   ACDCDIG_DACCLKCTRL_DAC_CKE_MASK |
			   ACDCDIG_DACCLKCTRL_I2SRX_CKE_MASK |
			   ACDCDIG_DACCLKCTRL_CKE_BCLKRX_MASK |
			   ACDCDIG_DACCLKCTRL_DAC_SYNC_ENA_MASK |
			   ACDCDIG_DACCLKCTRL_DAC_MODE_ATTENU_MASK,
			   ACDCDIG_DACCLKCTRL_DAC_CKE_EN |
			   ACDCDIG_DACCLKCTRL_I2SRX_CKE_EN |
			   ACDCDIG_DACCLKCTRL_CKE_BCLKRX_EN |
			   ACDCDIG_DACCLKCTRL_DAC_SYNC_ENA_EN |
			   ACDCDIG_DACCLKCTRL_DAC_MODE_ATTENU_EN);
}

static int rk_codec_digital_set_clk_sync(struct rk_codec_digital_priv *rcd,
					 unsigned int mclk,
					 unsigned int sclk,
					 unsigned int bclk)
{
	unsigned int div_sync, div_bclk;

	div_bclk = DIV_ROUND_CLOSEST(mclk, bclk);
	div_sync = DIV_ROUND_CLOSEST(mclk, sclk);

	clk_set_rate(rcd->clk_adc, mclk);
	clk_set_rate(rcd->clk_dac, mclk);

	/* select clock sync is from ADC. */
	regmap_update_bits(rcd->regmap, SYSCTRL0,
			   ACDCDIG_SYSCTRL0_SYNC_MODE_MASK |
			   ACDCDIG_SYSCTRL0_CLK_COM_SEL_MASK,
			   ACDCDIG_SYSCTRL0_SYNC_MODE_SYNC |
			   ACDCDIG_SYSCTRL0_CLK_COM_SEL_ADC);

	regmap_update_bits(rcd->regmap, ADCINT_DIV,
			   ACDCDIG_ADCINT_DIV_INT_DIV_CON_MASK,
			   ACDCDIG_ADCINT_DIV_INT_DIV_CON(div_sync));
	regmap_update_bits(rcd->regmap, DACINT_DIV,
			   ACDCDIG_DACINT_DIV_INT_DIV_CON_MASK,
			   ACDCDIG_DACINT_DIV_INT_DIV_CON(div_sync));

	rk_codec_digital_enable_clk_adc(rcd);
	rk_codec_digital_enable_clk_dac(rcd);

	regmap_update_bits(rcd->regmap, DACSCLKRXINT_DIV,
			   ACDCDIG_DACSCLKRXINT_DIV_SCKRXDIV_MASK,
			   ACDCDIG_DACSCLKRXINT_DIV_SCKRXDIV(div_bclk));
	regmap_update_bits(rcd->regmap, I2S_CKR0,
			   ACDCDIG_I2S_CKR0_RSD_MASK,
			   ACDCDIG_I2S_CKR0_RSD(64));
	regmap_update_bits(rcd->regmap, ADCSCLKTXINT_DIV,
			   ACDCDIG_ADCSCLKTXINT_DIV_SCKTXDIV_MASK,
			   ACDCDIG_ADCSCLKTXINT_DIV_SCKTXDIV(div_bclk));
	regmap_update_bits(rcd->regmap, I2S_CKR0,
			   ACDCDIG_I2S_CKR0_TSD_MASK,
			   ACDCDIG_I2S_CKR0_TSD(64));

	return 0;
}

static int rk_codec_digital_set_clk(struct rk_codec_digital_priv *rcd,
				    struct snd_pcm_substream *substream,
				    unsigned int samplerate)
{
	unsigned int mclk, sclk, bclk;
	unsigned int div_sync, div_bclk;

	rk_codec_digital_get_clk(samplerate, &mclk, &sclk);
	if (!mclk || !sclk)
		return -EINVAL;

	bclk = 64 * samplerate;
	div_bclk = DIV_ROUND_CLOSEST(mclk, bclk);
	div_sync = DIV_ROUND_CLOSEST(mclk, sclk);

	if (rcd->sync)
		return rk_codec_digital_set_clk_sync(rcd, mclk, sclk, bclk);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		clk_set_rate(rcd->clk_dac, mclk);

		regmap_update_bits(rcd->regmap, DACINT_DIV,
				   ACDCDIG_DACINT_DIV_INT_DIV_CON_MASK,
				   ACDCDIG_DACINT_DIV_INT_DIV_CON(div_sync));

		rk_codec_digital_enable_clk_dac(rcd);

		regmap_update_bits(rcd->regmap, DACSCLKRXINT_DIV,
				   ACDCDIG_DACSCLKRXINT_DIV_SCKRXDIV_MASK,
				   ACDCDIG_DACSCLKRXINT_DIV_SCKRXDIV(div_bclk));
		regmap_update_bits(rcd->regmap, I2S_CKR0,
				   ACDCDIG_I2S_CKR0_RSD_MASK,
				   ACDCDIG_I2S_CKR0_RSD(64));
	} else {
		clk_set_rate(rcd->clk_adc, mclk);

		regmap_update_bits(rcd->regmap, ADCINT_DIV,
				   ACDCDIG_ADCINT_DIV_INT_DIV_CON_MASK,
				   ACDCDIG_ADCINT_DIV_INT_DIV_CON(div_sync));

		rk_codec_digital_enable_clk_adc(rcd);
		regmap_update_bits(rcd->regmap, ADCSCLKTXINT_DIV,
				   ACDCDIG_ADCSCLKTXINT_DIV_SCKTXDIV_MASK,
				   ACDCDIG_ADCSCLKTXINT_DIV_SCKTXDIV(div_bclk));
		regmap_update_bits(rcd->regmap, I2S_CKR0,
				   ACDCDIG_I2S_CKR0_TSD_MASK,
				   ACDCDIG_I2S_CKR0_TSD(64));
	}

	return 0;
}

static int rk_codec_digital_set_dai_fmt(struct snd_soc_dai *dai,
					unsigned int fmt)
{
	struct rk_codec_digital_priv *rcd =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int mask = 0, val = 0;

	/* master mode only */
	regmap_update_bits(rcd->regmap, I2S_CKR1,
			   ACDCDIG_I2S_CKR1_MSS_MASK,
			   ACDCDIG_I2S_CKR1_MSS_MASTER);

	mask = ACDCDIG_I2S_CKR1_CKP_MASK |
	       ACDCDIG_I2S_CKR1_RLP_MASK |
	       ACDCDIG_I2S_CKR1_TLP_MASK;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		val = ACDCDIG_I2S_CKR1_CKP_NORMAL |
		      ACDCDIG_I2S_CKR1_RLP_NORMAL |
		      ACDCDIG_I2S_CKR1_TLP_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		val = ACDCDIG_I2S_CKR1_CKP_INVERTED |
		      ACDCDIG_I2S_CKR1_RLP_INVERTED |
		      ACDCDIG_I2S_CKR1_TLP_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val = ACDCDIG_I2S_CKR1_CKP_INVERTED |
		      ACDCDIG_I2S_CKR1_RLP_NORMAL |
		      ACDCDIG_I2S_CKR1_TLP_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		val = ACDCDIG_I2S_CKR1_CKP_NORMAL |
		      ACDCDIG_I2S_CKR1_RLP_INVERTED |
		      ACDCDIG_I2S_CKR1_TLP_INVERTED;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(rcd->regmap, I2S_CKR1, mask, val);

	return 0;
}

static int rk_codec_digital_enable_sync(struct rk_codec_digital_priv *rcd)
{
	regmap_update_bits(rcd->regmap, I2S_XFER,
			   ACDCDIG_I2S_XFER_RXS_MASK |
			   ACDCDIG_I2S_XFER_TXS_MASK,
			   ACDCDIG_I2S_XFER_RXS_START |
			   ACDCDIG_I2S_XFER_TXS_START);

	regmap_update_bits(rcd->regmap, SYSCTRL0,
			   ACDCDIG_SYSCTRL0_GLB_CKE_MASK,
			   ACDCDIG_SYSCTRL0_GLB_CKE_EN);

	regmap_update_bits(rcd->regmap, ADCDIGEN,
			   ACDCDIG_ADCDIGEN_ADC_GLBEN_MASK |
			   ACDCDIG_ADCDIGEN_ADCEN_L2_MASK |
			   ACDCDIG_ADCDIGEN_ADCEN_L0R1_MASK,
			   ACDCDIG_ADCDIGEN_ADC_GLBEN_EN |
			   ACDCDIG_ADCDIGEN_ADCEN_L2_EN |
			   ACDCDIG_ADCDIGEN_ADCEN_L0R1_EN);

	regmap_update_bits(rcd->regmap, DACDIGEN,
			   ACDCDIG_DACDIGEN_DAC_GLBEN_MASK |
			   ACDCDIG_DACDIGEN_DACEN_L0R1_MASK,
			   ACDCDIG_DACDIGEN_DAC_GLBEN_EN |
			   ACDCDIG_DACDIGEN_DACEN_L0R1_EN);

	return 0;
}

static int rk_codec_digital_disable_sync(struct rk_codec_digital_priv *rcd)
{
	regmap_update_bits(rcd->regmap, I2S_XFER,
			   ACDCDIG_I2S_XFER_RXS_MASK |
			   ACDCDIG_I2S_XFER_TXS_MASK,
			   ACDCDIG_I2S_XFER_RXS_STOP |
			   ACDCDIG_I2S_XFER_TXS_STOP);

	regmap_update_bits(rcd->regmap, I2S_CLR,
			   ACDCDIG_I2S_CLR_RXC_MASK |
			   ACDCDIG_I2S_CLR_TXC_MASK,
			   ACDCDIG_I2S_CLR_RXC_CLR |
			   ACDCDIG_I2S_CLR_TXC_CLR);

	regmap_update_bits(rcd->regmap, SYSCTRL0,
			   ACDCDIG_SYSCTRL0_GLB_CKE_MASK,
			   ACDCDIG_SYSCTRL0_GLB_CKE_DIS);

	regmap_update_bits(rcd->regmap, ADCDIGEN,
			   ACDCDIG_ADCDIGEN_ADC_GLBEN_MASK |
			   ACDCDIG_ADCDIGEN_ADCEN_L2_MASK |
			   ACDCDIG_ADCDIGEN_ADCEN_L0R1_MASK,
			   ACDCDIG_ADCDIGEN_ADC_GLBEN_DIS |
			   ACDCDIG_ADCDIGEN_ADCEN_L2_DIS |
			   ACDCDIG_ADCDIGEN_ADCEN_L0R1_DIS);

	regmap_update_bits(rcd->regmap, DACDIGEN,
			   ACDCDIG_DACDIGEN_DAC_GLBEN_MASK |
			   ACDCDIG_DACDIGEN_DACEN_L0R1_MASK,
			   ACDCDIG_DACDIGEN_DAC_GLBEN_DIS |
			   ACDCDIG_DACDIGEN_DACEN_L0R1_DIS);

	return 0;
}

static int rk_codec_digital_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct rk_codec_digital_priv *rcd =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int srt = 0, val = 0;

	rk_codec_digital_set_clk(rcd, substream, params_rate(params));

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

		regmap_update_bits(rcd->regmap, DACCFG1,
				   ACDCDIG_DACCFG1_DACSRT_MASK,
				   ACDCDIG_DACCFG1_DACSRT(srt));

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

		regmap_update_bits(rcd->regmap, I2S_RXCR0,
				   ACDCDIG_I2S_RXCR0_VDW_MASK,
				   ACDCDIG_I2S_RXCR0_VDW(val));
		if (rcd->pwmout)
			regmap_update_bits(rcd->regmap, DACPWM_CTRL,
					   ACDCDIG_DACPWM_CTRL_PWM_MODE_CKE_MASK |
					   ACDCDIG_DACPWM_CTRL_PWM_EN_MASK,
					   ACDCDIG_DACPWM_CTRL_PWM_MODE_CKE_EN |
					   ACDCDIG_DACPWM_CTRL_PWM_EN);
	} else {
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
			srt = 2;
			break;
		case 44100:
		case 48000:
			srt = 3;
			break;
		case 64000:
		case 88200:
		case 96000:
			srt = 4;
			break;
		case 128000:
		case 176400:
		case 192000:
			srt = 5;
			break;
		default:
			return -EINVAL;
		}

		regmap_update_bits(rcd->regmap, ADCCFG1,
				   ACDCDIG_ADCCFG1_ADCSRT_MASK,
				   ACDCDIG_ADCCFG1_ADCSRT(srt));

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

		regmap_update_bits(rcd->regmap, I2S_TXCR0,
				   ACDCDIG_I2S_TXCR0_VDW_MASK,
				   ACDCDIG_I2S_TXCR0_VDW(val));

		switch (params_channels(params)) {
		case 4:
			val = ACDCDIG_I2S_TXCR1_TCSR_4CH;
			break;
		case 2:
			val = ACDCDIG_I2S_TXCR1_TCSR_2CH;
			break;
		default:
			return -EINVAL;
		}

		regmap_update_bits(rcd->regmap, I2S_TXCR1,
				   ACDCDIG_I2S_TXCR1_TCSR_MASK, val);
	}

	if (rcd->sync)
		return rk_codec_digital_enable_sync(rcd);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(rcd->regmap, I2S_XFER,
				   ACDCDIG_I2S_XFER_RXS_MASK,
				   ACDCDIG_I2S_XFER_RXS_START);
		regmap_update_bits(rcd->regmap, DACDIGEN,
				   ACDCDIG_DACDIGEN_DAC_GLBEN_MASK |
				   ACDCDIG_DACDIGEN_DACEN_L0R1_MASK,
				   ACDCDIG_DACDIGEN_DAC_GLBEN_EN |
				   ACDCDIG_DACDIGEN_DACEN_L0R1_EN);
	} else {
		regmap_update_bits(rcd->regmap, I2S_XFER,
				   ACDCDIG_I2S_XFER_TXS_MASK,
				   ACDCDIG_I2S_XFER_TXS_START);
		regmap_update_bits(rcd->regmap, ADCDIGEN,
				   ACDCDIG_ADCDIGEN_ADC_GLBEN_MASK |
				   ACDCDIG_ADCDIGEN_ADCEN_L2_MASK |
				   ACDCDIG_ADCDIGEN_ADCEN_L0R1_MASK,
				   ACDCDIG_ADCDIGEN_ADC_GLBEN_EN |
				   ACDCDIG_ADCDIGEN_ADCEN_L2_EN |
				   ACDCDIG_ADCDIGEN_ADCEN_L0R1_EN);
	}

	return 0;
}

static int rk_codec_digital_pcm_startup(struct snd_pcm_substream *substream,
					struct snd_soc_dai *dai)
{
	struct rk_codec_digital_priv *rcd =
		snd_soc_component_get_drvdata(dai->component);

	if (rcd->pa_ctl) {
		gpiod_set_value_cansleep(rcd->pa_ctl, 1);
		if (rcd->pa_ctl_delay_ms)
			msleep(rcd->pa_ctl_delay_ms);
	}

	return 0;
}

static void rk_codec_digital_pcm_shutdown(struct snd_pcm_substream *substream,
					  struct snd_soc_dai *dai)
{
	struct rk_codec_digital_priv *rcd =
		snd_soc_component_get_drvdata(dai->component);

	if (rcd->pa_ctl)
		gpiod_set_value_cansleep(rcd->pa_ctl, 0);

	if (rcd->sync) {
		if (!snd_soc_component_active(dai->component)) {
			rk_codec_digital_disable_sync(rcd);
			rk_codec_digital_reset(rcd);
		}

		return;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (rcd->pwmout)
			regmap_update_bits(rcd->regmap, DACPWM_CTRL,
					   ACDCDIG_DACPWM_CTRL_PWM_MODE_CKE_MASK |
					   ACDCDIG_DACPWM_CTRL_PWM_EN_MASK,
					   ACDCDIG_DACPWM_CTRL_PWM_MODE_CKE_DIS |
					   ACDCDIG_DACPWM_CTRL_PWM_DIS);
		regmap_update_bits(rcd->regmap, I2S_XFER,
				   ACDCDIG_I2S_XFER_RXS_MASK,
				   ACDCDIG_I2S_XFER_RXS_STOP);
		regmap_update_bits(rcd->regmap, I2S_CLR,
				   ACDCDIG_I2S_CLR_RXC_MASK,
				   ACDCDIG_I2S_CLR_RXC_CLR);
		regmap_update_bits(rcd->regmap, DACDIGEN,
				   ACDCDIG_DACDIGEN_DAC_GLBEN_MASK |
				   ACDCDIG_DACDIGEN_DACEN_L0R1_MASK,
				   ACDCDIG_DACDIGEN_DAC_GLBEN_DIS |
				   ACDCDIG_DACDIGEN_DACEN_L0R1_DIS);
	} else {
		regmap_update_bits(rcd->regmap, I2S_XFER,
				   ACDCDIG_I2S_XFER_TXS_MASK,
				   ACDCDIG_I2S_XFER_TXS_STOP);
		regmap_update_bits(rcd->regmap, I2S_CLR,
				   ACDCDIG_I2S_CLR_TXC_MASK,
				   ACDCDIG_I2S_CLR_TXC_CLR);

		regmap_update_bits(rcd->regmap, ADCDIGEN,
				   ACDCDIG_ADCDIGEN_ADC_GLBEN_MASK |
				   ACDCDIG_ADCDIGEN_ADCEN_L2_MASK |
				   ACDCDIG_ADCDIGEN_ADCEN_L0R1_MASK,
				   ACDCDIG_ADCDIGEN_ADC_GLBEN_DIS |
				   ACDCDIG_ADCDIGEN_ADCEN_L2_DIS |
				   ACDCDIG_ADCDIGEN_ADCEN_L0R1_DIS);
	}
}

static const struct snd_soc_dai_ops rcd_dai_ops = {
	.hw_params = rk_codec_digital_hw_params,
	.set_fmt = rk_codec_digital_set_dai_fmt,
	.startup = rk_codec_digital_pcm_startup,
	.shutdown = rk_codec_digital_pcm_shutdown,
};

static struct snd_soc_dai_driver rcd_dai[] = {
	{
		.name = "rk_codec_digital",
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
		.capture = {
			.stream_name = "Capture",
			.channels_min = 2,
			.channels_max = 4,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &rcd_dai_ops,
	},
};

static const struct snd_soc_component_driver soc_codec_dev_rcd = {
	.controls = rk_codec_digital_snd_controls,
	.num_controls = ARRAY_SIZE(rk_codec_digital_snd_controls),
};

static const struct regmap_config rcd_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = VERSION,
	.cache_type = REGCACHE_FLAT,
};

static int rk3568_soc_init(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);

	if (IS_ERR(rcd->grf))
		return PTR_ERR(rcd->grf);

	/* enable internal codec to i2s3 */
	return regmap_write(rcd->grf, RK3568_GRF_SOC_CON2,
			    (BIT(13) << 16 | BIT(13)));
}

static void rk3568_soc_deinit(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);

	if (IS_ERR(rcd->grf))
		return;

	regmap_write(rcd->grf, RK3568_GRF_SOC_CON2, (BIT(13) << 16));
}

static const struct rk_codec_digital_soc_data rk3568_data = {
	.init = rk3568_soc_init,
	.deinit = rk3568_soc_deinit,
};

static int rk3588_soc_init(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);

	if (IS_ERR(rcd->grf))
		return PTR_ERR(rcd->grf);

	/* enable internal codec to i2s3 */
	return regmap_write(rcd->grf, RK3588_GRF_SOC_CON6,
			    (BIT(11) << 16 | BIT(11)));
}

static void rk3588_soc_deinit(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);

	if (IS_ERR(rcd->grf))
		return;

	regmap_write(rcd->grf, RK3588_GRF_SOC_CON6, (BIT(11) << 16));
}

static const struct rk_codec_digital_soc_data rk3588_data = {
	.init = rk3588_soc_init,
	.deinit = rk3588_soc_deinit,
};

static int rv1106_soc_init(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);

	if (IS_ERR(rcd->grf))
		return PTR_ERR(rcd->grf);

	/* enable internal codec to i2s0 */
	return regmap_write(rcd->grf, RV1106_GRF_SOC_CON1,
			    (BIT(8) << 16 | BIT(8)));
}

static void rv1106_soc_deinit(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);

	if (IS_ERR(rcd->grf))
		return;

	regmap_write(rcd->grf, RV1106_GRF_SOC_CON1, (BIT(8) << 16));
}

static const struct rk_codec_digital_soc_data rv1106_data = {
	.init = rv1106_soc_init,
	.deinit = rv1106_soc_deinit,
};

static int rv1126_soc_init(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);

	if (IS_ERR(rcd->grf))
		return PTR_ERR(rcd->grf);

	/* enable internal codec to i2s0 */
	return regmap_write(rcd->grf, RV1126_GRF_SOC_CON2,
			    (BIT(13) << 16 | BIT(13)));
}

static void rv1126_soc_deinit(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);

	if (IS_ERR(rcd->grf))
		return;

	regmap_write(rcd->grf, RV1126_GRF_SOC_CON2, (BIT(13) << 16));
}

static const struct rk_codec_digital_soc_data rv1126_data = {
	.init = rv1126_soc_init,
	.deinit = rv1126_soc_deinit,
};

#ifdef CONFIG_OF
static const struct of_device_id rcd_of_match[] = {
	{ .compatible = "rockchip,codec-digital-v1", },
#ifdef CONFIG_CPU_RK3568
	{ .compatible = "rockchip,rk3568-codec-digital", .data = &rk3568_data },
#endif
#ifdef CONFIG_CPU_RK3588
	{ .compatible = "rockchip,rk3588-codec-digital", .data = &rk3588_data },
#endif
#ifdef CONFIG_CPU_RV1106
	{ .compatible = "rockchip,rv1106-codec-digital", .data = &rv1106_data },
#endif
#ifdef CONFIG_CPU_RV1126
	{ .compatible = "rockchip,rv1126-codec-digital", .data = &rv1126_data },
#endif
	{},
};
MODULE_DEVICE_TABLE(of, rcd_of_match);
#endif

#ifdef CONFIG_PM
static int rk_codec_digital_runtime_resume(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);
	int ret = 0;

	ret = clk_prepare_enable(rcd->pclk);
	if (ret)
		return ret;

	regcache_cache_only(rcd->regmap, false);
	regcache_mark_dirty(rcd->regmap);

	ret = regcache_sync(rcd->regmap);
	if (ret)
		goto err;

	ret = clk_prepare_enable(rcd->clk_adc);
	if (ret)
		goto err;

	ret = clk_prepare_enable(rcd->clk_dac);
	if (ret)
		goto err_adc;

	ret = clk_prepare_enable(rcd->clk_i2c);
	if (ret)
		goto err_dac;

	return 0;

err_dac:
	clk_disable_unprepare(rcd->clk_dac);
err_adc:
	clk_disable_unprepare(rcd->clk_adc);
err:
	clk_disable_unprepare(rcd->pclk);

	return ret;
}

static int rk_codec_digital_runtime_suspend(struct device *dev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(dev);

	regcache_cache_only(rcd->regmap, true);
	clk_disable_unprepare(rcd->clk_adc);
	clk_disable_unprepare(rcd->clk_dac);
	clk_disable_unprepare(rcd->clk_i2c);
	clk_disable_unprepare(rcd->pclk);

	return 0;
}
#endif

static int rk_codec_digital_platform_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rk_codec_digital_priv *rcd;
	struct resource *res;
	void __iomem *base;
	int ret = 0;

	rcd = devm_kzalloc(&pdev->dev, sizeof(*rcd), GFP_KERNEL);
	if (!rcd)
		return -ENOMEM;

	rcd->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	rcd->pwmout = of_property_read_bool(np, "rockchip,pwm-output-mode");
	rcd->sync = of_property_read_bool(np, "rockchip,clk-sync-mode");
	if (of_property_read_u32(np, "rockchip,pa-ctl-delay-ms",
				 &rcd->pa_ctl_delay_ms))
		rcd->pa_ctl_delay_ms = 0;

	rcd->rc = devm_reset_control_get(&pdev->dev, "reset");

	/* optional on some platform */
	rcd->clk_adc = devm_clk_get_optional(&pdev->dev, "adc");
	if (IS_ERR(rcd->clk_adc))
		return PTR_ERR(rcd->clk_adc);

	rcd->clk_dac = devm_clk_get(&pdev->dev, "dac");
	if (IS_ERR(rcd->clk_dac))
		return PTR_ERR(rcd->clk_dac);

	/* optional on some platform */
	rcd->clk_i2c = devm_clk_get_optional(&pdev->dev, "i2c");
	if (IS_ERR(rcd->clk_i2c))
		return PTR_ERR(rcd->clk_i2c);

	rcd->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(rcd->pclk))
		return PTR_ERR(rcd->pclk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	rcd->regmap =
		devm_regmap_init_mmio(&pdev->dev, base, &rcd_regmap_config);
	if (IS_ERR(rcd->regmap))
		return PTR_ERR(rcd->regmap);

	platform_set_drvdata(pdev, rcd);

	rcd->data = of_device_get_match_data(&pdev->dev);
	if (rcd->data && rcd->data->init) {
		ret = rcd->data->init(&pdev->dev);
		if (ret)
			return ret;
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = rk_codec_digital_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	if (rcd->pwmout)
		regmap_update_bits(rcd->regmap, DACPWM_CTRL,
				   ACDCDIG_DACPWM_CTRL_PWM_MODE_MASK,
				   ACDCDIG_DACPWM_CTRL_PWM_MODE_0);

	rcd->pa_ctl = devm_gpiod_get_optional(&pdev->dev, "pa-ctl",
					       GPIOD_OUT_LOW);

	if (!rcd->pa_ctl) {
		dev_info(&pdev->dev, "no need pa-ctl gpio\n");
	} else if (IS_ERR(rcd->pa_ctl)) {
		ret = PTR_ERR(rcd->pa_ctl);
		dev_err(&pdev->dev, "fail to request gpio pa-ctl\n");
		goto err_suspend;
	}

	ret = devm_snd_soc_register_component(&pdev->dev, &soc_codec_dev_rcd,
					      rcd_dai, ARRAY_SIZE(rcd_dai));

	if (ret)
		goto err_suspend;

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		rk_codec_digital_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	if (rcd->data && rcd->data->deinit)
		rcd->data->deinit(&pdev->dev);

	return ret;
}

static int rk_codec_digital_platform_remove(struct platform_device *pdev)
{
	struct rk_codec_digital_priv *rcd = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		rk_codec_digital_runtime_suspend(&pdev->dev);

	if (rcd->data && rcd->data->deinit)
		rcd->data->deinit(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops rcd_pm = {
	SET_RUNTIME_PM_OPS(rk_codec_digital_runtime_suspend,
		rk_codec_digital_runtime_resume, NULL)
};

static struct platform_driver rk_codec_digital_driver = {
	.driver = {
		.name = "rk_codec_digital",
		.of_match_table = of_match_ptr(rcd_of_match),
		.pm = &rcd_pm,
	},
	.probe = rk_codec_digital_platform_probe,
	.remove = rk_codec_digital_platform_remove,
};
module_platform_driver(rk_codec_digital_driver);

MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("ASoC Rockchip codec digital driver");
MODULE_LICENSE("GPL v2");
