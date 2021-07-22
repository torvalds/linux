// SPDX-License-Identifier: GPL-2.0-only
//
// Cirrus Logic Madera class codecs common support
//
// Copyright (C) 2015-2019 Cirrus Logic, Inc. and
//                         Cirrus Logic International Semiconductor Ltd.
//

#include <linux/delay.h>
#include <linux/gcd.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include <linux/irqchip/irq-madera.h>
#include <linux/mfd/madera/core.h>
#include <linux/mfd/madera/registers.h>
#include <linux/mfd/madera/pdata.h>
#include <sound/madera-pdata.h>

#include <dt-bindings/sound/madera.h>

#include "madera.h"

#define MADERA_AIF_BCLK_CTRL			0x00
#define MADERA_AIF_TX_PIN_CTRL			0x01
#define MADERA_AIF_RX_PIN_CTRL			0x02
#define MADERA_AIF_RATE_CTRL			0x03
#define MADERA_AIF_FORMAT			0x04
#define MADERA_AIF_RX_BCLK_RATE			0x06
#define MADERA_AIF_FRAME_CTRL_1			0x07
#define MADERA_AIF_FRAME_CTRL_2			0x08
#define MADERA_AIF_FRAME_CTRL_3			0x09
#define MADERA_AIF_FRAME_CTRL_4			0x0A
#define MADERA_AIF_FRAME_CTRL_5			0x0B
#define MADERA_AIF_FRAME_CTRL_6			0x0C
#define MADERA_AIF_FRAME_CTRL_7			0x0D
#define MADERA_AIF_FRAME_CTRL_8			0x0E
#define MADERA_AIF_FRAME_CTRL_9			0x0F
#define MADERA_AIF_FRAME_CTRL_10		0x10
#define MADERA_AIF_FRAME_CTRL_11		0x11
#define MADERA_AIF_FRAME_CTRL_12		0x12
#define MADERA_AIF_FRAME_CTRL_13		0x13
#define MADERA_AIF_FRAME_CTRL_14		0x14
#define MADERA_AIF_FRAME_CTRL_15		0x15
#define MADERA_AIF_FRAME_CTRL_16		0x16
#define MADERA_AIF_FRAME_CTRL_17		0x17
#define MADERA_AIF_FRAME_CTRL_18		0x18
#define MADERA_AIF_TX_ENABLES			0x19
#define MADERA_AIF_RX_ENABLES			0x1A
#define MADERA_AIF_FORCE_WRITE			0x1B

#define MADERA_DSP_CONFIG_1_OFFS		0x00
#define MADERA_DSP_CONFIG_2_OFFS		0x02

#define MADERA_DSP_CLK_SEL_MASK			0x70000
#define MADERA_DSP_CLK_SEL_SHIFT		16

#define MADERA_DSP_RATE_MASK			0x7800
#define MADERA_DSP_RATE_SHIFT			11

#define MADERA_SYSCLK_6MHZ			0
#define MADERA_SYSCLK_12MHZ			1
#define MADERA_SYSCLK_24MHZ			2
#define MADERA_SYSCLK_49MHZ			3
#define MADERA_SYSCLK_98MHZ			4

#define MADERA_DSPCLK_9MHZ			0
#define MADERA_DSPCLK_18MHZ			1
#define MADERA_DSPCLK_36MHZ			2
#define MADERA_DSPCLK_73MHZ			3
#define MADERA_DSPCLK_147MHZ			4

#define MADERA_FLL_VCO_CORNER			141900000
#define MADERA_FLL_MAX_FREF			13500000
#define MADERA_FLL_MAX_N			1023
#define MADERA_FLL_MIN_FOUT			90000000
#define MADERA_FLL_MAX_FOUT			100000000
#define MADERA_FLL_MAX_FRATIO			16
#define MADERA_FLL_MAX_REFDIV			8
#define MADERA_FLL_OUTDIV			3
#define MADERA_FLL_VCO_MULT			3
#define MADERA_FLLAO_MAX_FREF			12288000
#define MADERA_FLLAO_MIN_N			4
#define MADERA_FLLAO_MAX_N			1023
#define MADERA_FLLAO_MAX_FBDIV			254
#define MADERA_FLLHJ_INT_MAX_N			1023
#define MADERA_FLLHJ_INT_MIN_N			1
#define MADERA_FLLHJ_FRAC_MAX_N			255
#define MADERA_FLLHJ_FRAC_MIN_N			4
#define MADERA_FLLHJ_LOW_THRESH			192000
#define MADERA_FLLHJ_MID_THRESH			1152000
#define MADERA_FLLHJ_MAX_THRESH			13000000
#define MADERA_FLLHJ_LOW_GAINS			0x23f0
#define MADERA_FLLHJ_MID_GAINS			0x22f2
#define MADERA_FLLHJ_HIGH_GAINS			0x21f0

#define MADERA_FLL_SYNCHRONISER_OFFS		0x10
#define CS47L35_FLL_SYNCHRONISER_OFFS		0xE
#define MADERA_FLL_CONTROL_1_OFFS		0x1
#define MADERA_FLL_CONTROL_2_OFFS		0x2
#define MADERA_FLL_CONTROL_3_OFFS		0x3
#define MADERA_FLL_CONTROL_4_OFFS		0x4
#define MADERA_FLL_CONTROL_5_OFFS		0x5
#define MADERA_FLL_CONTROL_6_OFFS		0x6
#define MADERA_FLL_GAIN_OFFS			0x8
#define MADERA_FLL_CONTROL_7_OFFS		0x9
#define MADERA_FLL_EFS_2_OFFS			0xA
#define MADERA_FLL_SYNCHRONISER_1_OFFS		0x1
#define MADERA_FLL_SYNCHRONISER_2_OFFS		0x2
#define MADERA_FLL_SYNCHRONISER_3_OFFS		0x3
#define MADERA_FLL_SYNCHRONISER_4_OFFS		0x4
#define MADERA_FLL_SYNCHRONISER_5_OFFS		0x5
#define MADERA_FLL_SYNCHRONISER_6_OFFS		0x6
#define MADERA_FLL_SYNCHRONISER_7_OFFS		0x7
#define MADERA_FLL_SPREAD_SPECTRUM_OFFS		0x9
#define MADERA_FLL_GPIO_CLOCK_OFFS		0xA
#define MADERA_FLL_CONTROL_10_OFFS		0xA
#define MADERA_FLL_CONTROL_11_OFFS		0xB
#define MADERA_FLL1_DIGITAL_TEST_1_OFFS		0xD

#define MADERA_FLLAO_CONTROL_1_OFFS		0x1
#define MADERA_FLLAO_CONTROL_2_OFFS		0x2
#define MADERA_FLLAO_CONTROL_3_OFFS		0x3
#define MADERA_FLLAO_CONTROL_4_OFFS		0x4
#define MADERA_FLLAO_CONTROL_5_OFFS		0x5
#define MADERA_FLLAO_CONTROL_6_OFFS		0x6
#define MADERA_FLLAO_CONTROL_7_OFFS		0x8
#define MADERA_FLLAO_CONTROL_8_OFFS		0xA
#define MADERA_FLLAO_CONTROL_9_OFFS		0xB
#define MADERA_FLLAO_CONTROL_10_OFFS		0xC
#define MADERA_FLLAO_CONTROL_11_OFFS		0xD

#define MADERA_FMT_DSP_MODE_A			0
#define MADERA_FMT_DSP_MODE_B			1
#define MADERA_FMT_I2S_MODE			2
#define MADERA_FMT_LEFT_JUSTIFIED_MODE		3

#define madera_fll_err(_fll, fmt, ...) \
	dev_err(_fll->madera->dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)
#define madera_fll_warn(_fll, fmt, ...) \
	dev_warn(_fll->madera->dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)
#define madera_fll_dbg(_fll, fmt, ...) \
	dev_dbg(_fll->madera->dev, "FLL%d: " fmt, _fll->id, ##__VA_ARGS__)

#define madera_aif_err(_dai, fmt, ...) \
	dev_err(_dai->dev, "AIF%d: " fmt, _dai->id, ##__VA_ARGS__)
#define madera_aif_warn(_dai, fmt, ...) \
	dev_warn(_dai->dev, "AIF%d: " fmt, _dai->id, ##__VA_ARGS__)
#define madera_aif_dbg(_dai, fmt, ...) \
	dev_dbg(_dai->dev, "AIF%d: " fmt, _dai->id, ##__VA_ARGS__)

static const int madera_dsp_bus_error_irqs[MADERA_MAX_ADSP] = {
	MADERA_IRQ_DSP1_BUS_ERR,
	MADERA_IRQ_DSP2_BUS_ERR,
	MADERA_IRQ_DSP3_BUS_ERR,
	MADERA_IRQ_DSP4_BUS_ERR,
	MADERA_IRQ_DSP5_BUS_ERR,
	MADERA_IRQ_DSP6_BUS_ERR,
	MADERA_IRQ_DSP7_BUS_ERR,
};

int madera_clk_ev(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	unsigned int val;
	int clk_idx;
	int ret;

	ret = regmap_read(madera->regmap, w->reg, &val);
	if (ret) {
		dev_err(madera->dev, "Failed to check clock source: %d\n", ret);
		return ret;
	}

	switch ((val & MADERA_SYSCLK_SRC_MASK) >> MADERA_SYSCLK_SRC_SHIFT) {
	case MADERA_CLK_SRC_MCLK1:
		clk_idx = MADERA_MCLK1;
		break;
	case MADERA_CLK_SRC_MCLK2:
		clk_idx = MADERA_MCLK2;
		break;
	case MADERA_CLK_SRC_MCLK3:
		clk_idx = MADERA_MCLK3;
		break;
	default:
		return 0;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return clk_prepare_enable(madera->mclk[clk_idx].clk);
	case SND_SOC_DAPM_POST_PMD:
		clk_disable_unprepare(madera->mclk[clk_idx].clk);
		return 0;
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(madera_clk_ev);

static void madera_spin_sysclk(struct madera_priv *priv)
{
	struct madera *madera = priv->madera;
	unsigned int val;
	int ret, i;

	/* Skip this if the chip is down */
	if (pm_runtime_suspended(madera->dev))
		return;

	/*
	 * Just read a register a few times to ensure the internal
	 * oscillator sends out a few clocks.
	 */
	for (i = 0; i < 4; i++) {
		ret = regmap_read(madera->regmap, MADERA_SOFTWARE_RESET, &val);
		if (ret)
			dev_err(madera->dev,
				"Failed to read sysclk spin %d: %d\n", i, ret);
	}

	udelay(300);
}

int madera_sysclk_ev(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
	case SND_SOC_DAPM_PRE_PMD:
		madera_spin_sysclk(priv);
		break;
	default:
		break;
	}

	return madera_clk_ev(w, kcontrol, event);
}
EXPORT_SYMBOL_GPL(madera_sysclk_ev);

static int madera_check_speaker_overheat(struct madera *madera,
					 bool *warn, bool *shutdown)
{
	unsigned int val;
	int ret;

	ret = regmap_read(madera->regmap, MADERA_IRQ1_RAW_STATUS_15, &val);
	if (ret) {
		dev_err(madera->dev, "Failed to read thermal status: %d\n",
			ret);
		return ret;
	}

	*warn = val & MADERA_SPK_OVERHEAT_WARN_STS1;
	*shutdown = val & MADERA_SPK_OVERHEAT_STS1;

	return 0;
}

int madera_spk_ev(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	bool warn, shutdown;
	int ret;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		ret = madera_check_speaker_overheat(madera, &warn, &shutdown);
		if (ret)
			return ret;

		if (shutdown) {
			dev_crit(madera->dev,
				 "Speaker not enabled due to temperature\n");
			return -EBUSY;
		}

		regmap_update_bits(madera->regmap, MADERA_OUTPUT_ENABLES_1,
				   1 << w->shift, 1 << w->shift);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(madera->regmap, MADERA_OUTPUT_ENABLES_1,
				   1 << w->shift, 0);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(madera_spk_ev);

static irqreturn_t madera_thermal_warn(int irq, void *data)
{
	struct madera *madera = data;
	bool warn, shutdown;
	int ret;

	ret = madera_check_speaker_overheat(madera, &warn, &shutdown);
	if (ret || shutdown) { /* for safety attempt to shutdown on error */
		dev_crit(madera->dev, "Thermal shutdown\n");
		ret = regmap_update_bits(madera->regmap,
					 MADERA_OUTPUT_ENABLES_1,
					 MADERA_OUT4L_ENA |
					 MADERA_OUT4R_ENA, 0);
		if (ret != 0)
			dev_crit(madera->dev,
				 "Failed to disable speaker outputs: %d\n",
				 ret);
	} else if (warn) {
		dev_alert(madera->dev, "Thermal warning\n");
	} else {
		dev_info(madera->dev, "Spurious thermal warning\n");
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

int madera_init_overheat(struct madera_priv *priv)
{
	struct madera *madera = priv->madera;
	struct device *dev = madera->dev;
	int ret;

	ret = madera_request_irq(madera, MADERA_IRQ_SPK_OVERHEAT_WARN,
				 "Thermal warning", madera_thermal_warn,
				 madera);
	if (ret)
		dev_err(dev, "Failed to get thermal warning IRQ: %d\n", ret);

	ret = madera_request_irq(madera, MADERA_IRQ_SPK_OVERHEAT,
				 "Thermal shutdown", madera_thermal_warn,
				 madera);
	if (ret)
		dev_err(dev, "Failed to get thermal shutdown IRQ: %d\n", ret);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_overheat);

int madera_free_overheat(struct madera_priv *priv)
{
	struct madera *madera = priv->madera;

	madera_free_irq(madera, MADERA_IRQ_SPK_OVERHEAT_WARN, madera);
	madera_free_irq(madera, MADERA_IRQ_SPK_OVERHEAT, madera);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_free_overheat);

static int madera_get_variable_u32_array(struct device *dev,
					 const char *propname,
					 u32 *dest, int n_max,
					 int multiple)
{
	int n, ret;

	n = device_property_count_u32(dev, propname);
	if (n < 0) {
		if (n == -EINVAL)
			return 0;	/* missing, ignore */

		dev_warn(dev, "%s malformed (%d)\n", propname, n);

		return n;
	} else if ((n % multiple) != 0) {
		dev_warn(dev, "%s not a multiple of %d entries\n",
			 propname, multiple);

		return -EINVAL;
	}

	if (n > n_max)
		n = n_max;

	ret = device_property_read_u32_array(dev, propname, dest, n);
	if (ret < 0)
		return ret;

	return n;
}

static void madera_prop_get_inmode(struct madera_priv *priv)
{
	struct madera *madera = priv->madera;
	struct madera_codec_pdata *pdata = &madera->pdata.codec;
	u32 tmp[MADERA_MAX_INPUT * MADERA_MAX_MUXED_CHANNELS];
	int n, i, in_idx, ch_idx;

	BUILD_BUG_ON(ARRAY_SIZE(pdata->inmode) != MADERA_MAX_INPUT);
	BUILD_BUG_ON(ARRAY_SIZE(pdata->inmode[0]) != MADERA_MAX_MUXED_CHANNELS);

	n = madera_get_variable_u32_array(madera->dev, "cirrus,inmode",
					  tmp, ARRAY_SIZE(tmp),
					  MADERA_MAX_MUXED_CHANNELS);
	if (n < 0)
		return;

	in_idx = 0;
	ch_idx = 0;
	for (i = 0; i < n; ++i) {
		pdata->inmode[in_idx][ch_idx] = tmp[i];

		if (++ch_idx == MADERA_MAX_MUXED_CHANNELS) {
			ch_idx = 0;
			++in_idx;
		}
	}
}

static void madera_prop_get_pdata(struct madera_priv *priv)
{
	struct madera *madera = priv->madera;
	struct madera_codec_pdata *pdata = &madera->pdata.codec;
	u32 out_mono[ARRAY_SIZE(pdata->out_mono)];
	int i, n;

	madera_prop_get_inmode(priv);

	n = madera_get_variable_u32_array(madera->dev, "cirrus,out-mono",
					  out_mono, ARRAY_SIZE(out_mono), 1);
	if (n > 0)
		for (i = 0; i < n; ++i)
			pdata->out_mono[i] = !!out_mono[i];

	madera_get_variable_u32_array(madera->dev,
				      "cirrus,max-channels-clocked",
				      pdata->max_channels_clocked,
				      ARRAY_SIZE(pdata->max_channels_clocked),
				      1);

	madera_get_variable_u32_array(madera->dev, "cirrus,pdm-fmt",
				      pdata->pdm_fmt,
				      ARRAY_SIZE(pdata->pdm_fmt), 1);

	madera_get_variable_u32_array(madera->dev, "cirrus,pdm-mute",
				      pdata->pdm_mute,
				      ARRAY_SIZE(pdata->pdm_mute), 1);

	madera_get_variable_u32_array(madera->dev, "cirrus,dmic-ref",
				      pdata->dmic_ref,
				      ARRAY_SIZE(pdata->dmic_ref), 1);
}

int madera_core_init(struct madera_priv *priv)
{
	int i;

	/* trap undersized array initializers */
	BUILD_BUG_ON(!madera_mixer_texts[MADERA_NUM_MIXER_INPUTS - 1]);
	BUILD_BUG_ON(!madera_mixer_values[MADERA_NUM_MIXER_INPUTS - 1]);

	if (!dev_get_platdata(priv->madera->dev))
		madera_prop_get_pdata(priv);

	mutex_init(&priv->rate_lock);

	for (i = 0; i < MADERA_MAX_HP_OUTPUT; i++)
		priv->madera->out_clamp[i] = true;

	return 0;
}
EXPORT_SYMBOL_GPL(madera_core_init);

int madera_core_free(struct madera_priv *priv)
{
	mutex_destroy(&priv->rate_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_core_free);

static void madera_debug_dump_domain_groups(const struct madera_priv *priv)
{
	struct madera *madera = priv->madera;
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->domain_group_ref); ++i)
		dev_dbg(madera->dev, "domain_grp_ref[%d]=%d\n", i,
			priv->domain_group_ref[i]);
}

int madera_domain_clk_ev(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol,
			 int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	int dom_grp = w->shift;

	if (dom_grp >= ARRAY_SIZE(priv->domain_group_ref)) {
		WARN(true, "%s dom_grp exceeds array size\n", __func__);
		return -EINVAL;
	}

	/*
	 * We can't rely on the DAPM mutex for locking because we need a lock
	 * that can safely be called in hw_params
	 */
	mutex_lock(&priv->rate_lock);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_dbg(priv->madera->dev, "Inc ref on domain group %d\n",
			dom_grp);
		++priv->domain_group_ref[dom_grp];
		break;
	case SND_SOC_DAPM_POST_PMD:
		dev_dbg(priv->madera->dev, "Dec ref on domain group %d\n",
			dom_grp);
		--priv->domain_group_ref[dom_grp];
		break;
	default:
		break;
	}

	madera_debug_dump_domain_groups(priv);

	mutex_unlock(&priv->rate_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_domain_clk_ev);

int madera_out1_demux_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int ep_sel, mux, change;
	bool out_mono;
	int ret;

	if (ucontrol->value.enumerated.item[0] > e->items - 1)
		return -EINVAL;

	mux = ucontrol->value.enumerated.item[0];

	snd_soc_dapm_mutex_lock(dapm);

	ep_sel = mux << MADERA_EP_SEL_SHIFT;

	change = snd_soc_component_test_bits(component, MADERA_OUTPUT_ENABLES_1,
					     MADERA_EP_SEL_MASK,
					     ep_sel);
	if (!change)
		goto end;

	/* EP_SEL should not be modified while HP or EP driver is enabled */
	ret = regmap_update_bits(madera->regmap, MADERA_OUTPUT_ENABLES_1,
				 MADERA_OUT1L_ENA | MADERA_OUT1R_ENA, 0);
	if (ret)
		dev_warn(madera->dev, "Failed to disable outputs: %d\n", ret);

	usleep_range(2000, 3000); /* wait for wseq to complete */

	/* change demux setting */
	ret = 0;
	if (madera->out_clamp[0])
		ret = regmap_update_bits(madera->regmap,
					 MADERA_OUTPUT_ENABLES_1,
					 MADERA_EP_SEL_MASK, ep_sel);
	if (ret) {
		dev_err(madera->dev, "Failed to set OUT1 demux: %d\n", ret);
	} else {
		/* apply correct setting for mono mode */
		if (!ep_sel && !madera->pdata.codec.out_mono[0])
			out_mono = false; /* stereo HP */
		else
			out_mono = true; /* EP or mono HP */

		ret = madera_set_output_mode(component, 1, out_mono);
		if (ret)
			dev_warn(madera->dev,
				 "Failed to set output mode: %d\n", ret);
	}

	/*
	 * if HPDET has disabled the clamp while switching to HPOUT
	 * OUT1 should remain disabled
	 */
	if (ep_sel ||
	    (madera->out_clamp[0] && !madera->out_shorted[0])) {
		ret = regmap_update_bits(madera->regmap,
					 MADERA_OUTPUT_ENABLES_1,
					 MADERA_OUT1L_ENA | MADERA_OUT1R_ENA,
					 madera->hp_ena);
		if (ret)
			dev_warn(madera->dev,
				 "Failed to restore earpiece outputs: %d\n",
				 ret);
		else if (madera->hp_ena)
			msleep(34); /* wait for enable wseq */
		else
			usleep_range(2000, 3000); /* wait for disable wseq */
	}

end:
	snd_soc_dapm_mutex_unlock(dapm);

	return snd_soc_dapm_mux_update_power(dapm, kcontrol, mux, e, NULL);
}
EXPORT_SYMBOL_GPL(madera_out1_demux_put);

int madera_out1_demux_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	unsigned int val;

	val = snd_soc_component_read(component, MADERA_OUTPUT_ENABLES_1);
	val &= MADERA_EP_SEL_MASK;
	val >>= MADERA_EP_SEL_SHIFT;
	ucontrol->value.enumerated.item[0] = val;

	return 0;
}
EXPORT_SYMBOL_GPL(madera_out1_demux_get);

static int madera_inmux_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	struct regmap *regmap = madera->regmap;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int mux, val, mask;
	unsigned int inmode;
	bool changed;
	int ret;

	mux = ucontrol->value.enumerated.item[0];
	if (mux > 1)
		return -EINVAL;

	val = mux << e->shift_l;
	mask = (e->mask << e->shift_l) | MADERA_IN1L_SRC_SE_MASK;

	switch (e->reg) {
	case MADERA_ADC_DIGITAL_VOLUME_1L:
		inmode = madera->pdata.codec.inmode[0][2 * mux];
		break;
	case MADERA_ADC_DIGITAL_VOLUME_1R:
		inmode = madera->pdata.codec.inmode[0][1 + (2 * mux)];
		break;
	case MADERA_ADC_DIGITAL_VOLUME_2L:
		inmode = madera->pdata.codec.inmode[1][2 * mux];
		break;
	case MADERA_ADC_DIGITAL_VOLUME_2R:
		inmode = madera->pdata.codec.inmode[1][1 + (2 * mux)];
		break;
	default:
		return -EINVAL;
	}

	if (inmode & MADERA_INMODE_SE)
		val |= 1 << MADERA_IN1L_SRC_SE_SHIFT;

	dev_dbg(madera->dev, "mux=%u reg=0x%x inmode=0x%x mask=0x%x val=0x%x\n",
		mux, e->reg, inmode, mask, val);

	ret = regmap_update_bits_check(regmap, e->reg, mask, val, &changed);
	if (ret < 0)
		return ret;

	if (changed)
		return snd_soc_dapm_mux_update_power(dapm, kcontrol,
						     mux, e, NULL);
	else
		return 0;
}

static const char * const madera_inmux_texts[] = {
	"A",
	"B",
};

static SOC_ENUM_SINGLE_DECL(madera_in1muxl_enum,
			    MADERA_ADC_DIGITAL_VOLUME_1L,
			    MADERA_IN1L_SRC_SHIFT,
			    madera_inmux_texts);

static SOC_ENUM_SINGLE_DECL(madera_in1muxr_enum,
			    MADERA_ADC_DIGITAL_VOLUME_1R,
			    MADERA_IN1R_SRC_SHIFT,
			    madera_inmux_texts);

static SOC_ENUM_SINGLE_DECL(madera_in2muxl_enum,
			    MADERA_ADC_DIGITAL_VOLUME_2L,
			    MADERA_IN2L_SRC_SHIFT,
			    madera_inmux_texts);

static SOC_ENUM_SINGLE_DECL(madera_in2muxr_enum,
			    MADERA_ADC_DIGITAL_VOLUME_2R,
			    MADERA_IN2R_SRC_SHIFT,
			    madera_inmux_texts);

const struct snd_kcontrol_new madera_inmux[] = {
	SOC_DAPM_ENUM_EXT("IN1L Mux", madera_in1muxl_enum,
			  snd_soc_dapm_get_enum_double, madera_inmux_put),
	SOC_DAPM_ENUM_EXT("IN1R Mux", madera_in1muxr_enum,
			  snd_soc_dapm_get_enum_double, madera_inmux_put),
	SOC_DAPM_ENUM_EXT("IN2L Mux", madera_in2muxl_enum,
			  snd_soc_dapm_get_enum_double, madera_inmux_put),
	SOC_DAPM_ENUM_EXT("IN2R Mux", madera_in2muxr_enum,
			  snd_soc_dapm_get_enum_double, madera_inmux_put),
};
EXPORT_SYMBOL_GPL(madera_inmux);

static const char * const madera_dmode_texts[] = {
	"Analog",
	"Digital",
};

static SOC_ENUM_SINGLE_DECL(madera_in1dmode_enum,
			    MADERA_IN1L_CONTROL,
			    MADERA_IN1_MODE_SHIFT,
			    madera_dmode_texts);

static SOC_ENUM_SINGLE_DECL(madera_in2dmode_enum,
			    MADERA_IN2L_CONTROL,
			    MADERA_IN2_MODE_SHIFT,
			    madera_dmode_texts);

static SOC_ENUM_SINGLE_DECL(madera_in3dmode_enum,
			    MADERA_IN3L_CONTROL,
			    MADERA_IN3_MODE_SHIFT,
			    madera_dmode_texts);

const struct snd_kcontrol_new madera_inmode[] = {
	SOC_DAPM_ENUM("IN1 Mode", madera_in1dmode_enum),
	SOC_DAPM_ENUM("IN2 Mode", madera_in2dmode_enum),
	SOC_DAPM_ENUM("IN3 Mode", madera_in3dmode_enum),
};
EXPORT_SYMBOL_GPL(madera_inmode);

static bool madera_can_change_grp_rate(const struct madera_priv *priv,
				       unsigned int reg)
{
	int count;

	switch (reg) {
	case MADERA_FX_CTRL1:
		count = priv->domain_group_ref[MADERA_DOM_GRP_FX];
		break;
	case MADERA_ASRC1_RATE1:
	case MADERA_ASRC1_RATE2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_ASRC1];
		break;
	case MADERA_ASRC2_RATE1:
	case MADERA_ASRC2_RATE2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_ASRC2];
		break;
	case MADERA_ISRC_1_CTRL_1:
	case MADERA_ISRC_1_CTRL_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_ISRC1];
		break;
	case MADERA_ISRC_2_CTRL_1:
	case MADERA_ISRC_2_CTRL_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_ISRC2];
		break;
	case MADERA_ISRC_3_CTRL_1:
	case MADERA_ISRC_3_CTRL_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_ISRC3];
		break;
	case MADERA_ISRC_4_CTRL_1:
	case MADERA_ISRC_4_CTRL_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_ISRC4];
		break;
	case MADERA_OUTPUT_RATE_1:
		count = priv->domain_group_ref[MADERA_DOM_GRP_OUT];
		break;
	case MADERA_SPD1_TX_CONTROL:
		count = priv->domain_group_ref[MADERA_DOM_GRP_SPD];
		break;
	case MADERA_DSP1_CONFIG_1:
	case MADERA_DSP1_CONFIG_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_DSP1];
		break;
	case MADERA_DSP2_CONFIG_1:
	case MADERA_DSP2_CONFIG_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_DSP2];
		break;
	case MADERA_DSP3_CONFIG_1:
	case MADERA_DSP3_CONFIG_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_DSP3];
		break;
	case MADERA_DSP4_CONFIG_1:
	case MADERA_DSP4_CONFIG_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_DSP4];
		break;
	case MADERA_DSP5_CONFIG_1:
	case MADERA_DSP5_CONFIG_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_DSP5];
		break;
	case MADERA_DSP6_CONFIG_1:
	case MADERA_DSP6_CONFIG_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_DSP6];
		break;
	case MADERA_DSP7_CONFIG_1:
	case MADERA_DSP7_CONFIG_2:
		count = priv->domain_group_ref[MADERA_DOM_GRP_DSP7];
		break;
	case MADERA_AIF1_RATE_CTRL:
		count = priv->domain_group_ref[MADERA_DOM_GRP_AIF1];
		break;
	case MADERA_AIF2_RATE_CTRL:
		count = priv->domain_group_ref[MADERA_DOM_GRP_AIF2];
		break;
	case MADERA_AIF3_RATE_CTRL:
		count = priv->domain_group_ref[MADERA_DOM_GRP_AIF3];
		break;
	case MADERA_AIF4_RATE_CTRL:
		count = priv->domain_group_ref[MADERA_DOM_GRP_AIF4];
		break;
	case MADERA_SLIMBUS_RATES_1:
	case MADERA_SLIMBUS_RATES_2:
	case MADERA_SLIMBUS_RATES_3:
	case MADERA_SLIMBUS_RATES_4:
	case MADERA_SLIMBUS_RATES_5:
	case MADERA_SLIMBUS_RATES_6:
	case MADERA_SLIMBUS_RATES_7:
	case MADERA_SLIMBUS_RATES_8:
		count = priv->domain_group_ref[MADERA_DOM_GRP_SLIMBUS];
		break;
	case MADERA_PWM_DRIVE_1:
		count = priv->domain_group_ref[MADERA_DOM_GRP_PWM];
		break;
	default:
		return false;
	}

	dev_dbg(priv->madera->dev, "Rate reg 0x%x group ref %d\n", reg, count);

	if (count)
		return false;
	else
		return true;
}

static int madera_adsp_rate_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int cached_rate;
	const int adsp_num = e->shift_l;
	int item;

	mutex_lock(&priv->rate_lock);
	cached_rate = priv->adsp_rate_cache[adsp_num];
	mutex_unlock(&priv->rate_lock);

	item = snd_soc_enum_val_to_item(e, cached_rate);
	ucontrol->value.enumerated.item[0] = item;

	return 0;
}

static int madera_adsp_rate_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	const int adsp_num = e->shift_l;
	const unsigned int item = ucontrol->value.enumerated.item[0];
	int ret;

	if (item >= e->items)
		return -EINVAL;

	/*
	 * We don't directly write the rate register here but we want to
	 * maintain consistent behaviour that rate domains cannot be changed
	 * while in use since this is a hardware requirement
	 */
	mutex_lock(&priv->rate_lock);

	if (!madera_can_change_grp_rate(priv, priv->adsp[adsp_num].base)) {
		dev_warn(priv->madera->dev,
			 "Cannot change '%s' while in use by active audio paths\n",
			 kcontrol->id.name);
		ret = -EBUSY;
	} else {
		/* Volatile register so defer until the codec is powered up */
		priv->adsp_rate_cache[adsp_num] = e->values[item];
		ret = 0;
	}

	mutex_unlock(&priv->rate_lock);

	return ret;
}

static const struct soc_enum madera_adsp_rate_enum[] = {
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 0, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 1, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 2, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 3, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 4, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 5, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(SND_SOC_NOPM, 6, 0xf, MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
};

const struct snd_kcontrol_new madera_adsp_rate_controls[] = {
	SOC_ENUM_EXT("DSP1 Rate", madera_adsp_rate_enum[0],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP2 Rate", madera_adsp_rate_enum[1],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP3 Rate", madera_adsp_rate_enum[2],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP4 Rate", madera_adsp_rate_enum[3],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP5 Rate", madera_adsp_rate_enum[4],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP6 Rate", madera_adsp_rate_enum[5],
		     madera_adsp_rate_get, madera_adsp_rate_put),
	SOC_ENUM_EXT("DSP7 Rate", madera_adsp_rate_enum[6],
		     madera_adsp_rate_get, madera_adsp_rate_put),
};
EXPORT_SYMBOL_GPL(madera_adsp_rate_controls);

static int madera_write_adsp_clk_setting(struct madera_priv *priv,
					 struct wm_adsp *dsp,
					 unsigned int freq)
{
	unsigned int val;
	unsigned int mask = MADERA_DSP_RATE_MASK;
	int ret;

	val = priv->adsp_rate_cache[dsp->num - 1] << MADERA_DSP_RATE_SHIFT;

	switch (priv->madera->type) {
	case CS47L35:
	case CS47L85:
	case WM1840:
		/* use legacy frequency registers */
		mask |= MADERA_DSP_CLK_SEL_MASK;
		val |= (freq << MADERA_DSP_CLK_SEL_SHIFT);
		break;
	default:
		/* Configure exact dsp frequency */
		dev_dbg(priv->madera->dev, "Set DSP frequency to 0x%x\n", freq);

		ret = regmap_write(dsp->regmap,
				   dsp->base + MADERA_DSP_CONFIG_2_OFFS, freq);
		if (ret)
			goto err;
		break;
	}

	ret = regmap_update_bits(dsp->regmap,
				 dsp->base + MADERA_DSP_CONFIG_1_OFFS,
				 mask, val);
	if (ret)
		goto err;

	dev_dbg(priv->madera->dev, "Set DSP clocking to 0x%x\n", val);

	return 0;

err:
	dev_err(dsp->dev, "Failed to set DSP%d clock: %d\n", dsp->num, ret);

	return ret;
}

int madera_set_adsp_clk(struct madera_priv *priv, int dsp_num,
			unsigned int freq)
{
	struct wm_adsp *dsp = &priv->adsp[dsp_num];
	struct madera *madera = priv->madera;
	unsigned int cur, new;
	int ret;

	/*
	 * This is called at a higher DAPM priority than the mux widgets so
	 * the muxes are still off at this point and it's safe to change
	 * the rate domain control.
	 * Also called at a lower DAPM priority than the domain group widgets
	 * so locking the reads of adsp_rate_cache is not necessary as we know
	 * changes are locked out by the domain_group_ref reference count.
	 */

	ret = regmap_read(dsp->regmap,  dsp->base, &cur);
	if (ret) {
		dev_err(madera->dev,
			"Failed to read current DSP rate: %d\n", ret);
		return ret;
	}

	cur &= MADERA_DSP_RATE_MASK;

	new = priv->adsp_rate_cache[dsp->num - 1] << MADERA_DSP_RATE_SHIFT;

	if (new == cur) {
		dev_dbg(madera->dev, "DSP rate not changed\n");
		return madera_write_adsp_clk_setting(priv, dsp, freq);
	} else {
		dev_dbg(madera->dev, "DSP rate changed\n");

		/* The write must be guarded by a number of SYSCLK cycles */
		madera_spin_sysclk(priv);
		ret = madera_write_adsp_clk_setting(priv, dsp, freq);
		madera_spin_sysclk(priv);
		return ret;
	}
}
EXPORT_SYMBOL_GPL(madera_set_adsp_clk);

int madera_rate_put(struct snd_kcontrol *kcontrol,
		    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int item = ucontrol->value.enumerated.item[0];
	unsigned int val;
	int ret;

	if (item >= e->items)
		return -EINVAL;

	/*
	 * Prevent the domain powering up while we're checking whether it's
	 * safe to change rate domain
	 */
	mutex_lock(&priv->rate_lock);

	val = snd_soc_component_read(component, e->reg);
	val >>= e->shift_l;
	val &= e->mask;
	if (snd_soc_enum_item_to_val(e, item) == val) {
		ret = 0;
		goto out;
	}

	if (!madera_can_change_grp_rate(priv, e->reg)) {
		dev_warn(priv->madera->dev,
			 "Cannot change '%s' while in use by active audio paths\n",
			 kcontrol->id.name);
		ret = -EBUSY;
	} else {
		/* The write must be guarded by a number of SYSCLK cycles */
		madera_spin_sysclk(priv);
		ret = snd_soc_put_enum_double(kcontrol, ucontrol);
		madera_spin_sysclk(priv);
	}
out:
	mutex_unlock(&priv->rate_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_rate_put);

static void madera_configure_input_mode(struct madera *madera)
{
	unsigned int dig_mode, ana_mode_l, ana_mode_r;
	int max_analogue_inputs, max_dmic_sup, i;

	switch (madera->type) {
	case CS47L15:
		max_analogue_inputs = 1;
		max_dmic_sup = 2;
		break;
	case CS47L35:
		max_analogue_inputs = 2;
		max_dmic_sup = 2;
		break;
	case CS47L85:
	case WM1840:
		max_analogue_inputs = 3;
		max_dmic_sup = 3;
		break;
	case CS47L90:
	case CS47L91:
		max_analogue_inputs = 2;
		max_dmic_sup = 2;
		break;
	default:
		max_analogue_inputs = 2;
		max_dmic_sup = 4;
		break;
	}

	/*
	 * Initialize input modes from the A settings. For muxed inputs the
	 * B settings will be applied if the mux is changed
	 */
	for (i = 0; i < max_dmic_sup; i++) {
		dev_dbg(madera->dev, "IN%d mode %u:%u:%u:%u\n", i + 1,
			madera->pdata.codec.inmode[i][0],
			madera->pdata.codec.inmode[i][1],
			madera->pdata.codec.inmode[i][2],
			madera->pdata.codec.inmode[i][3]);

		dig_mode = madera->pdata.codec.dmic_ref[i] <<
			   MADERA_IN1_DMIC_SUP_SHIFT;

		switch (madera->pdata.codec.inmode[i][0]) {
		case MADERA_INMODE_DIFF:
			ana_mode_l = 0;
			break;
		case MADERA_INMODE_SE:
			ana_mode_l = 1 << MADERA_IN1L_SRC_SE_SHIFT;
			break;
		default:
			dev_warn(madera->dev,
				 "IN%dAL Illegal inmode %u ignored\n",
				 i + 1, madera->pdata.codec.inmode[i][0]);
			continue;
		}

		switch (madera->pdata.codec.inmode[i][1]) {
		case MADERA_INMODE_DIFF:
			ana_mode_r = 0;
			break;
		case MADERA_INMODE_SE:
			ana_mode_r = 1 << MADERA_IN1R_SRC_SE_SHIFT;
			break;
		default:
			dev_warn(madera->dev,
				 "IN%dAR Illegal inmode %u ignored\n",
				 i + 1, madera->pdata.codec.inmode[i][1]);
			continue;
		}

		dev_dbg(madera->dev,
			"IN%dA DMIC mode=0x%x Analogue mode=0x%x,0x%x\n",
			i + 1, dig_mode, ana_mode_l, ana_mode_r);

		regmap_update_bits(madera->regmap,
				   MADERA_IN1L_CONTROL + (i * 8),
				   MADERA_IN1_DMIC_SUP_MASK, dig_mode);

		if (i >= max_analogue_inputs)
			continue;

		regmap_update_bits(madera->regmap,
				   MADERA_ADC_DIGITAL_VOLUME_1L + (i * 8),
				   MADERA_IN1L_SRC_SE_MASK, ana_mode_l);

		regmap_update_bits(madera->regmap,
				   MADERA_ADC_DIGITAL_VOLUME_1R + (i * 8),
				   MADERA_IN1R_SRC_SE_MASK, ana_mode_r);
	}
}

int madera_init_inputs(struct snd_soc_component *component)
{
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;

	madera_configure_input_mode(madera);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_inputs);

static const struct snd_soc_dapm_route madera_mono_routes[] = {
	{ "OUT1R", NULL, "OUT1L" },
	{ "OUT2R", NULL, "OUT2L" },
	{ "OUT3R", NULL, "OUT3L" },
	{ "OUT4R", NULL, "OUT4L" },
	{ "OUT5R", NULL, "OUT5L" },
	{ "OUT6R", NULL, "OUT6L" },
};

int madera_init_outputs(struct snd_soc_component *component,
			const struct snd_soc_dapm_route *routes,
			int n_mono_routes, int n_real)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	const struct madera_codec_pdata *pdata = &madera->pdata.codec;
	unsigned int val;
	int i;

	if (n_mono_routes > MADERA_MAX_OUTPUT) {
		dev_warn(madera->dev,
			 "Requested %d mono outputs, using maximum allowed %d\n",
			 n_mono_routes, MADERA_MAX_OUTPUT);
		n_mono_routes = MADERA_MAX_OUTPUT;
	}

	if (!routes)
		routes = madera_mono_routes;

	for (i = 0; i < n_mono_routes; i++) {
		/* Default is 0 so noop with defaults */
		if (pdata->out_mono[i]) {
			val = MADERA_OUT1_MONO;
			snd_soc_dapm_add_routes(dapm, &routes[i], 1);
		} else {
			val = 0;
		}

		if (i >= n_real)
			continue;

		regmap_update_bits(madera->regmap,
				   MADERA_OUTPUT_PATH_CONFIG_1L + (i * 8),
				   MADERA_OUT1_MONO, val);

		dev_dbg(madera->dev, "OUT%d mono=0x%x\n", i + 1, val);
	}

	for (i = 0; i < MADERA_MAX_PDM_SPK; i++) {
		dev_dbg(madera->dev, "PDM%d fmt=0x%x mute=0x%x\n", i + 1,
			pdata->pdm_fmt[i], pdata->pdm_mute[i]);

		if (pdata->pdm_mute[i])
			regmap_update_bits(madera->regmap,
					   MADERA_PDM_SPK1_CTRL_1 + (i * 2),
					   MADERA_SPK1_MUTE_ENDIAN_MASK |
					   MADERA_SPK1_MUTE_SEQ1_MASK,
					   pdata->pdm_mute[i]);

		if (pdata->pdm_fmt[i])
			regmap_update_bits(madera->regmap,
					   MADERA_PDM_SPK1_CTRL_2 + (i * 2),
					   MADERA_SPK1_FMT_MASK,
					   pdata->pdm_fmt[i]);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_outputs);

int madera_init_bus_error_irq(struct madera_priv *priv, int dsp_num,
			      irq_handler_t handler)
{
	struct madera *madera = priv->madera;
	int ret;

	ret = madera_request_irq(madera,
				 madera_dsp_bus_error_irqs[dsp_num],
				 "ADSP2 bus error",
				 handler,
				 &priv->adsp[dsp_num]);
	if (ret)
		dev_err(madera->dev,
			"Failed to request DSP Lock region IRQ: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_init_bus_error_irq);

void madera_free_bus_error_irq(struct madera_priv *priv, int dsp_num)
{
	struct madera *madera = priv->madera;

	madera_free_irq(madera,
			madera_dsp_bus_error_irqs[dsp_num],
			&priv->adsp[dsp_num]);
}
EXPORT_SYMBOL_GPL(madera_free_bus_error_irq);

const char * const madera_mixer_texts[] = {
	"None",
	"Tone Generator 1",
	"Tone Generator 2",
	"Haptics",
	"AEC1",
	"AEC2",
	"Mic Mute Mixer",
	"Noise Generator",
	"IN1L",
	"IN1R",
	"IN2L",
	"IN2R",
	"IN3L",
	"IN3R",
	"IN4L",
	"IN4R",
	"IN5L",
	"IN5R",
	"IN6L",
	"IN6R",
	"AIF1RX1",
	"AIF1RX2",
	"AIF1RX3",
	"AIF1RX4",
	"AIF1RX5",
	"AIF1RX6",
	"AIF1RX7",
	"AIF1RX8",
	"AIF2RX1",
	"AIF2RX2",
	"AIF2RX3",
	"AIF2RX4",
	"AIF2RX5",
	"AIF2RX6",
	"AIF2RX7",
	"AIF2RX8",
	"AIF3RX1",
	"AIF3RX2",
	"AIF3RX3",
	"AIF3RX4",
	"AIF4RX1",
	"AIF4RX2",
	"SLIMRX1",
	"SLIMRX2",
	"SLIMRX3",
	"SLIMRX4",
	"SLIMRX5",
	"SLIMRX6",
	"SLIMRX7",
	"SLIMRX8",
	"EQ1",
	"EQ2",
	"EQ3",
	"EQ4",
	"DRC1L",
	"DRC1R",
	"DRC2L",
	"DRC2R",
	"LHPF1",
	"LHPF2",
	"LHPF3",
	"LHPF4",
	"DSP1.1",
	"DSP1.2",
	"DSP1.3",
	"DSP1.4",
	"DSP1.5",
	"DSP1.6",
	"DSP2.1",
	"DSP2.2",
	"DSP2.3",
	"DSP2.4",
	"DSP2.5",
	"DSP2.6",
	"DSP3.1",
	"DSP3.2",
	"DSP3.3",
	"DSP3.4",
	"DSP3.5",
	"DSP3.6",
	"DSP4.1",
	"DSP4.2",
	"DSP4.3",
	"DSP4.4",
	"DSP4.5",
	"DSP4.6",
	"DSP5.1",
	"DSP5.2",
	"DSP5.3",
	"DSP5.4",
	"DSP5.5",
	"DSP5.6",
	"DSP6.1",
	"DSP6.2",
	"DSP6.3",
	"DSP6.4",
	"DSP6.5",
	"DSP6.6",
	"DSP7.1",
	"DSP7.2",
	"DSP7.3",
	"DSP7.4",
	"DSP7.5",
	"DSP7.6",
	"ASRC1IN1L",
	"ASRC1IN1R",
	"ASRC1IN2L",
	"ASRC1IN2R",
	"ASRC2IN1L",
	"ASRC2IN1R",
	"ASRC2IN2L",
	"ASRC2IN2R",
	"ISRC1INT1",
	"ISRC1INT2",
	"ISRC1INT3",
	"ISRC1INT4",
	"ISRC1DEC1",
	"ISRC1DEC2",
	"ISRC1DEC3",
	"ISRC1DEC4",
	"ISRC2INT1",
	"ISRC2INT2",
	"ISRC2INT3",
	"ISRC2INT4",
	"ISRC2DEC1",
	"ISRC2DEC2",
	"ISRC2DEC3",
	"ISRC2DEC4",
	"ISRC3INT1",
	"ISRC3INT2",
	"ISRC3INT3",
	"ISRC3INT4",
	"ISRC3DEC1",
	"ISRC3DEC2",
	"ISRC3DEC3",
	"ISRC3DEC4",
	"ISRC4INT1",
	"ISRC4INT2",
	"ISRC4DEC1",
	"ISRC4DEC2",
	"DFC1",
	"DFC2",
	"DFC3",
	"DFC4",
	"DFC5",
	"DFC6",
	"DFC7",
	"DFC8",
};
EXPORT_SYMBOL_GPL(madera_mixer_texts);

const unsigned int madera_mixer_values[] = {
	0x00,	/* None */
	0x04,	/* Tone Generator 1 */
	0x05,	/* Tone Generator 2 */
	0x06,	/* Haptics */
	0x08,	/* AEC */
	0x09,	/* AEC2 */
	0x0c,	/* Noise mixer */
	0x0d,	/* Comfort noise */
	0x10,	/* IN1L */
	0x11,
	0x12,
	0x13,
	0x14,
	0x15,
	0x16,
	0x17,
	0x18,
	0x19,
	0x1A,
	0x1B,
	0x20,	/* AIF1RX1 */
	0x21,
	0x22,
	0x23,
	0x24,
	0x25,
	0x26,
	0x27,
	0x28,	/* AIF2RX1 */
	0x29,
	0x2a,
	0x2b,
	0x2c,
	0x2d,
	0x2e,
	0x2f,
	0x30,	/* AIF3RX1 */
	0x31,
	0x32,
	0x33,
	0x34,	/* AIF4RX1 */
	0x35,
	0x38,	/* SLIMRX1 */
	0x39,
	0x3a,
	0x3b,
	0x3c,
	0x3d,
	0x3e,
	0x3f,
	0x50,	/* EQ1 */
	0x51,
	0x52,
	0x53,
	0x58,	/* DRC1L */
	0x59,
	0x5a,
	0x5b,
	0x60,	/* LHPF1 */
	0x61,
	0x62,
	0x63,
	0x68,	/* DSP1.1 */
	0x69,
	0x6a,
	0x6b,
	0x6c,
	0x6d,
	0x70,	/* DSP2.1 */
	0x71,
	0x72,
	0x73,
	0x74,
	0x75,
	0x78,	/* DSP3.1 */
	0x79,
	0x7a,
	0x7b,
	0x7c,
	0x7d,
	0x80,	/* DSP4.1 */
	0x81,
	0x82,
	0x83,
	0x84,
	0x85,
	0x88,	/* DSP5.1 */
	0x89,
	0x8a,
	0x8b,
	0x8c,
	0x8d,
	0xc0,	/* DSP6.1 */
	0xc1,
	0xc2,
	0xc3,
	0xc4,
	0xc5,
	0xc8,	/* DSP7.1 */
	0xc9,
	0xca,
	0xcb,
	0xcc,
	0xcd,
	0x90,	/* ASRC1IN1L */
	0x91,
	0x92,
	0x93,
	0x94,	/* ASRC2IN1L */
	0x95,
	0x96,
	0x97,
	0xa0,	/* ISRC1INT1 */
	0xa1,
	0xa2,
	0xa3,
	0xa4,	/* ISRC1DEC1 */
	0xa5,
	0xa6,
	0xa7,
	0xa8,	/* ISRC2DEC1 */
	0xa9,
	0xaa,
	0xab,
	0xac,	/* ISRC2INT1 */
	0xad,
	0xae,
	0xaf,
	0xb0,	/* ISRC3DEC1 */
	0xb1,
	0xb2,
	0xb3,
	0xb4,	/* ISRC3INT1 */
	0xb5,
	0xb6,
	0xb7,
	0xb8,	/* ISRC4INT1 */
	0xb9,
	0xbc,	/* ISRC4DEC1 */
	0xbd,
	0xf8,	/* DFC1 */
	0xf9,
	0xfa,
	0xfb,
	0xfc,
	0xfd,
	0xfe,
	0xff,	/* DFC8 */
};
EXPORT_SYMBOL_GPL(madera_mixer_values);

const DECLARE_TLV_DB_SCALE(madera_ana_tlv, 0, 100, 0);
EXPORT_SYMBOL_GPL(madera_ana_tlv);

const DECLARE_TLV_DB_SCALE(madera_eq_tlv, -1200, 100, 0);
EXPORT_SYMBOL_GPL(madera_eq_tlv);

const DECLARE_TLV_DB_SCALE(madera_digital_tlv, -6400, 50, 0);
EXPORT_SYMBOL_GPL(madera_digital_tlv);

const DECLARE_TLV_DB_SCALE(madera_noise_tlv, -13200, 600, 0);
EXPORT_SYMBOL_GPL(madera_noise_tlv);

const DECLARE_TLV_DB_SCALE(madera_ng_tlv, -12000, 600, 0);
EXPORT_SYMBOL_GPL(madera_ng_tlv);

const DECLARE_TLV_DB_SCALE(madera_mixer_tlv, -3200, 100, 0);
EXPORT_SYMBOL_GPL(madera_mixer_tlv);

const char * const madera_rate_text[MADERA_RATE_ENUM_SIZE] = {
	"SYNCCLK rate 1", "SYNCCLK rate 2", "SYNCCLK rate 3",
	"ASYNCCLK rate 1", "ASYNCCLK rate 2",
};
EXPORT_SYMBOL_GPL(madera_rate_text);

const unsigned int madera_rate_val[MADERA_RATE_ENUM_SIZE] = {
	0x0, 0x1, 0x2, 0x8, 0x9,
};
EXPORT_SYMBOL_GPL(madera_rate_val);

static const char * const madera_dfc_width_text[MADERA_DFC_WIDTH_ENUM_SIZE] = {
	"8 bit", "16 bit", "20 bit", "24 bit", "32 bit",
};

static const unsigned int madera_dfc_width_val[MADERA_DFC_WIDTH_ENUM_SIZE] = {
	7, 15, 19, 23, 31,
};

static const char * const madera_dfc_type_text[MADERA_DFC_TYPE_ENUM_SIZE] = {
	"Fixed", "Unsigned Fixed", "Single Precision Floating",
	"Half Precision Floating", "Arm Alternative Floating",
};

static const unsigned int madera_dfc_type_val[MADERA_DFC_TYPE_ENUM_SIZE] = {
	0, 1, 2, 4, 5,
};

const struct soc_enum madera_dfc_width[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC1_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC1_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC2_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC2_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC3_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC3_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC4_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC4_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC5_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC5_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC6_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC6_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC7_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC7_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC8_RX,
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_RX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_RX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC8_TX,
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      MADERA_DFC1_TX_DATA_WIDTH_MASK >>
			      MADERA_DFC1_TX_DATA_WIDTH_SHIFT,
			      ARRAY_SIZE(madera_dfc_width_text),
			      madera_dfc_width_text,
			      madera_dfc_width_val),
};
EXPORT_SYMBOL_GPL(madera_dfc_width);

const struct soc_enum madera_dfc_type[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC1_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC1_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC2_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC2_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC3_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC3_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC4_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC4_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC5_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC5_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC6_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC6_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC7_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC7_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC8_RX,
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_RX_DATA_TYPE_MASK >>
			      MADERA_DFC1_RX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DFC8_TX,
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      MADERA_DFC1_TX_DATA_TYPE_MASK >>
			      MADERA_DFC1_TX_DATA_TYPE_SHIFT,
			      ARRAY_SIZE(madera_dfc_type_text),
			      madera_dfc_type_text,
			      madera_dfc_type_val),
};
EXPORT_SYMBOL_GPL(madera_dfc_type);

const struct soc_enum madera_isrc_fsh[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_1_CTRL_1,
			      MADERA_ISRC1_FSH_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_2_CTRL_1,
			      MADERA_ISRC2_FSH_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_3_CTRL_1,
			      MADERA_ISRC3_FSH_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_4_CTRL_1,
			      MADERA_ISRC4_FSH_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
};
EXPORT_SYMBOL_GPL(madera_isrc_fsh);

const struct soc_enum madera_isrc_fsl[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_1_CTRL_2,
			      MADERA_ISRC1_FSL_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_2_CTRL_2,
			      MADERA_ISRC2_FSL_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_3_CTRL_2,
			      MADERA_ISRC3_FSL_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ISRC_4_CTRL_2,
			      MADERA_ISRC4_FSL_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
};
EXPORT_SYMBOL_GPL(madera_isrc_fsl);

const struct soc_enum madera_asrc1_rate[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC1_RATE1,
			      MADERA_ASRC1_RATE1_SHIFT, 0xf,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC1_RATE2,
			      MADERA_ASRC1_RATE1_SHIFT, 0xf,
			      MADERA_ASYNC_RATE_ENUM_SIZE,
			      madera_rate_text + MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_val + MADERA_SYNC_RATE_ENUM_SIZE),
};
EXPORT_SYMBOL_GPL(madera_asrc1_rate);

const struct soc_enum madera_asrc1_bidir_rate[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC1_RATE1,
			      MADERA_ASRC1_RATE1_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC1_RATE2,
			      MADERA_ASRC1_RATE2_SHIFT, 0xf,
			      MADERA_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
};
EXPORT_SYMBOL_GPL(madera_asrc1_bidir_rate);

const struct soc_enum madera_asrc2_rate[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC2_RATE1,
			      MADERA_ASRC2_RATE1_SHIFT, 0xf,
			      MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_text, madera_rate_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_ASRC2_RATE2,
			      MADERA_ASRC2_RATE2_SHIFT, 0xf,
			      MADERA_ASYNC_RATE_ENUM_SIZE,
			      madera_rate_text + MADERA_SYNC_RATE_ENUM_SIZE,
			      madera_rate_val + MADERA_SYNC_RATE_ENUM_SIZE),
};
EXPORT_SYMBOL_GPL(madera_asrc2_rate);

static const char * const madera_vol_ramp_text[] = {
	"0ms/6dB", "0.5ms/6dB", "1ms/6dB", "2ms/6dB", "4ms/6dB", "8ms/6dB",
	"15ms/6dB", "30ms/6dB",
};

SOC_ENUM_SINGLE_DECL(madera_in_vd_ramp,
		     MADERA_INPUT_VOLUME_RAMP,
		     MADERA_IN_VD_RAMP_SHIFT,
		     madera_vol_ramp_text);
EXPORT_SYMBOL_GPL(madera_in_vd_ramp);

SOC_ENUM_SINGLE_DECL(madera_in_vi_ramp,
		     MADERA_INPUT_VOLUME_RAMP,
		     MADERA_IN_VI_RAMP_SHIFT,
		     madera_vol_ramp_text);
EXPORT_SYMBOL_GPL(madera_in_vi_ramp);

SOC_ENUM_SINGLE_DECL(madera_out_vd_ramp,
		     MADERA_OUTPUT_VOLUME_RAMP,
		     MADERA_OUT_VD_RAMP_SHIFT,
		     madera_vol_ramp_text);
EXPORT_SYMBOL_GPL(madera_out_vd_ramp);

SOC_ENUM_SINGLE_DECL(madera_out_vi_ramp,
		     MADERA_OUTPUT_VOLUME_RAMP,
		     MADERA_OUT_VI_RAMP_SHIFT,
		     madera_vol_ramp_text);
EXPORT_SYMBOL_GPL(madera_out_vi_ramp);

static const char * const madera_lhpf_mode_text[] = {
	"Low-pass", "High-pass"
};

SOC_ENUM_SINGLE_DECL(madera_lhpf1_mode,
		     MADERA_HPLPF1_1,
		     MADERA_LHPF1_MODE_SHIFT,
		     madera_lhpf_mode_text);
EXPORT_SYMBOL_GPL(madera_lhpf1_mode);

SOC_ENUM_SINGLE_DECL(madera_lhpf2_mode,
		     MADERA_HPLPF2_1,
		     MADERA_LHPF2_MODE_SHIFT,
		     madera_lhpf_mode_text);
EXPORT_SYMBOL_GPL(madera_lhpf2_mode);

SOC_ENUM_SINGLE_DECL(madera_lhpf3_mode,
		     MADERA_HPLPF3_1,
		     MADERA_LHPF3_MODE_SHIFT,
		     madera_lhpf_mode_text);
EXPORT_SYMBOL_GPL(madera_lhpf3_mode);

SOC_ENUM_SINGLE_DECL(madera_lhpf4_mode,
		     MADERA_HPLPF4_1,
		     MADERA_LHPF4_MODE_SHIFT,
		     madera_lhpf_mode_text);
EXPORT_SYMBOL_GPL(madera_lhpf4_mode);

static const char * const madera_ng_hold_text[] = {
	"30ms", "120ms", "250ms", "500ms",
};

SOC_ENUM_SINGLE_DECL(madera_ng_hold,
		     MADERA_NOISE_GATE_CONTROL,
		     MADERA_NGATE_HOLD_SHIFT,
		     madera_ng_hold_text);
EXPORT_SYMBOL_GPL(madera_ng_hold);

static const char * const madera_in_hpf_cut_text[] = {
	"2.5Hz", "5Hz", "10Hz", "20Hz", "40Hz"
};

SOC_ENUM_SINGLE_DECL(madera_in_hpf_cut_enum,
		     MADERA_HPF_CONTROL,
		     MADERA_IN_HPF_CUT_SHIFT,
		     madera_in_hpf_cut_text);
EXPORT_SYMBOL_GPL(madera_in_hpf_cut_enum);

static const char * const madera_in_dmic_osr_text[MADERA_OSR_ENUM_SIZE] = {
	"384kHz", "768kHz", "1.536MHz", "3.072MHz", "6.144MHz",
};

static const unsigned int madera_in_dmic_osr_val[MADERA_OSR_ENUM_SIZE] = {
	2, 3, 4, 5, 6,
};

const struct soc_enum madera_in_dmic_osr[] = {
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC1L_CONTROL, MADERA_IN1_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC2L_CONTROL, MADERA_IN2_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC3L_CONTROL, MADERA_IN3_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC4L_CONTROL, MADERA_IN4_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC5L_CONTROL, MADERA_IN5_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
	SOC_VALUE_ENUM_SINGLE(MADERA_DMIC6L_CONTROL, MADERA_IN6_OSR_SHIFT,
			      0x7, MADERA_OSR_ENUM_SIZE,
			      madera_in_dmic_osr_text, madera_in_dmic_osr_val),
};
EXPORT_SYMBOL_GPL(madera_in_dmic_osr);

static const char * const madera_anc_input_src_text[] = {
	"None", "IN1", "IN2", "IN3", "IN4", "IN5", "IN6",
};

static const char * const madera_anc_channel_src_text[] = {
	"None", "Left", "Right", "Combine",
};

const struct soc_enum madera_anc_input_src[] = {
	SOC_ENUM_SINGLE(MADERA_ANC_SRC,
			MADERA_IN_RXANCL_SEL_SHIFT,
			ARRAY_SIZE(madera_anc_input_src_text),
			madera_anc_input_src_text),
	SOC_ENUM_SINGLE(MADERA_FCL_ADC_REFORMATTER_CONTROL,
			MADERA_FCL_MIC_MODE_SEL_SHIFT,
			ARRAY_SIZE(madera_anc_channel_src_text),
			madera_anc_channel_src_text),
	SOC_ENUM_SINGLE(MADERA_ANC_SRC,
			MADERA_IN_RXANCR_SEL_SHIFT,
			ARRAY_SIZE(madera_anc_input_src_text),
			madera_anc_input_src_text),
	SOC_ENUM_SINGLE(MADERA_FCR_ADC_REFORMATTER_CONTROL,
			MADERA_FCR_MIC_MODE_SEL_SHIFT,
			ARRAY_SIZE(madera_anc_channel_src_text),
			madera_anc_channel_src_text),
};
EXPORT_SYMBOL_GPL(madera_anc_input_src);

static const char * const madera_anc_ng_texts[] = {
	"None", "Internal", "External",
};

SOC_ENUM_SINGLE_DECL(madera_anc_ng_enum, SND_SOC_NOPM, 0, madera_anc_ng_texts);
EXPORT_SYMBOL_GPL(madera_anc_ng_enum);

static const char * const madera_out_anc_src_text[] = {
	"None", "RXANCL", "RXANCR",
};

const struct soc_enum madera_output_anc_src[] = {
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_1L,
			MADERA_OUT1L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_1R,
			MADERA_OUT1R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_2L,
			MADERA_OUT2L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_2R,
			MADERA_OUT2R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_3L,
			MADERA_OUT3L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_3R,
			MADERA_OUT3R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_4L,
			MADERA_OUT4L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_4R,
			MADERA_OUT4R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_5L,
			MADERA_OUT5L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_5R,
			MADERA_OUT5R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_6L,
			MADERA_OUT6L_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
	SOC_ENUM_SINGLE(MADERA_OUTPUT_PATH_CONFIG_6R,
			MADERA_OUT6R_ANC_SRC_SHIFT,
			ARRAY_SIZE(madera_out_anc_src_text),
			madera_out_anc_src_text),
};
EXPORT_SYMBOL_GPL(madera_output_anc_src);

int madera_dfc_put(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg = e->reg;
	unsigned int val;
	int ret = 0;

	reg = ((reg / 6) * 6) - 2;

	snd_soc_dapm_mutex_lock(dapm);

	val = snd_soc_component_read(component, reg);
	if (val & MADERA_DFC1_ENA) {
		ret = -EBUSY;
		dev_err(component->dev, "Can't change mode on an active DFC\n");
		goto exit;
	}

	ret = snd_soc_put_enum_double(kcontrol, ucontrol);
exit:
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_dfc_put);

int madera_lp_mode_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	unsigned int val, mask;
	int ret;

	snd_soc_dapm_mutex_lock(dapm);

	/* Cannot change lp mode on an active input */
	val = snd_soc_component_read(component, MADERA_INPUT_ENABLES);
	mask = (mc->reg - MADERA_ADC_DIGITAL_VOLUME_1L) / 4;
	mask ^= 0x1; /* Flip bottom bit for channel order */

	if (val & (1 << mask)) {
		ret = -EBUSY;
		dev_err(component->dev,
			"Can't change lp mode on an active input\n");
		goto exit;
	}

	ret = snd_soc_put_volsw(kcontrol, ucontrol);

exit:
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_lp_mode_put);

const struct snd_kcontrol_new madera_dsp_trigger_output_mux[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
};
EXPORT_SYMBOL_GPL(madera_dsp_trigger_output_mux);

const struct snd_kcontrol_new madera_drc_activity_output_mux[] = {
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0),
};
EXPORT_SYMBOL_GPL(madera_drc_activity_output_mux);

static void madera_in_set_vu(struct madera_priv *priv, bool enable)
{
	unsigned int val;
	int i, ret;

	if (enable)
		val = MADERA_IN_VU;
	else
		val = 0;

	for (i = 0; i < priv->num_inputs; i++) {
		ret = regmap_update_bits(priv->madera->regmap,
					 MADERA_ADC_DIGITAL_VOLUME_1L + (i * 4),
					 MADERA_IN_VU, val);
		if (ret)
			dev_warn(priv->madera->dev,
				 "Failed to modify VU bits: %d\n", ret);
	}
}

int madera_in_ev(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
		 int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	unsigned int reg, val;

	if (w->shift % 2)
		reg = MADERA_ADC_DIGITAL_VOLUME_1L + ((w->shift / 2) * 8);
	else
		reg = MADERA_ADC_DIGITAL_VOLUME_1R + ((w->shift / 2) * 8);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		priv->in_pending++;
		break;
	case SND_SOC_DAPM_POST_PMU:
		priv->in_pending--;
		snd_soc_component_update_bits(component, reg,
					      MADERA_IN1L_MUTE, 0);

		/* If this is the last input pending then allow VU */
		if (priv->in_pending == 0) {
			usleep_range(1000, 3000);
			madera_in_set_vu(priv, true);
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, reg,
					      MADERA_IN1L_MUTE | MADERA_IN_VU,
					      MADERA_IN1L_MUTE | MADERA_IN_VU);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* Disable volume updates if no inputs are enabled */
		val = snd_soc_component_read(component, MADERA_INPUT_ENABLES);
		if (!val)
			madera_in_set_vu(priv, false);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(madera_in_ev);

int madera_out_ev(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	int out_up_delay;

	switch (madera->type) {
	case CS47L90:
	case CS47L91:
	case CS42L92:
	case CS47L92:
	case CS47L93:
		out_up_delay = 6;
		break;
	default:
		out_up_delay = 17;
		break;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (w->shift) {
		case MADERA_OUT1L_ENA_SHIFT:
		case MADERA_OUT1R_ENA_SHIFT:
		case MADERA_OUT2L_ENA_SHIFT:
		case MADERA_OUT2R_ENA_SHIFT:
		case MADERA_OUT3L_ENA_SHIFT:
		case MADERA_OUT3R_ENA_SHIFT:
			priv->out_up_pending++;
			priv->out_up_delay += out_up_delay;
			break;
		default:
			break;
		}
		break;

	case SND_SOC_DAPM_POST_PMU:
		switch (w->shift) {
		case MADERA_OUT1L_ENA_SHIFT:
		case MADERA_OUT1R_ENA_SHIFT:
		case MADERA_OUT2L_ENA_SHIFT:
		case MADERA_OUT2R_ENA_SHIFT:
		case MADERA_OUT3L_ENA_SHIFT:
		case MADERA_OUT3R_ENA_SHIFT:
			priv->out_up_pending--;
			if (!priv->out_up_pending) {
				msleep(priv->out_up_delay);
				priv->out_up_delay = 0;
			}
			break;

		default:
			break;
		}
		break;

	case SND_SOC_DAPM_PRE_PMD:
		switch (w->shift) {
		case MADERA_OUT1L_ENA_SHIFT:
		case MADERA_OUT1R_ENA_SHIFT:
		case MADERA_OUT2L_ENA_SHIFT:
		case MADERA_OUT2R_ENA_SHIFT:
		case MADERA_OUT3L_ENA_SHIFT:
		case MADERA_OUT3R_ENA_SHIFT:
			priv->out_down_pending++;
			priv->out_down_delay++;
			break;
		default:
			break;
		}
		break;

	case SND_SOC_DAPM_POST_PMD:
		switch (w->shift) {
		case MADERA_OUT1L_ENA_SHIFT:
		case MADERA_OUT1R_ENA_SHIFT:
		case MADERA_OUT2L_ENA_SHIFT:
		case MADERA_OUT2R_ENA_SHIFT:
		case MADERA_OUT3L_ENA_SHIFT:
		case MADERA_OUT3R_ENA_SHIFT:
			priv->out_down_pending--;
			if (!priv->out_down_pending) {
				msleep(priv->out_down_delay);
				priv->out_down_delay = 0;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(madera_out_ev);

int madera_hp_ev(struct snd_soc_dapm_widget *w,
		 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	unsigned int mask = 1 << w->shift;
	unsigned int out_num = w->shift / 2;
	unsigned int val;
	unsigned int ep_sel = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = mask;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val = 0;
		break;
	case SND_SOC_DAPM_PRE_PMU:
	case SND_SOC_DAPM_POST_PMD:
		return madera_out_ev(w, kcontrol, event);
	default:
		return 0;
	}

	/* Store the desired state for the HP outputs */
	madera->hp_ena &= ~mask;
	madera->hp_ena |= val;

	switch (madera->type) {
	case CS42L92:
	case CS47L92:
	case CS47L93:
		break;
	default:
		/* if OUT1 is routed to EPOUT, ignore HP clamp and impedance */
		regmap_read(madera->regmap, MADERA_OUTPUT_ENABLES_1, &ep_sel);
		ep_sel &= MADERA_EP_SEL_MASK;
		break;
	}

	/* Force off if HPDET has disabled the clamp for this output */
	if (!ep_sel &&
	    (!madera->out_clamp[out_num] || madera->out_shorted[out_num]))
		val = 0;

	regmap_update_bits(madera->regmap, MADERA_OUTPUT_ENABLES_1, mask, val);

	return madera_out_ev(w, kcontrol, event);
}
EXPORT_SYMBOL_GPL(madera_hp_ev);

int madera_anc_ev(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol,
		  int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	unsigned int val;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = 1 << w->shift;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val = 1 << (w->shift + 1);
		break;
	default:
		return 0;
	}

	snd_soc_component_write(component, MADERA_CLOCK_CONTROL, val);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_anc_ev);

static const unsigned int madera_opclk_ref_48k_rates[] = {
	6144000,
	12288000,
	24576000,
	49152000,
};

static const unsigned int madera_opclk_ref_44k1_rates[] = {
	5644800,
	11289600,
	22579200,
	45158400,
};

static int madera_set_opclk(struct snd_soc_component *component,
			    unsigned int clk, unsigned int freq)
{
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	unsigned int mask = MADERA_OPCLK_DIV_MASK | MADERA_OPCLK_SEL_MASK;
	unsigned int reg, val;
	const unsigned int *rates;
	int ref, div, refclk;

	BUILD_BUG_ON(ARRAY_SIZE(madera_opclk_ref_48k_rates) !=
		     ARRAY_SIZE(madera_opclk_ref_44k1_rates));

	switch (clk) {
	case MADERA_CLK_OPCLK:
		reg = MADERA_OUTPUT_SYSTEM_CLOCK;
		refclk = priv->sysclk;
		break;
	case MADERA_CLK_ASYNC_OPCLK:
		reg = MADERA_OUTPUT_ASYNC_CLOCK;
		refclk = priv->asyncclk;
		break;
	default:
		return -EINVAL;
	}

	if (refclk % 4000)
		rates = madera_opclk_ref_44k1_rates;
	else
		rates = madera_opclk_ref_48k_rates;

	for (ref = 0; ref < ARRAY_SIZE(madera_opclk_ref_48k_rates); ++ref) {
		if (rates[ref] > refclk)
			continue;

		div = 2;
		while ((rates[ref] / div >= freq) && (div <= 30)) {
			if (rates[ref] / div == freq) {
				dev_dbg(component->dev, "Configured %dHz OPCLK\n",
					freq);

				val = (div << MADERA_OPCLK_DIV_SHIFT) | ref;

				snd_soc_component_update_bits(component, reg,
							      mask, val);
				return 0;
			}
			div += 2;
		}
	}

	dev_err(component->dev, "Unable to generate %dHz OPCLK\n", freq);

	return -EINVAL;
}

static int madera_get_sysclk_setting(unsigned int freq)
{
	switch (freq) {
	case 0:
	case 5644800:
	case 6144000:
		return 0;
	case 11289600:
	case 12288000:
		return MADERA_SYSCLK_12MHZ << MADERA_SYSCLK_FREQ_SHIFT;
	case 22579200:
	case 24576000:
		return MADERA_SYSCLK_24MHZ << MADERA_SYSCLK_FREQ_SHIFT;
	case 45158400:
	case 49152000:
		return MADERA_SYSCLK_49MHZ << MADERA_SYSCLK_FREQ_SHIFT;
	case 90316800:
	case 98304000:
		return MADERA_SYSCLK_98MHZ << MADERA_SYSCLK_FREQ_SHIFT;
	default:
		return -EINVAL;
	}
}

static int madera_get_legacy_dspclk_setting(struct madera *madera,
					    unsigned int freq)
{
	switch (freq) {
	case 0:
		return 0;
	case 45158400:
	case 49152000:
		switch (madera->type) {
		case CS47L85:
		case WM1840:
			if (madera->rev < 3)
				return -EINVAL;
			else
				return MADERA_SYSCLK_49MHZ <<
				       MADERA_SYSCLK_FREQ_SHIFT;
		default:
			return -EINVAL;
		}
	case 135475200:
	case 147456000:
		return MADERA_DSPCLK_147MHZ << MADERA_DSP_CLK_FREQ_LEGACY_SHIFT;
	default:
		return -EINVAL;
	}
}

static int madera_get_dspclk_setting(struct madera *madera,
				     unsigned int freq,
				     unsigned int *clock_2_val)
{
	switch (madera->type) {
	case CS47L35:
	case CS47L85:
	case WM1840:
		*clock_2_val = 0; /* don't use MADERA_DSP_CLOCK_2 */
		return madera_get_legacy_dspclk_setting(madera, freq);
	default:
		if (freq > 150000000)
			return -EINVAL;

		/* Use new exact frequency control */
		*clock_2_val = freq / 15625; /* freq * (2^6) / (10^6) */
		return 0;
	}
}

static int madera_set_outclk(struct snd_soc_component *component,
			     unsigned int source, unsigned int freq)
{
	int div, div_inc, rate;

	switch (source) {
	case MADERA_OUTCLK_SYSCLK:
		dev_dbg(component->dev, "Configured OUTCLK to SYSCLK\n");
		snd_soc_component_update_bits(component, MADERA_OUTPUT_RATE_1,
					      MADERA_OUT_CLK_SRC_MASK, source);
		return 0;
	case MADERA_OUTCLK_ASYNCCLK:
		dev_dbg(component->dev, "Configured OUTCLK to ASYNCCLK\n");
		snd_soc_component_update_bits(component, MADERA_OUTPUT_RATE_1,
					      MADERA_OUT_CLK_SRC_MASK, source);
		return 0;
	case MADERA_OUTCLK_MCLK1:
	case MADERA_OUTCLK_MCLK2:
	case MADERA_OUTCLK_MCLK3:
		break;
	default:
		return -EINVAL;
	}

	if (freq % 4000)
		rate = 5644800;
	else
		rate = 6144000;

	div = 1;
	div_inc = 0;
	while (div <= 8) {
		if (freq / div == rate && !(freq % div)) {
			dev_dbg(component->dev, "Configured %dHz OUTCLK\n", rate);
			snd_soc_component_update_bits(component,
				MADERA_OUTPUT_RATE_1,
				MADERA_OUT_EXT_CLK_DIV_MASK |
				MADERA_OUT_CLK_SRC_MASK,
				(div_inc << MADERA_OUT_EXT_CLK_DIV_SHIFT) |
				source);
			return 0;
		}
		div_inc++;
		div *= 2;
	}

	dev_err(component->dev,
		"Unable to generate %dHz OUTCLK from %dHz MCLK\n",
		rate, freq);
	return -EINVAL;
}

int madera_set_sysclk(struct snd_soc_component *component, int clk_id,
		      int source, unsigned int freq, int dir)
{
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	char *name;
	unsigned int reg, clock_2_val = 0;
	unsigned int mask = MADERA_SYSCLK_FREQ_MASK | MADERA_SYSCLK_SRC_MASK;
	unsigned int val = source << MADERA_SYSCLK_SRC_SHIFT;
	int clk_freq_sel, *clk;
	int ret = 0;

	switch (clk_id) {
	case MADERA_CLK_SYSCLK_1:
		name = "SYSCLK";
		reg = MADERA_SYSTEM_CLOCK_1;
		clk = &priv->sysclk;
		clk_freq_sel = madera_get_sysclk_setting(freq);
		mask |= MADERA_SYSCLK_FRAC;
		break;
	case MADERA_CLK_ASYNCCLK_1:
		name = "ASYNCCLK";
		reg = MADERA_ASYNC_CLOCK_1;
		clk = &priv->asyncclk;
		clk_freq_sel = madera_get_sysclk_setting(freq);
		break;
	case MADERA_CLK_DSPCLK:
		name = "DSPCLK";
		reg = MADERA_DSP_CLOCK_1;
		clk = &priv->dspclk;
		clk_freq_sel = madera_get_dspclk_setting(madera, freq,
							 &clock_2_val);
		break;
	case MADERA_CLK_OPCLK:
	case MADERA_CLK_ASYNC_OPCLK:
		return madera_set_opclk(component, clk_id, freq);
	case MADERA_CLK_OUTCLK:
		return madera_set_outclk(component, source, freq);
	default:
		return -EINVAL;
	}

	if (clk_freq_sel < 0) {
		dev_err(madera->dev,
			"Failed to get clk setting for %dHZ\n", freq);
		return clk_freq_sel;
	}

	*clk = freq;

	if (freq == 0) {
		dev_dbg(madera->dev, "%s cleared\n", name);
		return 0;
	}

	val |= clk_freq_sel;

	if (clock_2_val) {
		ret = regmap_write(madera->regmap, MADERA_DSP_CLOCK_2,
				   clock_2_val);
		if (ret) {
			dev_err(madera->dev,
				"Failed to write DSP_CONFIG2: %d\n", ret);
			return ret;
		}

		/*
		 * We're using the frequency setting in MADERA_DSP_CLOCK_2 so
		 * don't change the frequency select bits in MADERA_DSP_CLOCK_1
		 */
		mask = MADERA_SYSCLK_SRC_MASK;
	}

	if (freq % 6144000)
		val |= MADERA_SYSCLK_FRAC;

	dev_dbg(madera->dev, "%s set to %uHz\n", name, freq);

	return regmap_update_bits(madera->regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(madera_set_sysclk);

static int madera_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	int lrclk, bclk, mode, base;

	base = dai->driver->base;

	lrclk = 0;
	bclk = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		mode = MADERA_FMT_DSP_MODE_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) !=
		    SND_SOC_DAIFMT_CBM_CFM) {
			madera_aif_err(dai, "DSP_B not valid in slave mode\n");
			return -EINVAL;
		}
		mode = MADERA_FMT_DSP_MODE_B;
		break;
	case SND_SOC_DAIFMT_I2S:
		mode = MADERA_FMT_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) !=
		    SND_SOC_DAIFMT_CBM_CFM) {
			madera_aif_err(dai, "LEFT_J not valid in slave mode\n");
			return -EINVAL;
		}
		mode = MADERA_FMT_LEFT_JUSTIFIED_MODE;
		break;
	default:
		madera_aif_err(dai, "Unsupported DAI format %d\n",
			       fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		lrclk |= MADERA_AIF1TX_LRCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		bclk |= MADERA_AIF1_BCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		bclk |= MADERA_AIF1_BCLK_MSTR;
		lrclk |= MADERA_AIF1TX_LRCLK_MSTR;
		break;
	default:
		madera_aif_err(dai, "Unsupported master mode %d\n",
			       fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bclk |= MADERA_AIF1_BCLK_INV;
		lrclk |= MADERA_AIF1TX_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bclk |= MADERA_AIF1_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrclk |= MADERA_AIF1TX_LRCLK_INV;
		break;
	default:
		madera_aif_err(dai, "Unsupported invert mode %d\n",
			       fmt & SND_SOC_DAIFMT_INV_MASK);
		return -EINVAL;
	}

	regmap_update_bits(madera->regmap, base + MADERA_AIF_BCLK_CTRL,
			   MADERA_AIF1_BCLK_INV | MADERA_AIF1_BCLK_MSTR,
			   bclk);
	regmap_update_bits(madera->regmap, base + MADERA_AIF_TX_PIN_CTRL,
			   MADERA_AIF1TX_LRCLK_INV | MADERA_AIF1TX_LRCLK_MSTR,
			   lrclk);
	regmap_update_bits(madera->regmap, base + MADERA_AIF_RX_PIN_CTRL,
			   MADERA_AIF1RX_LRCLK_INV | MADERA_AIF1RX_LRCLK_MSTR,
			   lrclk);
	regmap_update_bits(madera->regmap, base + MADERA_AIF_FORMAT,
			   MADERA_AIF1_FMT_MASK, mode);

	return 0;
}

static const int madera_48k_bclk_rates[] = {
	-1,
	48000,
	64000,
	96000,
	128000,
	192000,
	256000,
	384000,
	512000,
	768000,
	1024000,
	1536000,
	2048000,
	3072000,
	4096000,
	6144000,
	8192000,
	12288000,
	24576000,
};

static const int madera_44k1_bclk_rates[] = {
	-1,
	44100,
	58800,
	88200,
	117600,
	177640,
	235200,
	352800,
	470400,
	705600,
	940800,
	1411200,
	1881600,
	2822400,
	3763200,
	5644800,
	7526400,
	11289600,
	22579200,
};

static const unsigned int madera_sr_vals[] = {
	0,
	12000,
	24000,
	48000,
	96000,
	192000,
	384000,
	768000,
	0,
	11025,
	22050,
	44100,
	88200,
	176400,
	352800,
	705600,
	4000,
	8000,
	16000,
	32000,
	64000,
	128000,
	256000,
	512000,
};

#define MADERA_192K_48K_RATE_MASK	0x0F003E
#define MADERA_192K_44K1_RATE_MASK	0x003E00
#define MADERA_192K_RATE_MASK		(MADERA_192K_48K_RATE_MASK | \
					 MADERA_192K_44K1_RATE_MASK)
#define MADERA_384K_48K_RATE_MASK	0x0F007E
#define MADERA_384K_44K1_RATE_MASK	0x007E00
#define MADERA_384K_RATE_MASK		(MADERA_384K_48K_RATE_MASK | \
					 MADERA_384K_44K1_RATE_MASK)

static const struct snd_pcm_hw_constraint_list madera_constraint = {
	.count	= ARRAY_SIZE(madera_sr_vals),
	.list	= madera_sr_vals,
};

static int madera_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera_dai_priv *dai_priv = &priv->dai[dai->id - 1];
	struct madera *madera = priv->madera;
	unsigned int base_rate;

	if (!substream->runtime)
		return 0;

	switch (dai_priv->clk) {
	case MADERA_CLK_SYSCLK_1:
	case MADERA_CLK_SYSCLK_2:
	case MADERA_CLK_SYSCLK_3:
		base_rate = priv->sysclk;
		break;
	case MADERA_CLK_ASYNCCLK_1:
	case MADERA_CLK_ASYNCCLK_2:
		base_rate = priv->asyncclk;
		break;
	default:
		return 0;
	}

	switch (madera->type) {
	case CS42L92:
	case CS47L92:
	case CS47L93:
		if (base_rate == 0)
			dai_priv->constraint.mask = MADERA_384K_RATE_MASK;
		else if (base_rate % 4000)
			dai_priv->constraint.mask = MADERA_384K_44K1_RATE_MASK;
		else
			dai_priv->constraint.mask = MADERA_384K_48K_RATE_MASK;
		break;
	default:
		if (base_rate == 0)
			dai_priv->constraint.mask = MADERA_192K_RATE_MASK;
		else if (base_rate % 4000)
			dai_priv->constraint.mask = MADERA_192K_44K1_RATE_MASK;
		else
			dai_priv->constraint.mask = MADERA_192K_48K_RATE_MASK;
		break;
	}

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &dai_priv->constraint);
}

static int madera_hw_params_rate(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera_dai_priv *dai_priv = &priv->dai[dai->id - 1];
	int base = dai->driver->base;
	int i, sr_val;
	unsigned int reg, cur, tar;
	int ret;

	for (i = 0; i < ARRAY_SIZE(madera_sr_vals); i++)
		if (madera_sr_vals[i] == params_rate(params))
			break;

	if (i == ARRAY_SIZE(madera_sr_vals)) {
		madera_aif_err(dai, "Unsupported sample rate %dHz\n",
			       params_rate(params));
		return -EINVAL;
	}
	sr_val = i;

	switch (dai_priv->clk) {
	case MADERA_CLK_SYSCLK_1:
		reg = MADERA_SAMPLE_RATE_1;
		tar = 0 << MADERA_AIF1_RATE_SHIFT;
		break;
	case MADERA_CLK_SYSCLK_2:
		reg = MADERA_SAMPLE_RATE_2;
		tar = 1 << MADERA_AIF1_RATE_SHIFT;
		break;
	case MADERA_CLK_SYSCLK_3:
		reg = MADERA_SAMPLE_RATE_3;
		tar = 2 << MADERA_AIF1_RATE_SHIFT;
		break;
	case MADERA_CLK_ASYNCCLK_1:
		reg = MADERA_ASYNC_SAMPLE_RATE_1;
		tar = 8 << MADERA_AIF1_RATE_SHIFT;
		break;
	case MADERA_CLK_ASYNCCLK_2:
		reg = MADERA_ASYNC_SAMPLE_RATE_2;
		tar = 9 << MADERA_AIF1_RATE_SHIFT;
		break;
	default:
		madera_aif_err(dai, "Invalid clock %d\n", dai_priv->clk);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, reg, MADERA_SAMPLE_RATE_1_MASK,
				      sr_val);

	if (!base)
		return 0;

	ret = regmap_read(priv->madera->regmap,
			  base + MADERA_AIF_RATE_CTRL, &cur);
	if (ret != 0) {
		madera_aif_err(dai, "Failed to check rate: %d\n", ret);
		return ret;
	}

	if ((cur & MADERA_AIF1_RATE_MASK) == (tar & MADERA_AIF1_RATE_MASK))
		return 0;

	mutex_lock(&priv->rate_lock);

	if (!madera_can_change_grp_rate(priv, base + MADERA_AIF_RATE_CTRL)) {
		madera_aif_warn(dai, "Cannot change rate while active\n");
		ret = -EBUSY;
		goto out;
	}

	/* Guard the rate change with SYSCLK cycles */
	madera_spin_sysclk(priv);
	snd_soc_component_update_bits(component, base + MADERA_AIF_RATE_CTRL,
				      MADERA_AIF1_RATE_MASK, tar);
	madera_spin_sysclk(priv);

out:
	mutex_unlock(&priv->rate_lock);

	return ret;
}

static int madera_aif_cfg_changed(struct snd_soc_component *component,
				  int base, int bclk, int lrclk, int frame)
{
	unsigned int val;

	val = snd_soc_component_read(component, base + MADERA_AIF_BCLK_CTRL);
	if (bclk != (val & MADERA_AIF1_BCLK_FREQ_MASK))
		return 1;

	val = snd_soc_component_read(component, base + MADERA_AIF_RX_BCLK_RATE);
	if (lrclk != (val & MADERA_AIF1RX_BCPF_MASK))
		return 1;

	val = snd_soc_component_read(component, base + MADERA_AIF_FRAME_CTRL_1);
	if (frame != (val & (MADERA_AIF1TX_WL_MASK |
			     MADERA_AIF1TX_SLOT_LEN_MASK)))
		return 1;

	return 0;
}

static int madera_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	int base = dai->driver->base;
	const int *rates;
	int i, ret;
	unsigned int val;
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	unsigned int chan_limit =
			madera->pdata.codec.max_channels_clocked[dai->id - 1];
	int tdm_width = priv->tdm_width[dai->id - 1];
	int tdm_slots = priv->tdm_slots[dai->id - 1];
	int bclk, lrclk, wl, frame, bclk_target, num_rates;
	int reconfig;
	unsigned int aif_tx_state = 0, aif_rx_state = 0;

	if (rate % 4000) {
		rates = &madera_44k1_bclk_rates[0];
		num_rates = ARRAY_SIZE(madera_44k1_bclk_rates);
	} else {
		rates = &madera_48k_bclk_rates[0];
		num_rates = ARRAY_SIZE(madera_48k_bclk_rates);
	}

	wl = snd_pcm_format_width(params_format(params));

	if (tdm_slots) {
		madera_aif_dbg(dai, "Configuring for %d %d bit TDM slots\n",
			       tdm_slots, tdm_width);
		bclk_target = tdm_slots * tdm_width * rate;
		channels = tdm_slots;
	} else {
		bclk_target = snd_soc_params_to_bclk(params);
		tdm_width = wl;
	}

	if (chan_limit && chan_limit < channels) {
		madera_aif_dbg(dai, "Limiting to %d channels\n", chan_limit);
		bclk_target /= channels;
		bclk_target *= chan_limit;
	}

	/* Force multiple of 2 channels for I2S mode */
	val = snd_soc_component_read(component, base + MADERA_AIF_FORMAT);
	val &= MADERA_AIF1_FMT_MASK;
	if ((channels & 1) && val == MADERA_FMT_I2S_MODE) {
		madera_aif_dbg(dai, "Forcing stereo mode\n");
		bclk_target /= channels;
		bclk_target *= channels + 1;
	}

	for (i = 0; i < num_rates; i++) {
		if (rates[i] >= bclk_target && rates[i] % rate == 0) {
			bclk = i;
			break;
		}
	}

	if (i == num_rates) {
		madera_aif_err(dai, "Unsupported sample rate %dHz\n", rate);
		return -EINVAL;
	}

	lrclk = rates[bclk] / rate;

	madera_aif_dbg(dai, "BCLK %dHz LRCLK %dHz\n",
		       rates[bclk], rates[bclk] / lrclk);

	frame = wl << MADERA_AIF1TX_WL_SHIFT | tdm_width;

	reconfig = madera_aif_cfg_changed(component, base, bclk, lrclk, frame);
	if (reconfig < 0)
		return reconfig;

	if (reconfig) {
		/* Save AIF TX/RX state */
		regmap_read(madera->regmap, base + MADERA_AIF_TX_ENABLES,
			    &aif_tx_state);
		regmap_read(madera->regmap, base + MADERA_AIF_RX_ENABLES,
			    &aif_rx_state);
		/* Disable AIF TX/RX before reconfiguring it */
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_TX_ENABLES, 0xff, 0x0);
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_RX_ENABLES, 0xff, 0x0);
	}

	ret = madera_hw_params_rate(substream, params, dai);
	if (ret != 0)
		goto restore_aif;

	if (reconfig) {
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_BCLK_CTRL,
				   MADERA_AIF1_BCLK_FREQ_MASK, bclk);
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_RX_BCLK_RATE,
				   MADERA_AIF1RX_BCPF_MASK, lrclk);
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_FRAME_CTRL_1,
				   MADERA_AIF1TX_WL_MASK |
				   MADERA_AIF1TX_SLOT_LEN_MASK, frame);
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_FRAME_CTRL_2,
				   MADERA_AIF1RX_WL_MASK |
				   MADERA_AIF1RX_SLOT_LEN_MASK, frame);
	}

restore_aif:
	if (reconfig) {
		/* Restore AIF TX/RX state */
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_TX_ENABLES,
				   0xff, aif_tx_state);
		regmap_update_bits(madera->regmap,
				   base + MADERA_AIF_RX_ENABLES,
				   0xff, aif_rx_state);
	}

	return ret;
}

static int madera_is_syncclk(int clk_id)
{
	switch (clk_id) {
	case MADERA_CLK_SYSCLK_1:
	case MADERA_CLK_SYSCLK_2:
	case MADERA_CLK_SYSCLK_3:
		return 1;
	case MADERA_CLK_ASYNCCLK_1:
	case MADERA_CLK_ASYNCCLK_2:
		return 0;
	default:
		return -EINVAL;
	}
}

static int madera_dai_set_sysclk(struct snd_soc_dai *dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera_dai_priv *dai_priv = &priv->dai[dai->id - 1];
	struct snd_soc_dapm_route routes[2];
	int is_sync;

	is_sync = madera_is_syncclk(clk_id);
	if (is_sync < 0) {
		dev_err(component->dev, "Illegal DAI clock id %d\n", clk_id);
		return is_sync;
	}

	if (is_sync == madera_is_syncclk(dai_priv->clk))
		return 0;

	if (snd_soc_dai_active(dai)) {
		dev_err(component->dev, "Can't change clock on active DAI %d\n",
			dai->id);
		return -EBUSY;
	}

	dev_dbg(component->dev, "Setting AIF%d to %s\n", dai->id,
		is_sync ? "SYSCLK" : "ASYNCCLK");

	/*
	 * A connection to SYSCLK is always required, we only add and remove
	 * a connection to ASYNCCLK
	 */
	memset(&routes, 0, sizeof(routes));
	routes[0].sink = dai->driver->capture.stream_name;
	routes[1].sink = dai->driver->playback.stream_name;
	routes[0].source = "ASYNCCLK";
	routes[1].source = "ASYNCCLK";

	if (is_sync)
		snd_soc_dapm_del_routes(dapm, routes, ARRAY_SIZE(routes));
	else
		snd_soc_dapm_add_routes(dapm, routes, ARRAY_SIZE(routes));

	dai_priv->clk = clk_id;

	return snd_soc_dapm_sync(dapm);
}

static int madera_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct snd_soc_component *component = dai->component;
	int base = dai->driver->base;
	unsigned int reg;
	int ret;

	if (tristate)
		reg = MADERA_AIF1_TRI;
	else
		reg = 0;

	ret = snd_soc_component_update_bits(component,
					    base + MADERA_AIF_RATE_CTRL,
					    MADERA_AIF1_TRI, reg);
	if (ret < 0)
		return ret;
	else
		return 0;
}

static void madera_set_channels_to_mask(struct snd_soc_dai *dai,
					unsigned int base,
					int channels, unsigned int mask)
{
	struct snd_soc_component *component = dai->component;
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	int slot, i;

	for (i = 0; i < channels; ++i) {
		slot = ffs(mask) - 1;
		if (slot < 0)
			return;

		regmap_write(madera->regmap, base + i, slot);

		mask &= ~(1 << slot);
	}

	if (mask)
		madera_aif_warn(dai, "Too many channels in TDM mask\n");
}

static int madera_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			       unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	int base = dai->driver->base;
	int rx_max_chan = dai->driver->playback.channels_max;
	int tx_max_chan = dai->driver->capture.channels_max;

	/* Only support TDM for the physical AIFs */
	if (dai->id > MADERA_MAX_AIF)
		return -ENOTSUPP;

	if (slots == 0) {
		tx_mask = (1 << tx_max_chan) - 1;
		rx_mask = (1 << rx_max_chan) - 1;
	}

	madera_set_channels_to_mask(dai, base + MADERA_AIF_FRAME_CTRL_3,
				    tx_max_chan, tx_mask);
	madera_set_channels_to_mask(dai, base + MADERA_AIF_FRAME_CTRL_11,
				    rx_max_chan, rx_mask);

	priv->tdm_width[dai->id - 1] = slot_width;
	priv->tdm_slots[dai->id - 1] = slots;

	return 0;
}

const struct snd_soc_dai_ops madera_dai_ops = {
	.startup = &madera_startup,
	.set_fmt = &madera_set_fmt,
	.set_tdm_slot = &madera_set_tdm_slot,
	.hw_params = &madera_hw_params,
	.set_sysclk = &madera_dai_set_sysclk,
	.set_tristate = &madera_set_tristate,
};
EXPORT_SYMBOL_GPL(madera_dai_ops);

const struct snd_soc_dai_ops madera_simple_dai_ops = {
	.startup = &madera_startup,
	.hw_params = &madera_hw_params_rate,
	.set_sysclk = &madera_dai_set_sysclk,
};
EXPORT_SYMBOL_GPL(madera_simple_dai_ops);

int madera_init_dai(struct madera_priv *priv, int id)
{
	struct madera_dai_priv *dai_priv = &priv->dai[id];

	dai_priv->clk = MADERA_CLK_SYSCLK_1;
	dai_priv->constraint = madera_constraint;

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_dai);

static const struct {
	unsigned int min;
	unsigned int max;
	u16 fratio;
	int ratio;
} fll_sync_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

static const unsigned int pseudo_fref_max[MADERA_FLL_MAX_FRATIO] = {
	13500000,
	 6144000,
	 6144000,
	 3072000,
	 3072000,
	 2822400,
	 2822400,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	 1536000,
	  768000,
};

struct madera_fll_gains {
	unsigned int min;
	unsigned int max;
	int gain;		/* main gain */
	int alt_gain;		/* alternate integer gain */
};

static const struct madera_fll_gains madera_fll_sync_gains[] = {
	{       0,   256000, 0, -1 },
	{  256000,  1000000, 2, -1 },
	{ 1000000, 13500000, 4, -1 },
};

static const struct madera_fll_gains madera_fll_main_gains[] = {
	{       0,   100000, 0, 2 },
	{  100000,   375000, 2, 2 },
	{  375000,   768000, 3, 2 },
	{  768001,  1500000, 3, 3 },
	{ 1500000,  6000000, 4, 3 },
	{ 6000000, 13500000, 5, 3 },
};

static int madera_find_sync_fratio(unsigned int fref, int *fratio)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fll_sync_fratios); i++) {
		if (fll_sync_fratios[i].min <= fref &&
		    fref <= fll_sync_fratios[i].max) {
			if (fratio)
				*fratio = fll_sync_fratios[i].fratio;

			return fll_sync_fratios[i].ratio;
		}
	}

	return -EINVAL;
}

static int madera_find_main_fratio(unsigned int fref, unsigned int fout,
				   int *fratio)
{
	int ratio = 1;

	while ((fout / (ratio * fref)) > MADERA_FLL_MAX_N)
		ratio++;

	if (fratio)
		*fratio = ratio - 1;

	return ratio;
}

static int madera_find_fratio(struct madera_fll *fll, unsigned int fref,
			      bool sync, int *fratio)
{
	switch (fll->madera->type) {
	case CS47L35:
		switch (fll->madera->rev) {
		case 0:
			/* rev A0 uses sync calculation for both loops */
			return madera_find_sync_fratio(fref, fratio);
		default:
			if (sync)
				return madera_find_sync_fratio(fref, fratio);
			else
				return madera_find_main_fratio(fref,
							       fll->fout,
							       fratio);
		}
		break;
	case CS47L85:
	case WM1840:
		/* these use the same calculation for main and sync loops */
		return madera_find_sync_fratio(fref, fratio);
	default:
		if (sync)
			return madera_find_sync_fratio(fref, fratio);
		else
			return madera_find_main_fratio(fref, fll->fout, fratio);
	}
}

static int madera_calc_fratio(struct madera_fll *fll,
			      struct madera_fll_cfg *cfg,
			      unsigned int fref, bool sync)
{
	int init_ratio, ratio;
	int refdiv, div;

	/* fref must be <=13.5MHz, find initial refdiv */
	div = 1;
	cfg->refdiv = 0;
	while (fref > MADERA_FLL_MAX_FREF) {
		div *= 2;
		fref /= 2;
		cfg->refdiv++;

		if (div > MADERA_FLL_MAX_REFDIV)
			return -EINVAL;
	}

	/* Find an appropriate FLL_FRATIO */
	init_ratio = madera_find_fratio(fll, fref, sync, &cfg->fratio);
	if (init_ratio < 0) {
		madera_fll_err(fll, "Unable to find FRATIO for fref=%uHz\n",
			       fref);
		return init_ratio;
	}

	if (!sync)
		cfg->fratio = init_ratio - 1;

	switch (fll->madera->type) {
	case CS47L35:
		switch (fll->madera->rev) {
		case 0:
			if (sync)
				return init_ratio;
			break;
		default:
			return init_ratio;
		}
		break;
	case CS47L85:
	case WM1840:
		if (sync)
			return init_ratio;
		break;
	default:
		return init_ratio;
	}

	/*
	 * For CS47L35 rev A0, CS47L85 and WM1840 adjust FRATIO/refdiv to avoid
	 * integer mode if possible
	 */
	refdiv = cfg->refdiv;

	while (div <= MADERA_FLL_MAX_REFDIV) {
		/*
		 * start from init_ratio because this may already give a
		 * fractional N.K
		 */
		for (ratio = init_ratio; ratio > 0; ratio--) {
			if (fll->fout % (ratio * fref)) {
				cfg->refdiv = refdiv;
				cfg->fratio = ratio - 1;
				return ratio;
			}
		}

		for (ratio = init_ratio + 1; ratio <= MADERA_FLL_MAX_FRATIO;
		     ratio++) {
			if ((MADERA_FLL_VCO_CORNER / 2) /
			    (MADERA_FLL_VCO_MULT * ratio) < fref)
				break;

			if (fref > pseudo_fref_max[ratio - 1])
				break;

			if (fll->fout % (ratio * fref)) {
				cfg->refdiv = refdiv;
				cfg->fratio = ratio - 1;
				return ratio;
			}
		}

		div *= 2;
		fref /= 2;
		refdiv++;
		init_ratio = madera_find_fratio(fll, fref, sync, NULL);
	}

	madera_fll_warn(fll, "Falling back to integer mode operation\n");

	return cfg->fratio + 1;
}

static int madera_find_fll_gain(struct madera_fll *fll,
				struct madera_fll_cfg *cfg,
				unsigned int fref,
				const struct madera_fll_gains *gains,
				int n_gains)
{
	int i;

	for (i = 0; i < n_gains; i++) {
		if (gains[i].min <= fref && fref <= gains[i].max) {
			cfg->gain = gains[i].gain;
			cfg->alt_gain = gains[i].alt_gain;
			return 0;
		}
	}

	madera_fll_err(fll, "Unable to find gain for fref=%uHz\n", fref);

	return -EINVAL;
}

static int madera_calc_fll(struct madera_fll *fll,
			   struct madera_fll_cfg *cfg,
			   unsigned int fref, bool sync)
{
	unsigned int gcd_fll;
	const struct madera_fll_gains *gains;
	int n_gains;
	int ratio, ret;

	madera_fll_dbg(fll, "fref=%u Fout=%u fvco=%u\n",
		       fref, fll->fout, fll->fout * MADERA_FLL_VCO_MULT);

	/* Find an appropriate FLL_FRATIO and refdiv */
	ratio = madera_calc_fratio(fll, cfg, fref, sync);
	if (ratio < 0)
		return ratio;

	/* Apply the division for our remaining calculations */
	fref = fref / (1 << cfg->refdiv);

	cfg->n = fll->fout / (ratio * fref);

	if (fll->fout % (ratio * fref)) {
		gcd_fll = gcd(fll->fout, ratio * fref);
		madera_fll_dbg(fll, "GCD=%u\n", gcd_fll);

		cfg->theta = (fll->fout - (cfg->n * ratio * fref))
			/ gcd_fll;
		cfg->lambda = (ratio * fref) / gcd_fll;
	} else {
		cfg->theta = 0;
		cfg->lambda = 0;
	}

	/*
	 * Round down to 16bit range with cost of accuracy lost.
	 * Denominator must be bigger than numerator so we only
	 * take care of it.
	 */
	while (cfg->lambda >= (1 << 16)) {
		cfg->theta >>= 1;
		cfg->lambda >>= 1;
	}

	switch (fll->madera->type) {
	case CS47L35:
		switch (fll->madera->rev) {
		case 0:
			/* Rev A0 uses the sync gains for both loops */
			gains = madera_fll_sync_gains;
			n_gains = ARRAY_SIZE(madera_fll_sync_gains);
			break;
		default:
			if (sync) {
				gains = madera_fll_sync_gains;
				n_gains = ARRAY_SIZE(madera_fll_sync_gains);
			} else {
				gains = madera_fll_main_gains;
				n_gains = ARRAY_SIZE(madera_fll_main_gains);
			}
			break;
		}
		break;
	case CS47L85:
	case WM1840:
		/* These use the sync gains for both loops */
		gains = madera_fll_sync_gains;
		n_gains = ARRAY_SIZE(madera_fll_sync_gains);
		break;
	default:
		if (sync) {
			gains = madera_fll_sync_gains;
			n_gains = ARRAY_SIZE(madera_fll_sync_gains);
		} else {
			gains = madera_fll_main_gains;
			n_gains = ARRAY_SIZE(madera_fll_main_gains);
		}
		break;
	}

	ret = madera_find_fll_gain(fll, cfg, fref, gains, n_gains);
	if (ret)
		return ret;

	madera_fll_dbg(fll, "N=%d THETA=%d LAMBDA=%d\n",
		       cfg->n, cfg->theta, cfg->lambda);
	madera_fll_dbg(fll, "FRATIO=0x%x(%d) REFCLK_DIV=0x%x(%d)\n",
		       cfg->fratio, ratio, cfg->refdiv, 1 << cfg->refdiv);
	madera_fll_dbg(fll, "GAIN=0x%x(%d)\n", cfg->gain, 1 << cfg->gain);

	return 0;
}

static bool madera_write_fll(struct madera *madera, unsigned int base,
			     struct madera_fll_cfg *cfg, int source,
			     bool sync, int gain)
{
	bool change, fll_change;

	fll_change = false;
	regmap_update_bits_check(madera->regmap,
				 base + MADERA_FLL_CONTROL_3_OFFS,
				 MADERA_FLL1_THETA_MASK,
				 cfg->theta, &change);
	fll_change |= change;
	regmap_update_bits_check(madera->regmap,
				 base + MADERA_FLL_CONTROL_4_OFFS,
				 MADERA_FLL1_LAMBDA_MASK,
				 cfg->lambda, &change);
	fll_change |= change;
	regmap_update_bits_check(madera->regmap,
				 base + MADERA_FLL_CONTROL_5_OFFS,
				 MADERA_FLL1_FRATIO_MASK,
				 cfg->fratio << MADERA_FLL1_FRATIO_SHIFT,
				 &change);
	fll_change |= change;
	regmap_update_bits_check(madera->regmap,
				 base + MADERA_FLL_CONTROL_6_OFFS,
				 MADERA_FLL1_REFCLK_DIV_MASK |
				 MADERA_FLL1_REFCLK_SRC_MASK,
				 cfg->refdiv << MADERA_FLL1_REFCLK_DIV_SHIFT |
				 source << MADERA_FLL1_REFCLK_SRC_SHIFT,
				 &change);
	fll_change |= change;

	if (sync) {
		regmap_update_bits_check(madera->regmap,
					 base + MADERA_FLL_SYNCHRONISER_7_OFFS,
					 MADERA_FLL1_GAIN_MASK,
					 gain << MADERA_FLL1_GAIN_SHIFT,
					 &change);
		fll_change |= change;
	} else {
		regmap_update_bits_check(madera->regmap,
					 base + MADERA_FLL_CONTROL_7_OFFS,
					 MADERA_FLL1_GAIN_MASK,
					 gain << MADERA_FLL1_GAIN_SHIFT,
					 &change);
		fll_change |= change;
	}

	regmap_update_bits_check(madera->regmap,
				 base + MADERA_FLL_CONTROL_2_OFFS,
				 MADERA_FLL1_CTRL_UPD | MADERA_FLL1_N_MASK,
				 MADERA_FLL1_CTRL_UPD | cfg->n, &change);
	fll_change |= change;

	return fll_change;
}

static int madera_is_enabled_fll(struct madera_fll *fll, int base)
{
	struct madera *madera = fll->madera;
	unsigned int reg;
	int ret;

	ret = regmap_read(madera->regmap,
			  base + MADERA_FLL_CONTROL_1_OFFS, &reg);
	if (ret != 0) {
		madera_fll_err(fll, "Failed to read current state: %d\n", ret);
		return ret;
	}

	return reg & MADERA_FLL1_ENA;
}

static int madera_wait_for_fll(struct madera_fll *fll, bool requested)
{
	struct madera *madera = fll->madera;
	unsigned int val = 0;
	bool status;
	int i;

	madera_fll_dbg(fll, "Waiting for FLL...\n");

	for (i = 0; i < 30; i++) {
		regmap_read(madera->regmap, MADERA_IRQ1_RAW_STATUS_2, &val);
		status = val & (MADERA_FLL1_LOCK_STS1 << (fll->id - 1));
		if (status == requested)
			return 0;

		switch (i) {
		case 0 ... 5:
			usleep_range(75, 125);
			break;
		case 11 ... 20:
			usleep_range(750, 1250);
			break;
		default:
			msleep(20);
			break;
		}
	}

	madera_fll_warn(fll, "Timed out waiting for lock\n");

	return -ETIMEDOUT;
}

static bool madera_set_fll_phase_integrator(struct madera_fll *fll,
					    struct madera_fll_cfg *ref_cfg,
					    bool sync)
{
	unsigned int val;
	bool reg_change;

	if (!sync && ref_cfg->theta == 0)
		val = (1 << MADERA_FLL1_PHASE_ENA_SHIFT) |
		      (2 << MADERA_FLL1_PHASE_GAIN_SHIFT);
	else
		val = 2 << MADERA_FLL1_PHASE_GAIN_SHIFT;

	regmap_update_bits_check(fll->madera->regmap,
				 fll->base + MADERA_FLL_EFS_2_OFFS,
				 MADERA_FLL1_PHASE_ENA_MASK |
				 MADERA_FLL1_PHASE_GAIN_MASK,
				 val, &reg_change);

	return reg_change;
}

static int madera_set_fll_clks_reg(struct madera_fll *fll, bool ena,
				   unsigned int reg, unsigned int mask,
				   unsigned int shift)
{
	struct madera *madera = fll->madera;
	unsigned int src;
	struct clk *clk;
	int ret;

	ret = regmap_read(madera->regmap, reg, &src);
	if (ret != 0) {
		madera_fll_err(fll, "Failed to read current source: %d\n",
			       ret);
		return ret;
	}

	src = (src & mask) >> shift;

	switch (src) {
	case MADERA_FLL_SRC_MCLK1:
		clk = madera->mclk[MADERA_MCLK1].clk;
		break;
	case MADERA_FLL_SRC_MCLK2:
		clk = madera->mclk[MADERA_MCLK2].clk;
		break;
	case MADERA_FLL_SRC_MCLK3:
		clk = madera->mclk[MADERA_MCLK3].clk;
		break;
	default:
		return 0;
	}

	if (ena) {
		return clk_prepare_enable(clk);
	} else {
		clk_disable_unprepare(clk);
		return 0;
	}
}

static inline int madera_set_fll_clks(struct madera_fll *fll, int base, bool ena)
{
	return madera_set_fll_clks_reg(fll, ena,
				       base + MADERA_FLL_CONTROL_6_OFFS,
				       MADERA_FLL1_REFCLK_SRC_MASK,
				       MADERA_FLL1_REFCLK_DIV_SHIFT);
}

static inline int madera_set_fllao_clks(struct madera_fll *fll, int base, bool ena)
{
	return madera_set_fll_clks_reg(fll, ena,
				       base + MADERA_FLLAO_CONTROL_6_OFFS,
				       MADERA_FLL_AO_REFCLK_SRC_MASK,
				       MADERA_FLL_AO_REFCLK_SRC_SHIFT);
}

static inline int madera_set_fllhj_clks(struct madera_fll *fll, int base, bool ena)
{
	return madera_set_fll_clks_reg(fll, ena,
				       base + MADERA_FLL_CONTROL_1_OFFS,
				       CS47L92_FLL1_REFCLK_SRC_MASK,
				       CS47L92_FLL1_REFCLK_SRC_SHIFT);
}

static void madera_disable_fll(struct madera_fll *fll)
{
	struct madera *madera = fll->madera;
	unsigned int sync_base;
	bool ref_change, sync_change;

	switch (madera->type) {
	case CS47L35:
		sync_base = fll->base + CS47L35_FLL_SYNCHRONISER_OFFS;
		break;
	default:
		sync_base = fll->base + MADERA_FLL_SYNCHRONISER_OFFS;
		break;
	}

	madera_fll_dbg(fll, "Disabling FLL\n");

	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   MADERA_FLL1_FREERUN, MADERA_FLL1_FREERUN);
	regmap_update_bits_check(madera->regmap,
				 fll->base + MADERA_FLL_CONTROL_1_OFFS,
				 MADERA_FLL1_ENA, 0, &ref_change);
	regmap_update_bits_check(madera->regmap,
				 sync_base + MADERA_FLL_SYNCHRONISER_1_OFFS,
				 MADERA_FLL1_SYNC_ENA, 0, &sync_change);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   MADERA_FLL1_FREERUN, 0);

	madera_wait_for_fll(fll, false);

	if (sync_change)
		madera_set_fll_clks(fll, sync_base, false);

	if (ref_change) {
		madera_set_fll_clks(fll, fll->base, false);
		pm_runtime_put_autosuspend(madera->dev);
	}
}

static int madera_enable_fll(struct madera_fll *fll)
{
	struct madera *madera = fll->madera;
	bool have_sync = false;
	int already_enabled = madera_is_enabled_fll(fll, fll->base);
	int sync_enabled;
	struct madera_fll_cfg cfg;
	unsigned int sync_base;
	int gain, ret;
	bool fll_change = false;

	if (already_enabled < 0)
		return already_enabled;	/* error getting current state */

	if (fll->ref_src < 0 || fll->ref_freq == 0) {
		madera_fll_err(fll, "No REFCLK\n");
		ret = -EINVAL;
		goto err;
	}

	madera_fll_dbg(fll, "Enabling FLL, initially %s\n",
		       already_enabled ? "enabled" : "disabled");

	if (fll->fout < MADERA_FLL_MIN_FOUT ||
	    fll->fout > MADERA_FLL_MAX_FOUT) {
		madera_fll_err(fll, "invalid fout %uHz\n", fll->fout);
		ret = -EINVAL;
		goto err;
	}

	switch (madera->type) {
	case CS47L35:
		sync_base = fll->base + CS47L35_FLL_SYNCHRONISER_OFFS;
		break;
	default:
		sync_base = fll->base + MADERA_FLL_SYNCHRONISER_OFFS;
		break;
	}

	sync_enabled = madera_is_enabled_fll(fll, sync_base);
	if (sync_enabled < 0)
		return sync_enabled;

	if (already_enabled) {
		/* Facilitate smooth refclk across the transition */
		regmap_update_bits(fll->madera->regmap,
				   fll->base + MADERA_FLL_CONTROL_1_OFFS,
				   MADERA_FLL1_FREERUN,
				   MADERA_FLL1_FREERUN);
		udelay(32);
		regmap_update_bits(fll->madera->regmap,
				   fll->base + MADERA_FLL_CONTROL_7_OFFS,
				   MADERA_FLL1_GAIN_MASK, 0);

		if (sync_enabled > 0)
			madera_set_fll_clks(fll, sync_base, false);
		madera_set_fll_clks(fll, fll->base, false);
	}

	/* Apply SYNCCLK setting */
	if (fll->sync_src >= 0) {
		ret = madera_calc_fll(fll, &cfg, fll->sync_freq, true);
		if (ret < 0)
			goto err;

		fll_change |= madera_write_fll(madera, sync_base,
					       &cfg, fll->sync_src,
					       true, cfg.gain);
		have_sync = true;
	}

	if (already_enabled && !!sync_enabled != have_sync)
		madera_fll_warn(fll, "Synchroniser changed on active FLL\n");

	/* Apply REFCLK setting */
	ret = madera_calc_fll(fll, &cfg, fll->ref_freq, false);
	if (ret < 0)
		goto err;

	/* Ref path hardcodes lambda to 65536 when sync is on */
	if (have_sync && cfg.lambda)
		cfg.theta = (cfg.theta * (1 << 16)) / cfg.lambda;

	switch (fll->madera->type) {
	case CS47L35:
		switch (fll->madera->rev) {
		case 0:
			gain = cfg.gain;
			break;
		default:
			fll_change |=
				madera_set_fll_phase_integrator(fll, &cfg,
								have_sync);
			if (!have_sync && cfg.theta == 0)
				gain = cfg.alt_gain;
			else
				gain = cfg.gain;
			break;
		}
		break;
	case CS47L85:
	case WM1840:
		gain = cfg.gain;
		break;
	default:
		fll_change |= madera_set_fll_phase_integrator(fll, &cfg,
							      have_sync);
		if (!have_sync && cfg.theta == 0)
			gain = cfg.alt_gain;
		else
			gain = cfg.gain;
		break;
	}

	fll_change |= madera_write_fll(madera, fll->base,
				       &cfg, fll->ref_src,
				       false, gain);

	/*
	 * Increase the bandwidth if we're not using a low frequency
	 * sync source.
	 */
	if (have_sync && fll->sync_freq > 100000)
		regmap_update_bits(madera->regmap,
				   sync_base + MADERA_FLL_SYNCHRONISER_7_OFFS,
				   MADERA_FLL1_SYNC_DFSAT_MASK, 0);
	else
		regmap_update_bits(madera->regmap,
				   sync_base + MADERA_FLL_SYNCHRONISER_7_OFFS,
				   MADERA_FLL1_SYNC_DFSAT_MASK,
				   MADERA_FLL1_SYNC_DFSAT);

	if (!already_enabled)
		pm_runtime_get_sync(madera->dev);

	if (have_sync) {
		madera_set_fll_clks(fll, sync_base, true);
		regmap_update_bits(madera->regmap,
				   sync_base + MADERA_FLL_SYNCHRONISER_1_OFFS,
				   MADERA_FLL1_SYNC_ENA,
				   MADERA_FLL1_SYNC_ENA);
	}

	madera_set_fll_clks(fll, fll->base, true);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   MADERA_FLL1_ENA, MADERA_FLL1_ENA);

	if (already_enabled)
		regmap_update_bits(madera->regmap,
				   fll->base + MADERA_FLL_CONTROL_1_OFFS,
				   MADERA_FLL1_FREERUN, 0);

	if (fll_change || !already_enabled)
		madera_wait_for_fll(fll, true);

	return 0;

err:
	 /* In case of error don't leave the FLL running with an old config */
	madera_disable_fll(fll);

	return ret;
}

static int madera_apply_fll(struct madera_fll *fll)
{
	if (fll->fout) {
		return madera_enable_fll(fll);
	} else {
		madera_disable_fll(fll);
		return 0;
	}
}

int madera_set_fll_syncclk(struct madera_fll *fll, int source,
			   unsigned int fref, unsigned int fout)
{
	/*
	 * fout is ignored, since the synchronizer is an optional extra
	 * constraint on the Fout generated from REFCLK, so the Fout is
	 * set when configuring REFCLK
	 */

	if (fll->sync_src == source && fll->sync_freq == fref)
		return 0;

	fll->sync_src = source;
	fll->sync_freq = fref;

	return madera_apply_fll(fll);
}
EXPORT_SYMBOL_GPL(madera_set_fll_syncclk);

int madera_set_fll_refclk(struct madera_fll *fll, int source,
			  unsigned int fref, unsigned int fout)
{
	int ret;

	if (fll->ref_src == source &&
	    fll->ref_freq == fref && fll->fout == fout)
		return 0;

	/*
	 * Changes of fout on an enabled FLL aren't allowed except when
	 * setting fout==0 to disable the FLL
	 */
	if (fout && fout != fll->fout) {
		ret = madera_is_enabled_fll(fll, fll->base);
		if (ret < 0)
			return ret;

		if (ret) {
			madera_fll_err(fll, "Can't change Fout on active FLL\n");
			return -EBUSY;
		}
	}

	fll->ref_src = source;
	fll->ref_freq = fref;
	fll->fout = fout;

	return madera_apply_fll(fll);
}
EXPORT_SYMBOL_GPL(madera_set_fll_refclk);

int madera_init_fll(struct madera *madera, int id, int base,
		    struct madera_fll *fll)
{
	fll->id = id;
	fll->base = base;
	fll->madera = madera;
	fll->ref_src = MADERA_FLL_SRC_NONE;
	fll->sync_src = MADERA_FLL_SRC_NONE;

	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   MADERA_FLL1_FREERUN, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(madera_init_fll);

static const struct reg_sequence madera_fll_ao_32K_49M_patch[] = {
	{ MADERA_FLLAO_CONTROL_2,  0x02EE },
	{ MADERA_FLLAO_CONTROL_3,  0x0000 },
	{ MADERA_FLLAO_CONTROL_4,  0x0001 },
	{ MADERA_FLLAO_CONTROL_5,  0x0002 },
	{ MADERA_FLLAO_CONTROL_6,  0x8001 },
	{ MADERA_FLLAO_CONTROL_7,  0x0004 },
	{ MADERA_FLLAO_CONTROL_8,  0x0077 },
	{ MADERA_FLLAO_CONTROL_10, 0x06D8 },
	{ MADERA_FLLAO_CONTROL_11, 0x0085 },
	{ MADERA_FLLAO_CONTROL_2,  0x82EE },
};

static const struct reg_sequence madera_fll_ao_32K_45M_patch[] = {
	{ MADERA_FLLAO_CONTROL_2,  0x02B1 },
	{ MADERA_FLLAO_CONTROL_3,  0x0001 },
	{ MADERA_FLLAO_CONTROL_4,  0x0010 },
	{ MADERA_FLLAO_CONTROL_5,  0x0002 },
	{ MADERA_FLLAO_CONTROL_6,  0x8001 },
	{ MADERA_FLLAO_CONTROL_7,  0x0004 },
	{ MADERA_FLLAO_CONTROL_8,  0x0077 },
	{ MADERA_FLLAO_CONTROL_10, 0x06D8 },
	{ MADERA_FLLAO_CONTROL_11, 0x0005 },
	{ MADERA_FLLAO_CONTROL_2,  0x82B1 },
};

struct madera_fllao_patch {
	unsigned int fin;
	unsigned int fout;
	const struct reg_sequence *patch;
	unsigned int patch_size;
};

static const struct madera_fllao_patch madera_fllao_settings[] = {
	{
		.fin = 32768,
		.fout = 49152000,
		.patch = madera_fll_ao_32K_49M_patch,
		.patch_size = ARRAY_SIZE(madera_fll_ao_32K_49M_patch),

	},
	{
		.fin = 32768,
		.fout = 45158400,
		.patch = madera_fll_ao_32K_45M_patch,
		.patch_size = ARRAY_SIZE(madera_fll_ao_32K_45M_patch),
	},
};

static int madera_enable_fll_ao(struct madera_fll *fll,
				const struct reg_sequence *patch,
				unsigned int patch_size)
{
	struct madera *madera = fll->madera;
	int already_enabled = madera_is_enabled_fll(fll, fll->base);
	unsigned int val;
	int i;

	if (already_enabled < 0)
		return already_enabled;

	if (!already_enabled)
		pm_runtime_get_sync(madera->dev);

	madera_fll_dbg(fll, "Enabling FLL_AO, initially %s\n",
		       already_enabled ? "enabled" : "disabled");

	/* FLL_AO_HOLD must be set before configuring any registers */
	regmap_update_bits(fll->madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
			   MADERA_FLL_AO_HOLD, MADERA_FLL_AO_HOLD);

	if (already_enabled)
		madera_set_fllao_clks(fll, fll->base, false);

	for (i = 0; i < patch_size; i++) {
		val = patch[i].def;

		/* modify the patch to apply fll->ref_src as input clock */
		if (patch[i].reg == MADERA_FLLAO_CONTROL_6) {
			val &= ~MADERA_FLL_AO_REFCLK_SRC_MASK;
			val |= (fll->ref_src << MADERA_FLL_AO_REFCLK_SRC_SHIFT)
				& MADERA_FLL_AO_REFCLK_SRC_MASK;
		}

		regmap_write(madera->regmap, patch[i].reg, val);
	}

	madera_set_fllao_clks(fll, fll->base, true);

	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
			   MADERA_FLL_AO_ENA, MADERA_FLL_AO_ENA);

	/* Release the hold so that fll_ao locks to external frequency */
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
			   MADERA_FLL_AO_HOLD, 0);

	if (!already_enabled)
		madera_wait_for_fll(fll, true);

	return 0;
}

static int madera_disable_fll_ao(struct madera_fll *fll)
{
	struct madera *madera = fll->madera;
	bool change;

	madera_fll_dbg(fll, "Disabling FLL_AO\n");

	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
			   MADERA_FLL_AO_HOLD, MADERA_FLL_AO_HOLD);
	regmap_update_bits_check(madera->regmap,
				 fll->base + MADERA_FLLAO_CONTROL_1_OFFS,
				 MADERA_FLL_AO_ENA, 0, &change);

	madera_wait_for_fll(fll, false);

	/*
	 * ctrl_up gates the writes to all fll_ao register, setting it to 0
	 * here ensures that after a runtime suspend/resume cycle when one
	 * enables the fllao then ctrl_up is the last bit that is configured
	 * by the fllao enable code rather than the cache sync operation which
	 * would have updated it much earlier before writing out all fllao
	 * registers
	 */
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLLAO_CONTROL_2_OFFS,
			   MADERA_FLL_AO_CTRL_UPD_MASK, 0);

	if (change) {
		madera_set_fllao_clks(fll, fll->base, false);
		pm_runtime_put_autosuspend(madera->dev);
	}

	return 0;
}

int madera_set_fll_ao_refclk(struct madera_fll *fll, int source,
			     unsigned int fin, unsigned int fout)
{
	int ret = 0;
	const struct reg_sequence *patch = NULL;
	int patch_size = 0;
	unsigned int i;

	if (fll->ref_src == source &&
	    fll->ref_freq == fin && fll->fout == fout)
		return 0;

	madera_fll_dbg(fll, "Change FLL_AO refclk to fin=%u fout=%u source=%d\n",
		       fin, fout, source);

	if (fout && (fll->ref_freq != fin || fll->fout != fout)) {
		for (i = 0; i < ARRAY_SIZE(madera_fllao_settings); i++) {
			if (madera_fllao_settings[i].fin == fin &&
			    madera_fllao_settings[i].fout == fout)
				break;
		}

		if (i == ARRAY_SIZE(madera_fllao_settings)) {
			madera_fll_err(fll,
				       "No matching configuration for FLL_AO\n");
			return -EINVAL;
		}

		patch = madera_fllao_settings[i].patch;
		patch_size = madera_fllao_settings[i].patch_size;
	}

	fll->ref_src = source;
	fll->ref_freq = fin;
	fll->fout = fout;

	if (fout)
		ret = madera_enable_fll_ao(fll, patch, patch_size);
	else
		madera_disable_fll_ao(fll);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_set_fll_ao_refclk);

static int madera_fllhj_disable(struct madera_fll *fll)
{
	struct madera *madera = fll->madera;
	bool change;

	madera_fll_dbg(fll, "Disabling FLL\n");

	/* Disable lockdet, but don't set ctrl_upd update but.  This allows the
	 * lock status bit to clear as normal, but should the FLL be enabled
	 * again due to a control clock being required, the lock won't re-assert
	 * as the FLL config registers are automatically applied when the FLL
	 * enables.
	 */
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_11_OFFS,
			   MADERA_FLL1_LOCKDET_MASK, 0);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   MADERA_FLL1_HOLD_MASK, MADERA_FLL1_HOLD_MASK);
	regmap_update_bits_check(madera->regmap,
				 fll->base + MADERA_FLL_CONTROL_1_OFFS,
				 MADERA_FLL1_ENA_MASK, 0, &change);

	madera_wait_for_fll(fll, false);

	/* ctrl_up gates the writes to all the fll's registers, setting it to 0
	 * here ensures that after a runtime suspend/resume cycle when one
	 * enables the fll then ctrl_up is the last bit that is configured
	 * by the fll enable code rather than the cache sync operation which
	 * would have updated it much earlier before writing out all fll
	 * registers
	 */
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_2_OFFS,
			   MADERA_FLL1_CTRL_UPD_MASK, 0);

	if (change) {
		madera_set_fllhj_clks(fll, fll->base, false);
		pm_runtime_put_autosuspend(madera->dev);
	}

	return 0;
}

static int madera_fllhj_apply(struct madera_fll *fll, int fin)
{
	struct madera *madera = fll->madera;
	int refdiv, fref, fout, lockdet_thr, fbdiv, hp, fast_clk, fllgcd;
	bool frac = false;
	unsigned int fll_n, min_n, max_n, ratio, theta, lambda;
	unsigned int gains, val, num;

	madera_fll_dbg(fll, "fin=%d, fout=%d\n", fin, fll->fout);

	for (refdiv = 0; refdiv < 4; refdiv++)
		if ((fin / (1 << refdiv)) <= MADERA_FLLHJ_MAX_THRESH)
			break;

	fref = fin / (1 << refdiv);

	/* Use simple heuristic approach to find a configuration that
	 * should work for most input clocks.
	 */
	fast_clk = 0;
	fout = fll->fout;
	frac = fout % fref;

	if (fref < MADERA_FLLHJ_LOW_THRESH) {
		lockdet_thr = 2;
		gains = MADERA_FLLHJ_LOW_GAINS;
		if (frac)
			fbdiv = 256;
		else
			fbdiv = 4;
	} else if (fref < MADERA_FLLHJ_MID_THRESH) {
		lockdet_thr = 8;
		gains = MADERA_FLLHJ_MID_GAINS;
		fbdiv = 1;
	} else {
		lockdet_thr = 8;
		gains = MADERA_FLLHJ_HIGH_GAINS;
		fbdiv = 1;
		/* For high speed input clocks, enable 300MHz fast oscillator
		 * when we're in fractional divider mode.
		 */
		if (frac) {
			fast_clk = 0x3;
			fout = fll->fout * 6;
		}
	}
	/* Use high performance mode for fractional configurations. */
	if (frac) {
		hp = 0x3;
		min_n = MADERA_FLLHJ_FRAC_MIN_N;
		max_n = MADERA_FLLHJ_FRAC_MAX_N;
	} else {
		hp = 0x0;
		min_n = MADERA_FLLHJ_INT_MIN_N;
		max_n = MADERA_FLLHJ_INT_MAX_N;
	}

	ratio = fout / fref;

	madera_fll_dbg(fll, "refdiv=%d, fref=%d, frac:%d\n",
		       refdiv, fref, frac);

	while (ratio / fbdiv < min_n) {
		fbdiv /= 2;
		if (fbdiv < 1) {
			madera_fll_err(fll, "FBDIV (%d) must be >= 1\n", fbdiv);
			return -EINVAL;
		}
	}
	while (frac && (ratio / fbdiv > max_n)) {
		fbdiv *= 2;
		if (fbdiv >= 1024) {
			madera_fll_err(fll, "FBDIV (%u) >= 1024\n", fbdiv);
			return -EINVAL;
		}
	}

	madera_fll_dbg(fll, "lockdet=%d, hp=0x%x, fbdiv:%d\n",
		       lockdet_thr, hp, fbdiv);

	/* Calculate N.K values */
	fllgcd = gcd(fout, fbdiv * fref);
	num = fout / fllgcd;
	lambda = (fref * fbdiv) / fllgcd;
	fll_n = num / lambda;
	theta = num % lambda;

	madera_fll_dbg(fll, "fll_n=%d, gcd=%d, theta=%d, lambda=%d\n",
		       fll_n, fllgcd, theta, lambda);

	/* Some sanity checks before any registers are written. */
	if (fll_n < min_n || fll_n > max_n) {
		madera_fll_err(fll, "N not in valid %s mode range %d-%d: %d\n",
			       frac ? "fractional" : "integer", min_n, max_n,
			       fll_n);
		return -EINVAL;
	}
	if (fbdiv < 1 || (frac && fbdiv >= 1024) || (!frac && fbdiv >= 256)) {
		madera_fll_err(fll, "Invalid fbdiv for %s mode (%u)\n",
			       frac ? "fractional" : "integer", fbdiv);
		return -EINVAL;
	}

	/* clear the ctrl_upd bit to guarantee we write to it later. */
	regmap_write(madera->regmap,
		     fll->base + MADERA_FLL_CONTROL_2_OFFS,
		     fll_n << MADERA_FLL1_N_SHIFT);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_3_OFFS,
			   MADERA_FLL1_THETA_MASK,
			   theta << MADERA_FLL1_THETA_SHIFT);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_4_OFFS,
			   MADERA_FLL1_LAMBDA_MASK,
			   lambda << MADERA_FLL1_LAMBDA_SHIFT);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_5_OFFS,
			   MADERA_FLL1_FB_DIV_MASK,
			   fbdiv << MADERA_FLL1_FB_DIV_SHIFT);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_6_OFFS,
			   MADERA_FLL1_REFCLK_DIV_MASK,
			   refdiv << MADERA_FLL1_REFCLK_DIV_SHIFT);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_GAIN_OFFS,
			   0xffff,
			   gains);
	val = hp << MADERA_FLL1_HP_SHIFT;
	val |= 1 << MADERA_FLL1_PHASEDET_ENA_SHIFT;
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_10_OFFS,
			   MADERA_FLL1_HP_MASK | MADERA_FLL1_PHASEDET_ENA_MASK,
			   val);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_11_OFFS,
			   MADERA_FLL1_LOCKDET_THR_MASK,
			   lockdet_thr << MADERA_FLL1_LOCKDET_THR_SHIFT);
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL1_DIGITAL_TEST_1_OFFS,
			   MADERA_FLL1_SYNC_EFS_ENA_MASK |
			   MADERA_FLL1_CLK_VCO_FAST_SRC_MASK,
			   fast_clk);

	return 0;
}

static int madera_fllhj_enable(struct madera_fll *fll)
{
	struct madera *madera = fll->madera;
	int already_enabled = madera_is_enabled_fll(fll, fll->base);
	int ret;

	if (already_enabled < 0)
		return already_enabled;

	if (!already_enabled)
		pm_runtime_get_sync(madera->dev);

	madera_fll_dbg(fll, "Enabling FLL, initially %s\n",
		       already_enabled ? "enabled" : "disabled");

	/* FLLn_HOLD must be set before configuring any registers */
	regmap_update_bits(fll->madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   MADERA_FLL1_HOLD_MASK,
			   MADERA_FLL1_HOLD_MASK);

	if (already_enabled)
		madera_set_fllhj_clks(fll, fll->base, false);

	/* Apply refclk */
	ret = madera_fllhj_apply(fll, fll->ref_freq);
	if (ret) {
		madera_fll_err(fll, "Failed to set FLL: %d\n", ret);
		goto out;
	}
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   CS47L92_FLL1_REFCLK_SRC_MASK,
			   fll->ref_src << CS47L92_FLL1_REFCLK_SRC_SHIFT);

	madera_set_fllhj_clks(fll, fll->base, true);

	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   MADERA_FLL1_ENA_MASK,
			   MADERA_FLL1_ENA_MASK);

out:
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_11_OFFS,
			   MADERA_FLL1_LOCKDET_MASK,
			   MADERA_FLL1_LOCKDET_MASK);

	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_2_OFFS,
			   MADERA_FLL1_CTRL_UPD_MASK,
			   MADERA_FLL1_CTRL_UPD_MASK);

	/* Release the hold so that flln locks to external frequency */
	regmap_update_bits(madera->regmap,
			   fll->base + MADERA_FLL_CONTROL_1_OFFS,
			   MADERA_FLL1_HOLD_MASK,
			   0);

	if (!already_enabled)
		madera_wait_for_fll(fll, true);

	return 0;
}

static int madera_fllhj_validate(struct madera_fll *fll,
				 unsigned int ref_in,
				 unsigned int fout)
{
	if (fout && !ref_in) {
		madera_fll_err(fll, "fllout set without valid input clk\n");
		return -EINVAL;
	}

	if (fll->fout && fout != fll->fout) {
		madera_fll_err(fll, "Can't change output on active FLL\n");
		return -EINVAL;
	}

	if (ref_in / MADERA_FLL_MAX_REFDIV > MADERA_FLLHJ_MAX_THRESH) {
		madera_fll_err(fll, "Can't scale %dMHz to <=13MHz\n", ref_in);
		return -EINVAL;
	}

	return 0;
}

int madera_fllhj_set_refclk(struct madera_fll *fll, int source,
			    unsigned int fin, unsigned int fout)
{
	int ret = 0;

	/* To remain consistent with previous FLLs, we expect fout to be
	 * provided in the form of the required sysclk rate, which is
	 * 2x the calculated fll out.
	 */
	if (fout)
		fout /= 2;

	if (fll->ref_src == source && fll->ref_freq == fin &&
	    fll->fout == fout)
		return 0;

	if (fin && fout && madera_fllhj_validate(fll, fin, fout))
		return -EINVAL;

	fll->ref_src = source;
	fll->ref_freq = fin;
	fll->fout = fout;

	if (fout)
		ret = madera_fllhj_enable(fll);
	else
		madera_fllhj_disable(fll);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_fllhj_set_refclk);

/**
 * madera_set_output_mode - Set the mode of the specified output
 *
 * @component: Device to configure
 * @output: Output number
 * @differential: True to set the output to differential mode
 *
 * Some systems use external analogue switches to connect more
 * analogue devices to the CODEC than are supported by the device.  In
 * some systems this requires changing the switched output from single
 * ended to differential mode dynamically at runtime, an operation
 * supported using this function.
 *
 * Most systems have a single static configuration and should use
 * platform data instead.
 */
int madera_set_output_mode(struct snd_soc_component *component, int output,
			   bool differential)
{
	unsigned int reg, val;
	int ret;

	if (output < 1 || output > MADERA_MAX_OUTPUT)
		return -EINVAL;

	reg = MADERA_OUTPUT_PATH_CONFIG_1L + (output - 1) * 8;

	if (differential)
		val = MADERA_OUT1_MONO;
	else
		val = 0;

	ret = snd_soc_component_update_bits(component, reg, MADERA_OUT1_MONO,
					    val);
	if (ret < 0)
		return ret;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(madera_set_output_mode);

static bool madera_eq_filter_unstable(bool mode, __be16 _a, __be16 _b)
{
	s16 a = be16_to_cpu(_a);
	s16 b = be16_to_cpu(_b);

	if (!mode) {
		return abs(a) >= 4096;
	} else {
		if (abs(b) >= 4096)
			return true;

		return (abs((a << 16) / (4096 - b)) >= 4096 << 4);
	}
}

int madera_eq_coeff_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	struct soc_bytes *params = (void *)kcontrol->private_value;
	unsigned int val;
	__be16 *data;
	int len;
	int ret;

	len = params->num_regs * regmap_get_val_bytes(madera->regmap);

	data = kmemdup(ucontrol->value.bytes.data, len, GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;

	data[0] &= cpu_to_be16(MADERA_EQ1_B1_MODE);

	if (madera_eq_filter_unstable(!!data[0], data[1], data[2]) ||
	    madera_eq_filter_unstable(true, data[4], data[5]) ||
	    madera_eq_filter_unstable(true, data[8], data[9]) ||
	    madera_eq_filter_unstable(true, data[12], data[13]) ||
	    madera_eq_filter_unstable(false, data[16], data[17])) {
		dev_err(madera->dev, "Rejecting unstable EQ coefficients\n");
		ret = -EINVAL;
		goto out;
	}

	ret = regmap_read(madera->regmap, params->base, &val);
	if (ret != 0)
		goto out;

	val &= ~MADERA_EQ1_B1_MODE;
	data[0] |= cpu_to_be16(val);

	ret = regmap_raw_write(madera->regmap, params->base, data, len);

out:
	kfree(data);

	return ret;
}
EXPORT_SYMBOL_GPL(madera_eq_coeff_put);

int madera_lhpf_coeff_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct madera_priv *priv = snd_soc_component_get_drvdata(component);
	struct madera *madera = priv->madera;
	__be16 *data = (__be16 *)ucontrol->value.bytes.data;
	s16 val = be16_to_cpu(*data);

	if (abs(val) >= 4096) {
		dev_err(madera->dev, "Rejecting unstable LHPF coefficients\n");
		return -EINVAL;
	}

	return snd_soc_bytes_put(kcontrol, ucontrol);
}
EXPORT_SYMBOL_GPL(madera_lhpf_coeff_put);

MODULE_SOFTDEP("pre: madera");
MODULE_DESCRIPTION("ASoC Cirrus Logic Madera codec support");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.cirrus.com>");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
