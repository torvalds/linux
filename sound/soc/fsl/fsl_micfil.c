// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright 2018 NXP

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/dma/imx-dma.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/core.h>

#include "fsl_micfil.h"
#include "fsl_utils.h"

#define MICFIL_OSR_DEFAULT	16

enum quality {
	QUALITY_HIGH,
	QUALITY_MEDIUM,
	QUALITY_LOW,
	QUALITY_VLOW0,
	QUALITY_VLOW1,
	QUALITY_VLOW2,
};

struct fsl_micfil {
	struct platform_device *pdev;
	struct regmap *regmap;
	const struct fsl_micfil_soc_data *soc;
	struct clk *busclk;
	struct clk *mclk;
	struct clk *pll8k_clk;
	struct clk *pll11k_clk;
	struct snd_dmaengine_dai_dma_data dma_params_rx;
	struct sdma_peripheral_config sdmacfg;
	struct snd_soc_card *card;
	unsigned int dataline;
	char name[32];
	int irq[MICFIL_IRQ_LINES];
	enum quality quality;
	int dc_remover;
	int vad_init_mode;
	int vad_enabled;
	int vad_detected;
	struct fsl_micfil_verid verid;
	struct fsl_micfil_param param;
};

struct fsl_micfil_soc_data {
	unsigned int fifos;
	unsigned int fifo_depth;
	unsigned int dataline;
	bool imx;
	bool use_edma;
	bool use_verid;
	u64  formats;
};

static struct fsl_micfil_soc_data fsl_micfil_imx8mm = {
	.imx = true,
	.fifos = 8,
	.fifo_depth = 8,
	.dataline =  0xf,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
};

static struct fsl_micfil_soc_data fsl_micfil_imx8mp = {
	.imx = true,
	.fifos = 8,
	.fifo_depth = 32,
	.dataline =  0xf,
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
};

static struct fsl_micfil_soc_data fsl_micfil_imx93 = {
	.imx = true,
	.fifos = 8,
	.fifo_depth = 32,
	.dataline =  0xf,
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.use_edma = true,
	.use_verid = true,
};

static const struct of_device_id fsl_micfil_dt_ids[] = {
	{ .compatible = "fsl,imx8mm-micfil", .data = &fsl_micfil_imx8mm },
	{ .compatible = "fsl,imx8mp-micfil", .data = &fsl_micfil_imx8mp },
	{ .compatible = "fsl,imx93-micfil", .data = &fsl_micfil_imx93 },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_micfil_dt_ids);

static const char * const micfil_quality_select_texts[] = {
	[QUALITY_HIGH] = "High",
	[QUALITY_MEDIUM] = "Medium",
	[QUALITY_LOW] = "Low",
	[QUALITY_VLOW0] = "VLow0",
	[QUALITY_VLOW1] = "Vlow1",
	[QUALITY_VLOW2] = "Vlow2",
};

static const struct soc_enum fsl_micfil_quality_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(micfil_quality_select_texts),
			    micfil_quality_select_texts);

static DECLARE_TLV_DB_SCALE(gain_tlv, 0, 100, 0);

static int micfil_set_quality(struct fsl_micfil *micfil)
{
	u32 qsel;

	switch (micfil->quality) {
	case QUALITY_HIGH:
		qsel = MICFIL_QSEL_HIGH_QUALITY;
		break;
	case QUALITY_MEDIUM:
		qsel = MICFIL_QSEL_MEDIUM_QUALITY;
		break;
	case QUALITY_LOW:
		qsel = MICFIL_QSEL_LOW_QUALITY;
		break;
	case QUALITY_VLOW0:
		qsel = MICFIL_QSEL_VLOW0_QUALITY;
		break;
	case QUALITY_VLOW1:
		qsel = MICFIL_QSEL_VLOW1_QUALITY;
		break;
	case QUALITY_VLOW2:
		qsel = MICFIL_QSEL_VLOW2_QUALITY;
		break;
	}

	return regmap_update_bits(micfil->regmap, REG_MICFIL_CTRL2,
				  MICFIL_CTRL2_QSEL,
				  FIELD_PREP(MICFIL_CTRL2_QSEL, qsel));
}

static int micfil_quality_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct fsl_micfil *micfil = snd_soc_component_get_drvdata(cmpnt);

	ucontrol->value.integer.value[0] = micfil->quality;

	return 0;
}

static int micfil_quality_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct fsl_micfil *micfil = snd_soc_component_get_drvdata(cmpnt);

	micfil->quality = ucontrol->value.integer.value[0];

	return micfil_set_quality(micfil);
}

static const char * const micfil_hwvad_enable[] = {
	"Disable (Record only)",
	"Enable (Record with Vad)",
};

static const char * const micfil_hwvad_init_mode[] = {
	"Envelope mode", "Energy mode",
};

static const char * const micfil_hwvad_hpf_texts[] = {
	"Filter bypass",
	"Cut-off @1750Hz",
	"Cut-off @215Hz",
	"Cut-off @102Hz",
};

/*
 * DC Remover Control
 * Filter Bypassed	1 1
 * Cut-off @21Hz	0 0
 * Cut-off @83Hz	0 1
 * Cut-off @152HZ	1 0
 */
static const char * const micfil_dc_remover_texts[] = {
	"Cut-off @21Hz", "Cut-off @83Hz",
	"Cut-off @152Hz", "Bypass",
};

static const struct soc_enum hwvad_enable_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(micfil_hwvad_enable),
			    micfil_hwvad_enable);
static const struct soc_enum hwvad_init_mode_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(micfil_hwvad_init_mode),
			    micfil_hwvad_init_mode);
static const struct soc_enum hwvad_hpf_enum =
	SOC_ENUM_SINGLE(REG_MICFIL_VAD0_CTRL2, 0,
			ARRAY_SIZE(micfil_hwvad_hpf_texts),
			micfil_hwvad_hpf_texts);
static const struct soc_enum fsl_micfil_dc_remover_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(micfil_dc_remover_texts),
			    micfil_dc_remover_texts);

static int micfil_put_dc_remover_state(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct fsl_micfil *micfil = snd_soc_component_get_drvdata(comp);
	unsigned int *item = ucontrol->value.enumerated.item;
	int val = snd_soc_enum_item_to_val(e, item[0]);
	int i = 0, ret = 0;
	u32 reg_val = 0;

	if (val < 0 || val > 3)
		return -EINVAL;

	micfil->dc_remover = val;

	/* Calculate total value for all channels */
	for (i = 0; i < MICFIL_OUTPUT_CHANNELS; i++)
		reg_val |= val << MICFIL_DC_CHX_SHIFT(i);

	/* Update DC Remover mode for all channels */
	ret = snd_soc_component_update_bits(comp, REG_MICFIL_DC_CTRL,
					    MICFIL_DC_CTRL_CONFIG, reg_val);
	if (ret < 0)
		return ret;

	return 0;
}

static int micfil_get_dc_remover_state(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct fsl_micfil *micfil = snd_soc_component_get_drvdata(comp);

	ucontrol->value.enumerated.item[0] = micfil->dc_remover;

	return 0;
}

static int hwvad_put_enable(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	struct fsl_micfil *micfil = snd_soc_component_get_drvdata(comp);
	int val = snd_soc_enum_item_to_val(e, item[0]);

	micfil->vad_enabled = val;

	return 0;
}

static int hwvad_get_enable(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct fsl_micfil *micfil = snd_soc_component_get_drvdata(comp);

	ucontrol->value.enumerated.item[0] = micfil->vad_enabled;

	return 0;
}

static int hwvad_put_init_mode(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	struct fsl_micfil *micfil = snd_soc_component_get_drvdata(comp);
	int val = snd_soc_enum_item_to_val(e, item[0]);

	/* 0 - Envelope-based Mode
	 * 1 - Energy-based Mode
	 */
	micfil->vad_init_mode = val;

	return 0;
}

static int hwvad_get_init_mode(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct fsl_micfil *micfil = snd_soc_component_get_drvdata(comp);

	ucontrol->value.enumerated.item[0] = micfil->vad_init_mode;

	return 0;
}

static int hwvad_detected(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct fsl_micfil *micfil = snd_soc_component_get_drvdata(comp);

	ucontrol->value.enumerated.item[0] = micfil->vad_detected;

	return 0;
}

static const struct snd_kcontrol_new fsl_micfil_snd_controls[] = {
	SOC_SINGLE_SX_TLV("CH0 Volume", REG_MICFIL_OUT_CTRL,
			  MICFIL_OUTGAIN_CHX_SHIFT(0), 0x8, 0xF, gain_tlv),
	SOC_SINGLE_SX_TLV("CH1 Volume", REG_MICFIL_OUT_CTRL,
			  MICFIL_OUTGAIN_CHX_SHIFT(1), 0x8, 0xF, gain_tlv),
	SOC_SINGLE_SX_TLV("CH2 Volume", REG_MICFIL_OUT_CTRL,
			  MICFIL_OUTGAIN_CHX_SHIFT(2), 0x8, 0xF, gain_tlv),
	SOC_SINGLE_SX_TLV("CH3 Volume", REG_MICFIL_OUT_CTRL,
			  MICFIL_OUTGAIN_CHX_SHIFT(3), 0x8, 0xF, gain_tlv),
	SOC_SINGLE_SX_TLV("CH4 Volume", REG_MICFIL_OUT_CTRL,
			  MICFIL_OUTGAIN_CHX_SHIFT(4), 0x8, 0xF, gain_tlv),
	SOC_SINGLE_SX_TLV("CH5 Volume", REG_MICFIL_OUT_CTRL,
			  MICFIL_OUTGAIN_CHX_SHIFT(5), 0x8, 0xF, gain_tlv),
	SOC_SINGLE_SX_TLV("CH6 Volume", REG_MICFIL_OUT_CTRL,
			  MICFIL_OUTGAIN_CHX_SHIFT(6), 0x8, 0xF, gain_tlv),
	SOC_SINGLE_SX_TLV("CH7 Volume", REG_MICFIL_OUT_CTRL,
			  MICFIL_OUTGAIN_CHX_SHIFT(7), 0x8, 0xF, gain_tlv),
	SOC_ENUM_EXT("MICFIL Quality Select",
		     fsl_micfil_quality_enum,
		     micfil_quality_get, micfil_quality_set),
	SOC_ENUM_EXT("HWVAD Enablement Switch", hwvad_enable_enum,
		     hwvad_get_enable, hwvad_put_enable),
	SOC_ENUM_EXT("HWVAD Initialization Mode", hwvad_init_mode_enum,
		     hwvad_get_init_mode, hwvad_put_init_mode),
	SOC_ENUM("HWVAD High-Pass Filter", hwvad_hpf_enum),
	SOC_SINGLE("HWVAD ZCD Switch", REG_MICFIL_VAD0_ZCD, 0, 1, 0),
	SOC_SINGLE("HWVAD ZCD Auto Threshold Switch",
		   REG_MICFIL_VAD0_ZCD, 2, 1, 0),
	SOC_ENUM_EXT("MICFIL DC Remover Control", fsl_micfil_dc_remover_enum,
		     micfil_get_dc_remover_state, micfil_put_dc_remover_state),
	SOC_SINGLE("HWVAD Input Gain", REG_MICFIL_VAD0_CTRL2, 8, 15, 0),
	SOC_SINGLE("HWVAD Sound Gain", REG_MICFIL_VAD0_SCONFIG, 0, 15, 0),
	SOC_SINGLE("HWVAD Noise Gain", REG_MICFIL_VAD0_NCONFIG, 0, 15, 0),
	SOC_SINGLE_RANGE("HWVAD Detector Frame Time", REG_MICFIL_VAD0_CTRL2, 16, 0, 63, 0),
	SOC_SINGLE("HWVAD Detector Initialization Time", REG_MICFIL_VAD0_CTRL1, 8, 31, 0),
	SOC_SINGLE("HWVAD Noise Filter Adjustment", REG_MICFIL_VAD0_NCONFIG, 8, 31, 0),
	SOC_SINGLE("HWVAD ZCD Threshold", REG_MICFIL_VAD0_ZCD, 16, 1023, 0),
	SOC_SINGLE("HWVAD ZCD Adjustment", REG_MICFIL_VAD0_ZCD, 8, 15, 0),
	SOC_SINGLE("HWVAD ZCD And Behavior Switch",
		   REG_MICFIL_VAD0_ZCD, 4, 1, 0),
	SOC_SINGLE_BOOL_EXT("VAD Detected", 0, hwvad_detected, NULL),
};

static int fsl_micfil_use_verid(struct device *dev)
{
	struct fsl_micfil *micfil = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	if (!micfil->soc->use_verid)
		return 0;

	ret = regmap_read(micfil->regmap, REG_MICFIL_VERID, &val);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "VERID: 0x%016X\n", val);

	micfil->verid.version = val &
		(MICFIL_VERID_MAJOR_MASK | MICFIL_VERID_MINOR_MASK);
	micfil->verid.version >>= MICFIL_VERID_MINOR_SHIFT;
	micfil->verid.feature = val & MICFIL_VERID_FEATURE_MASK;

	ret = regmap_read(micfil->regmap, REG_MICFIL_PARAM, &val);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "PARAM: 0x%016X\n", val);

	micfil->param.hwvad_num = (val & MICFIL_PARAM_NUM_HWVAD_MASK) >>
		MICFIL_PARAM_NUM_HWVAD_SHIFT;
	micfil->param.hwvad_zcd = val & MICFIL_PARAM_HWVAD_ZCD;
	micfil->param.hwvad_energy_mode = val & MICFIL_PARAM_HWVAD_ENERGY_MODE;
	micfil->param.hwvad = val & MICFIL_PARAM_HWVAD;
	micfil->param.dc_out_bypass = val & MICFIL_PARAM_DC_OUT_BYPASS;
	micfil->param.dc_in_bypass = val & MICFIL_PARAM_DC_IN_BYPASS;
	micfil->param.low_power = val & MICFIL_PARAM_LOW_POWER;
	micfil->param.fil_out_width = val & MICFIL_PARAM_FIL_OUT_WIDTH;
	micfil->param.fifo_ptrwid = (val & MICFIL_PARAM_FIFO_PTRWID_MASK) >>
		MICFIL_PARAM_FIFO_PTRWID_SHIFT;
	micfil->param.npair = (val & MICFIL_PARAM_NPAIR_MASK) >>
		MICFIL_PARAM_NPAIR_SHIFT;

	return 0;
}

/* The SRES is a self-negated bit which provides the CPU with the
 * capability to initialize the PDM Interface module through the
 * slave-bus interface. This bit always reads as zero, and this
 * bit is only effective when MDIS is cleared
 */
static int fsl_micfil_reset(struct device *dev)
{
	struct fsl_micfil *micfil = dev_get_drvdata(dev);
	int ret;

	ret = regmap_clear_bits(micfil->regmap, REG_MICFIL_CTRL1,
				MICFIL_CTRL1_MDIS);
	if (ret)
		return ret;

	ret = regmap_set_bits(micfil->regmap, REG_MICFIL_CTRL1,
			      MICFIL_CTRL1_SRES);
	if (ret)
		return ret;

	/*
	 * SRES is self-cleared bit, but REG_MICFIL_CTRL1 is defined
	 * as non-volatile register, so SRES still remain in regmap
	 * cache after set, that every update of REG_MICFIL_CTRL1,
	 * software reset happens. so clear it explicitly.
	 */
	ret = regmap_clear_bits(micfil->regmap, REG_MICFIL_CTRL1,
				MICFIL_CTRL1_SRES);
	if (ret)
		return ret;

	/*
	 * Set SRES should clear CHnF flags, But even add delay here
	 * the CHnF may not be cleared sometimes, so clear CHnF explicitly.
	 */
	ret = regmap_write_bits(micfil->regmap, REG_MICFIL_STAT, 0xFF, 0xFF);
	if (ret)
		return ret;

	return 0;
}

static int fsl_micfil_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct fsl_micfil *micfil = snd_soc_dai_get_drvdata(dai);

	if (!micfil) {
		dev_err(dai->dev, "micfil dai priv_data not set\n");
		return -EINVAL;
	}

	return 0;
}

/* Enable/disable hwvad interrupts */
static int fsl_micfil_configure_hwvad_interrupts(struct fsl_micfil *micfil, int enable)
{
	u32 vadie_reg = enable ? MICFIL_VAD0_CTRL1_IE : 0;
	u32 vaderie_reg = enable ? MICFIL_VAD0_CTRL1_ERIE : 0;

	/* Voice Activity Detector Error Interruption */
	regmap_update_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL1,
			   MICFIL_VAD0_CTRL1_ERIE, vaderie_reg);

	/* Voice Activity Detector Interruption */
	regmap_update_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL1,
			   MICFIL_VAD0_CTRL1_IE, vadie_reg);

	return 0;
}

/* Configuration done only in energy-based initialization mode */
static int fsl_micfil_init_hwvad_energy_mode(struct fsl_micfil *micfil)
{
	/* Keep the VADFRENDIS bitfield cleared. */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL2,
			  MICFIL_VAD0_CTRL2_FRENDIS);

	/* Keep the VADPREFEN bitfield cleared. */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL2,
			  MICFIL_VAD0_CTRL2_PREFEN);

	/* Keep the VADSFILEN bitfield cleared. */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_SCONFIG,
			  MICFIL_VAD0_SCONFIG_SFILEN);

	/* Keep the VADSMAXEN bitfield cleared. */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_SCONFIG,
			  MICFIL_VAD0_SCONFIG_SMAXEN);

	/* Keep the VADNFILAUTO bitfield asserted. */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_NCONFIG,
			MICFIL_VAD0_NCONFIG_NFILAUT);

	/* Keep the VADNMINEN bitfield cleared. */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_NCONFIG,
			  MICFIL_VAD0_NCONFIG_NMINEN);

	/* Keep the VADNDECEN bitfield cleared. */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_NCONFIG,
			  MICFIL_VAD0_NCONFIG_NDECEN);

	/* Keep the VADNOREN bitfield cleared. */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_NCONFIG,
			  MICFIL_VAD0_NCONFIG_NOREN);

	return 0;
}

/* Configuration done only in envelope-based initialization mode */
static int fsl_micfil_init_hwvad_envelope_mode(struct fsl_micfil *micfil)
{
	/* Assert the VADFRENDIS bitfield */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL2,
			MICFIL_VAD0_CTRL2_FRENDIS);

	/* Assert the VADPREFEN bitfield. */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL2,
			MICFIL_VAD0_CTRL2_PREFEN);

	/* Assert the VADSFILEN bitfield. */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_SCONFIG,
			MICFIL_VAD0_SCONFIG_SFILEN);

	/* Assert the VADSMAXEN bitfield. */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_SCONFIG,
			MICFIL_VAD0_SCONFIG_SMAXEN);

	/* Clear the VADNFILAUTO bitfield */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_NCONFIG,
			  MICFIL_VAD0_NCONFIG_NFILAUT);

	/* Assert the VADNMINEN bitfield. */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_NCONFIG,
			MICFIL_VAD0_NCONFIG_NMINEN);

	/* Assert the VADNDECEN bitfield. */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_NCONFIG,
			MICFIL_VAD0_NCONFIG_NDECEN);

	/* Assert VADNOREN bitfield. */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_NCONFIG,
			MICFIL_VAD0_NCONFIG_NOREN);

	return 0;
}

/*
 * Hardware Voice Active Detection: The HWVAD takes data from the input
 * of a selected PDM microphone to detect if there is any
 * voice activity. When a voice activity is detected, an interrupt could
 * be delivered to the system. Initialization in section 8.4:
 * Can work in two modes:
 *  -> Eneveope-based mode (section 8.4.1)
 *  -> Energy-based mode (section 8.4.2)
 *
 * It is important to remark that the HWVAD detector could be enabled
 * or reset only when the MICFIL isn't running i.e. when the BSY_FIL
 * bit in STAT register is cleared
 */
static int fsl_micfil_hwvad_enable(struct fsl_micfil *micfil)
{
	int ret;

	micfil->vad_detected = 0;

	/* envelope-based specific initialization */
	if (micfil->vad_init_mode == MICFIL_HWVAD_ENVELOPE_MODE)
		ret = fsl_micfil_init_hwvad_envelope_mode(micfil);
	else
		ret = fsl_micfil_init_hwvad_energy_mode(micfil);
	if (ret)
		return ret;

	/* Voice Activity Detector Internal Filters Initialization*/
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL1,
			MICFIL_VAD0_CTRL1_ST10);

	/* Voice Activity Detector Internal Filter */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL1,
			  MICFIL_VAD0_CTRL1_ST10);

	/* Enable Interrupts */
	ret = fsl_micfil_configure_hwvad_interrupts(micfil, 1);
	if (ret)
		return ret;

	/* Voice Activity Detector Reset */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL1,
			MICFIL_VAD0_CTRL1_RST);

	/* Voice Activity Detector Enabled */
	regmap_set_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL1,
			MICFIL_VAD0_CTRL1_EN);

	return 0;
}

static int fsl_micfil_hwvad_disable(struct fsl_micfil *micfil)
{
	struct device *dev = &micfil->pdev->dev;
	int ret = 0;

	/* Disable HWVAD */
	regmap_clear_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL1,
			  MICFIL_VAD0_CTRL1_EN);

	/* Disable hwvad interrupts */
	ret = fsl_micfil_configure_hwvad_interrupts(micfil, 0);
	if (ret)
		dev_err(dev, "Failed to disable interrupts\n");

	return ret;
}

static int fsl_micfil_trigger(struct snd_pcm_substream *substream, int cmd,
			      struct snd_soc_dai *dai)
{
	struct fsl_micfil *micfil = snd_soc_dai_get_drvdata(dai);
	struct device *dev = &micfil->pdev->dev;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = fsl_micfil_reset(dev);
		if (ret) {
			dev_err(dev, "failed to soft reset\n");
			return ret;
		}

		/* DMA Interrupt Selection - DISEL bits
		 * 00 - DMA and IRQ disabled
		 * 01 - DMA req enabled
		 * 10 - IRQ enabled
		 * 11 - reserved
		 */
		ret = regmap_update_bits(micfil->regmap, REG_MICFIL_CTRL1,
				MICFIL_CTRL1_DISEL,
				FIELD_PREP(MICFIL_CTRL1_DISEL, MICFIL_CTRL1_DISEL_DMA));
		if (ret)
			return ret;

		/* Enable the module */
		ret = regmap_set_bits(micfil->regmap, REG_MICFIL_CTRL1,
				      MICFIL_CTRL1_PDMIEN);
		if (ret)
			return ret;

		if (micfil->vad_enabled)
			fsl_micfil_hwvad_enable(micfil);

		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (micfil->vad_enabled)
			fsl_micfil_hwvad_disable(micfil);

		/* Disable the module */
		ret = regmap_clear_bits(micfil->regmap, REG_MICFIL_CTRL1,
					MICFIL_CTRL1_PDMIEN);
		if (ret)
			return ret;

		ret = regmap_update_bits(micfil->regmap, REG_MICFIL_CTRL1,
				MICFIL_CTRL1_DISEL,
				FIELD_PREP(MICFIL_CTRL1_DISEL, MICFIL_CTRL1_DISEL_DISABLE));
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int fsl_micfil_reparent_rootclk(struct fsl_micfil *micfil, unsigned int sample_rate)
{
	struct device *dev = &micfil->pdev->dev;
	u64 ratio = sample_rate;
	struct clk *clk;
	int ret;

	/* Get root clock */
	clk = micfil->mclk;

	/* Disable clock first, for it was enabled by pm_runtime */
	clk_disable_unprepare(clk);
	fsl_asoc_reparent_pll_clocks(dev, clk, micfil->pll8k_clk,
				     micfil->pll11k_clk, ratio);
	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	return 0;
}

static int fsl_micfil_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct fsl_micfil *micfil = snd_soc_dai_get_drvdata(dai);
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	int clk_div = 8;
	int osr = MICFIL_OSR_DEFAULT;
	int ret;

	/* 1. Disable the module */
	ret = regmap_clear_bits(micfil->regmap, REG_MICFIL_CTRL1,
				MICFIL_CTRL1_PDMIEN);
	if (ret)
		return ret;

	/* enable channels */
	ret = regmap_update_bits(micfil->regmap, REG_MICFIL_CTRL1,
				 0xFF, ((1 << channels) - 1));
	if (ret)
		return ret;

	ret = fsl_micfil_reparent_rootclk(micfil, rate);
	if (ret)
		return ret;

	ret = clk_set_rate(micfil->mclk, rate * clk_div * osr * 8);
	if (ret)
		return ret;

	ret = micfil_set_quality(micfil);
	if (ret)
		return ret;

	ret = regmap_update_bits(micfil->regmap, REG_MICFIL_CTRL2,
				 MICFIL_CTRL2_CLKDIV | MICFIL_CTRL2_CICOSR,
				 FIELD_PREP(MICFIL_CTRL2_CLKDIV, clk_div) |
				 FIELD_PREP(MICFIL_CTRL2_CICOSR, 16 - osr));

	/* Configure CIC OSR in VADCICOSR */
	regmap_update_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL1,
			   MICFIL_VAD0_CTRL1_CICOSR,
			   FIELD_PREP(MICFIL_VAD0_CTRL1_CICOSR, 16 - osr));

	/* Configure source channel in VADCHSEL */
	regmap_update_bits(micfil->regmap, REG_MICFIL_VAD0_CTRL1,
			   MICFIL_VAD0_CTRL1_CHSEL,
			   FIELD_PREP(MICFIL_VAD0_CTRL1_CHSEL, (channels - 1)));

	micfil->dma_params_rx.peripheral_config = &micfil->sdmacfg;
	micfil->dma_params_rx.peripheral_size = sizeof(micfil->sdmacfg);
	micfil->sdmacfg.n_fifos_src = channels;
	micfil->sdmacfg.sw_done = true;
	micfil->dma_params_rx.maxburst = channels * MICFIL_DMA_MAXBURST_RX;
	if (micfil->soc->use_edma)
		micfil->dma_params_rx.maxburst = channels;

	return 0;
}

static const struct snd_soc_dai_ops fsl_micfil_dai_ops = {
	.startup = fsl_micfil_startup,
	.trigger = fsl_micfil_trigger,
	.hw_params = fsl_micfil_hw_params,
};

static int fsl_micfil_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct fsl_micfil *micfil = dev_get_drvdata(cpu_dai->dev);
	struct device *dev = cpu_dai->dev;
	unsigned int val = 0;
	int ret, i;

	micfil->quality = QUALITY_VLOW0;
	micfil->card = cpu_dai->component->card;

	/* set default gain to 2 */
	regmap_write(micfil->regmap, REG_MICFIL_OUT_CTRL, 0x22222222);

	/* set DC Remover in bypass mode*/
	for (i = 0; i < MICFIL_OUTPUT_CHANNELS; i++)
		val |= MICFIL_DC_BYPASS << MICFIL_DC_CHX_SHIFT(i);
	ret = regmap_update_bits(micfil->regmap, REG_MICFIL_DC_CTRL,
				 MICFIL_DC_CTRL_CONFIG, val);
	if (ret) {
		dev_err(dev, "failed to set DC Remover mode bits\n");
		return ret;
	}
	micfil->dc_remover = MICFIL_DC_BYPASS;

	snd_soc_dai_init_dma_data(cpu_dai, NULL,
				  &micfil->dma_params_rx);

	/* FIFO Watermark Control - FIFOWMK*/
	ret = regmap_update_bits(micfil->regmap, REG_MICFIL_FIFO_CTRL,
			MICFIL_FIFO_CTRL_FIFOWMK,
			FIELD_PREP(MICFIL_FIFO_CTRL_FIFOWMK, micfil->soc->fifo_depth - 1));
	if (ret)
		return ret;

	return 0;
}

static struct snd_soc_dai_driver fsl_micfil_dai = {
	.probe = fsl_micfil_dai_probe,
	.capture = {
		.stream_name = "CPU-Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &fsl_micfil_dai_ops,
};

static const struct snd_soc_component_driver fsl_micfil_component = {
	.name		= "fsl-micfil-dai",
	.controls       = fsl_micfil_snd_controls,
	.num_controls   = ARRAY_SIZE(fsl_micfil_snd_controls),
	.legacy_dai_naming      = 1,
};

/* REGMAP */
static const struct reg_default fsl_micfil_reg_defaults[] = {
	{REG_MICFIL_CTRL1,		0x00000000},
	{REG_MICFIL_CTRL2,		0x00000000},
	{REG_MICFIL_STAT,		0x00000000},
	{REG_MICFIL_FIFO_CTRL,		0x00000007},
	{REG_MICFIL_FIFO_STAT,		0x00000000},
	{REG_MICFIL_DATACH0,		0x00000000},
	{REG_MICFIL_DATACH1,		0x00000000},
	{REG_MICFIL_DATACH2,		0x00000000},
	{REG_MICFIL_DATACH3,		0x00000000},
	{REG_MICFIL_DATACH4,		0x00000000},
	{REG_MICFIL_DATACH5,		0x00000000},
	{REG_MICFIL_DATACH6,		0x00000000},
	{REG_MICFIL_DATACH7,		0x00000000},
	{REG_MICFIL_DC_CTRL,		0x00000000},
	{REG_MICFIL_OUT_CTRL,		0x00000000},
	{REG_MICFIL_OUT_STAT,		0x00000000},
	{REG_MICFIL_VAD0_CTRL1,		0x00000000},
	{REG_MICFIL_VAD0_CTRL2,		0x000A0000},
	{REG_MICFIL_VAD0_STAT,		0x00000000},
	{REG_MICFIL_VAD0_SCONFIG,	0x00000000},
	{REG_MICFIL_VAD0_NCONFIG,	0x80000000},
	{REG_MICFIL_VAD0_NDATA,		0x00000000},
	{REG_MICFIL_VAD0_ZCD,		0x00000004},
};

static bool fsl_micfil_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_MICFIL_CTRL1:
	case REG_MICFIL_CTRL2:
	case REG_MICFIL_STAT:
	case REG_MICFIL_FIFO_CTRL:
	case REG_MICFIL_FIFO_STAT:
	case REG_MICFIL_DATACH0:
	case REG_MICFIL_DATACH1:
	case REG_MICFIL_DATACH2:
	case REG_MICFIL_DATACH3:
	case REG_MICFIL_DATACH4:
	case REG_MICFIL_DATACH5:
	case REG_MICFIL_DATACH6:
	case REG_MICFIL_DATACH7:
	case REG_MICFIL_DC_CTRL:
	case REG_MICFIL_OUT_CTRL:
	case REG_MICFIL_OUT_STAT:
	case REG_MICFIL_FSYNC_CTRL:
	case REG_MICFIL_VERID:
	case REG_MICFIL_PARAM:
	case REG_MICFIL_VAD0_CTRL1:
	case REG_MICFIL_VAD0_CTRL2:
	case REG_MICFIL_VAD0_STAT:
	case REG_MICFIL_VAD0_SCONFIG:
	case REG_MICFIL_VAD0_NCONFIG:
	case REG_MICFIL_VAD0_NDATA:
	case REG_MICFIL_VAD0_ZCD:
		return true;
	default:
		return false;
	}
}

static bool fsl_micfil_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_MICFIL_CTRL1:
	case REG_MICFIL_CTRL2:
	case REG_MICFIL_STAT:		/* Write 1 to Clear */
	case REG_MICFIL_FIFO_CTRL:
	case REG_MICFIL_FIFO_STAT:	/* Write 1 to Clear */
	case REG_MICFIL_DC_CTRL:
	case REG_MICFIL_OUT_CTRL:
	case REG_MICFIL_OUT_STAT:	/* Write 1 to Clear */
	case REG_MICFIL_FSYNC_CTRL:
	case REG_MICFIL_VAD0_CTRL1:
	case REG_MICFIL_VAD0_CTRL2:
	case REG_MICFIL_VAD0_STAT:	/* Write 1 to Clear */
	case REG_MICFIL_VAD0_SCONFIG:
	case REG_MICFIL_VAD0_NCONFIG:
	case REG_MICFIL_VAD0_ZCD:
		return true;
	default:
		return false;
	}
}

static bool fsl_micfil_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case REG_MICFIL_STAT:
	case REG_MICFIL_DATACH0:
	case REG_MICFIL_DATACH1:
	case REG_MICFIL_DATACH2:
	case REG_MICFIL_DATACH3:
	case REG_MICFIL_DATACH4:
	case REG_MICFIL_DATACH5:
	case REG_MICFIL_DATACH6:
	case REG_MICFIL_DATACH7:
	case REG_MICFIL_VERID:
	case REG_MICFIL_PARAM:
	case REG_MICFIL_VAD0_STAT:
	case REG_MICFIL_VAD0_NDATA:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config fsl_micfil_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,

	.max_register = REG_MICFIL_VAD0_ZCD,
	.reg_defaults = fsl_micfil_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(fsl_micfil_reg_defaults),
	.readable_reg = fsl_micfil_readable_reg,
	.volatile_reg = fsl_micfil_volatile_reg,
	.writeable_reg = fsl_micfil_writeable_reg,
	.cache_type = REGCACHE_RBTREE,
};

/* END OF REGMAP */

static irqreturn_t micfil_isr(int irq, void *devid)
{
	struct fsl_micfil *micfil = (struct fsl_micfil *)devid;
	struct platform_device *pdev = micfil->pdev;
	u32 stat_reg;
	u32 fifo_stat_reg;
	u32 ctrl1_reg;
	bool dma_enabled;
	int i;

	regmap_read(micfil->regmap, REG_MICFIL_STAT, &stat_reg);
	regmap_read(micfil->regmap, REG_MICFIL_CTRL1, &ctrl1_reg);
	regmap_read(micfil->regmap, REG_MICFIL_FIFO_STAT, &fifo_stat_reg);

	dma_enabled = FIELD_GET(MICFIL_CTRL1_DISEL, ctrl1_reg) == MICFIL_CTRL1_DISEL_DMA;

	/* Channel 0-7 Output Data Flags */
	for (i = 0; i < MICFIL_OUTPUT_CHANNELS; i++) {
		if (stat_reg & MICFIL_STAT_CHXF(i))
			dev_dbg(&pdev->dev,
				"Data available in Data Channel %d\n", i);
		/* if DMA is not enabled, field must be written with 1
		 * to clear
		 */
		if (!dma_enabled)
			regmap_write_bits(micfil->regmap,
					  REG_MICFIL_STAT,
					  MICFIL_STAT_CHXF(i),
					  1);
	}

	for (i = 0; i < MICFIL_FIFO_NUM; i++) {
		if (fifo_stat_reg & MICFIL_FIFO_STAT_FIFOX_OVER(i))
			dev_dbg(&pdev->dev,
				"FIFO Overflow Exception flag for channel %d\n",
				i);

		if (fifo_stat_reg & MICFIL_FIFO_STAT_FIFOX_UNDER(i))
			dev_dbg(&pdev->dev,
				"FIFO Underflow Exception flag for channel %d\n",
				i);
	}

	return IRQ_HANDLED;
}

static irqreturn_t micfil_err_isr(int irq, void *devid)
{
	struct fsl_micfil *micfil = (struct fsl_micfil *)devid;
	struct platform_device *pdev = micfil->pdev;
	u32 stat_reg;

	regmap_read(micfil->regmap, REG_MICFIL_STAT, &stat_reg);

	if (stat_reg & MICFIL_STAT_BSY_FIL)
		dev_dbg(&pdev->dev, "isr: Decimation Filter is running\n");

	if (stat_reg & MICFIL_STAT_FIR_RDY)
		dev_dbg(&pdev->dev, "isr: FIR Filter Data ready\n");

	if (stat_reg & MICFIL_STAT_LOWFREQF) {
		dev_dbg(&pdev->dev, "isr: ipg_clk_app is too low\n");
		regmap_write_bits(micfil->regmap, REG_MICFIL_STAT,
				  MICFIL_STAT_LOWFREQF, 1);
	}

	return IRQ_HANDLED;
}

static irqreturn_t voice_detected_fn(int irq, void *devid)
{
	struct fsl_micfil *micfil = (struct fsl_micfil *)devid;
	struct snd_kcontrol *kctl;

	if (!micfil->card)
		return IRQ_HANDLED;

	kctl = snd_soc_card_get_kcontrol(micfil->card, "VAD Detected");
	if (!kctl)
		return IRQ_HANDLED;

	if (micfil->vad_detected)
		snd_ctl_notify(micfil->card->snd_card,
			       SNDRV_CTL_EVENT_MASK_VALUE,
			       &kctl->id);

	return IRQ_HANDLED;
}

static irqreturn_t hwvad_isr(int irq, void *devid)
{
	struct fsl_micfil *micfil = (struct fsl_micfil *)devid;
	struct device *dev = &micfil->pdev->dev;
	u32 vad0_reg;
	int ret;

	regmap_read(micfil->regmap, REG_MICFIL_VAD0_STAT, &vad0_reg);

	/*
	 * The only difference between MICFIL_VAD0_STAT_EF and
	 * MICFIL_VAD0_STAT_IF is that the former requires Write
	 * 1 to Clear. Since both flags are set, it is enough
	 * to only read one of them
	 */
	if (vad0_reg & MICFIL_VAD0_STAT_IF) {
		/* Write 1 to clear */
		regmap_write_bits(micfil->regmap, REG_MICFIL_VAD0_STAT,
				  MICFIL_VAD0_STAT_IF,
				  MICFIL_VAD0_STAT_IF);

		micfil->vad_detected = 1;
	}

	ret = fsl_micfil_hwvad_disable(micfil);
	if (ret)
		dev_err(dev, "Failed to disable hwvad\n");

	return IRQ_WAKE_THREAD;
}

static irqreturn_t hwvad_err_isr(int irq, void *devid)
{
	struct fsl_micfil *micfil = (struct fsl_micfil *)devid;
	struct device *dev = &micfil->pdev->dev;
	u32 vad0_reg;

	regmap_read(micfil->regmap, REG_MICFIL_VAD0_STAT, &vad0_reg);

	if (vad0_reg & MICFIL_VAD0_STAT_INSATF)
		dev_dbg(dev, "voice activity input overflow/underflow detected\n");

	return IRQ_HANDLED;
}

static int fsl_micfil_runtime_suspend(struct device *dev);
static int fsl_micfil_runtime_resume(struct device *dev);

static int fsl_micfil_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_micfil *micfil;
	struct resource *res;
	void __iomem *regs;
	int ret, i;

	micfil = devm_kzalloc(&pdev->dev, sizeof(*micfil), GFP_KERNEL);
	if (!micfil)
		return -ENOMEM;

	micfil->pdev = pdev;
	strscpy(micfil->name, np->name, sizeof(micfil->name));

	micfil->soc = of_device_get_match_data(&pdev->dev);

	/* ipg_clk is used to control the registers
	 * ipg_clk_app is used to operate the filter
	 */
	micfil->mclk = devm_clk_get(&pdev->dev, "ipg_clk_app");
	if (IS_ERR(micfil->mclk)) {
		dev_err(&pdev->dev, "failed to get core clock: %ld\n",
			PTR_ERR(micfil->mclk));
		return PTR_ERR(micfil->mclk);
	}

	micfil->busclk = devm_clk_get(&pdev->dev, "ipg_clk");
	if (IS_ERR(micfil->busclk)) {
		dev_err(&pdev->dev, "failed to get ipg clock: %ld\n",
			PTR_ERR(micfil->busclk));
		return PTR_ERR(micfil->busclk);
	}

	fsl_asoc_get_pll_clocks(&pdev->dev, &micfil->pll8k_clk,
				&micfil->pll11k_clk);

	/* init regmap */
	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	micfil->regmap = devm_regmap_init_mmio(&pdev->dev,
					       regs,
					       &fsl_micfil_regmap_config);
	if (IS_ERR(micfil->regmap)) {
		dev_err(&pdev->dev, "failed to init MICFIL regmap: %ld\n",
			PTR_ERR(micfil->regmap));
		return PTR_ERR(micfil->regmap);
	}

	/* dataline mask for RX */
	ret = of_property_read_u32_index(np,
					 "fsl,dataline",
					 0,
					 &micfil->dataline);
	if (ret)
		micfil->dataline = 1;

	if (micfil->dataline & ~micfil->soc->dataline) {
		dev_err(&pdev->dev, "dataline setting error, Mask is 0x%X\n",
			micfil->soc->dataline);
		return -EINVAL;
	}

	/* get IRQs */
	for (i = 0; i < MICFIL_IRQ_LINES; i++) {
		micfil->irq[i] = platform_get_irq(pdev, i);
		if (micfil->irq[i] < 0)
			return micfil->irq[i];
	}

	/* Digital Microphone interface interrupt */
	ret = devm_request_irq(&pdev->dev, micfil->irq[0],
			       micfil_isr, IRQF_SHARED,
			       micfil->name, micfil);
	if (ret) {
		dev_err(&pdev->dev, "failed to claim mic interface irq %u\n",
			micfil->irq[0]);
		return ret;
	}

	/* Digital Microphone interface error interrupt */
	ret = devm_request_irq(&pdev->dev, micfil->irq[1],
			       micfil_err_isr, IRQF_SHARED,
			       micfil->name, micfil);
	if (ret) {
		dev_err(&pdev->dev, "failed to claim mic interface error irq %u\n",
			micfil->irq[1]);
		return ret;
	}

	/* Digital Microphone interface voice activity detector event */
	ret = devm_request_threaded_irq(&pdev->dev, micfil->irq[2],
					hwvad_isr, voice_detected_fn,
					IRQF_SHARED, micfil->name, micfil);
	if (ret) {
		dev_err(&pdev->dev, "failed to claim hwvad event irq %u\n",
			micfil->irq[0]);
		return ret;
	}

	/* Digital Microphone interface voice activity detector error */
	ret = devm_request_irq(&pdev->dev, micfil->irq[3],
			       hwvad_err_isr, IRQF_SHARED,
			       micfil->name, micfil);
	if (ret) {
		dev_err(&pdev->dev, "failed to claim hwvad error irq %u\n",
			micfil->irq[1]);
		return ret;
	}

	micfil->dma_params_rx.chan_name = "rx";
	micfil->dma_params_rx.addr = res->start + REG_MICFIL_DATACH0;
	micfil->dma_params_rx.maxburst = MICFIL_DMA_MAXBURST_RX;

	platform_set_drvdata(pdev, micfil);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = fsl_micfil_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		goto err_pm_get_sync;

	/* Get micfil version */
	ret = fsl_micfil_use_verid(&pdev->dev);
	if (ret < 0)
		dev_warn(&pdev->dev, "Error reading MICFIL version: %d\n", ret);

	ret = pm_runtime_put_sync(&pdev->dev);
	if (ret < 0 && ret != -ENOSYS)
		goto err_pm_get_sync;

	regcache_cache_only(micfil->regmap, true);

	/*
	 * Register platform component before registering cpu dai for there
	 * is not defer probe for platform component in snd_soc_add_pcm_runtime().
	 */
	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "failed to pcm register\n");
		goto err_pm_disable;
	}

	fsl_micfil_dai.capture.formats = micfil->soc->formats;

	ret = devm_snd_soc_register_component(&pdev->dev, &fsl_micfil_component,
					      &fsl_micfil_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "failed to register component %s\n",
			fsl_micfil_component.name);
		goto err_pm_disable;
	}

	return ret;

err_pm_get_sync:
	if (!pm_runtime_status_suspended(&pdev->dev))
		fsl_micfil_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void fsl_micfil_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static int fsl_micfil_runtime_suspend(struct device *dev)
{
	struct fsl_micfil *micfil = dev_get_drvdata(dev);

	regcache_cache_only(micfil->regmap, true);

	clk_disable_unprepare(micfil->mclk);
	clk_disable_unprepare(micfil->busclk);

	return 0;
}

static int fsl_micfil_runtime_resume(struct device *dev)
{
	struct fsl_micfil *micfil = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(micfil->busclk);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(micfil->mclk);
	if (ret < 0) {
		clk_disable_unprepare(micfil->busclk);
		return ret;
	}

	regcache_cache_only(micfil->regmap, false);
	regcache_mark_dirty(micfil->regmap);
	regcache_sync(micfil->regmap);

	return 0;
}

static const struct dev_pm_ops fsl_micfil_pm_ops = {
	SET_RUNTIME_PM_OPS(fsl_micfil_runtime_suspend,
			   fsl_micfil_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver fsl_micfil_driver = {
	.probe = fsl_micfil_probe,
	.remove_new = fsl_micfil_remove,
	.driver = {
		.name = "fsl-micfil-dai",
		.pm = &fsl_micfil_pm_ops,
		.of_match_table = fsl_micfil_dt_ids,
	},
};
module_platform_driver(fsl_micfil_driver);

MODULE_AUTHOR("Cosmin-Gabriel Samoila <cosmin.samoila@nxp.com>");
MODULE_DESCRIPTION("NXP PDM Microphone Interface (MICFIL) driver");
MODULE_LICENSE("Dual BSD/GPL");
