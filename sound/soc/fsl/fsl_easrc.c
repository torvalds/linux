// SPDX-License-Identifier: GPL-2.0
// Copyright 2019 NXP

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/sched/signal.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/gcd.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/core.h>

#include "fsl_easrc.h"
#include "imx-pcm.h"

#define FSL_EASRC_FORMATS       (SNDRV_PCM_FMTBIT_S16_LE | \
				 SNDRV_PCM_FMTBIT_U16_LE | \
				 SNDRV_PCM_FMTBIT_S24_LE | \
				 SNDRV_PCM_FMTBIT_S24_3LE | \
				 SNDRV_PCM_FMTBIT_U24_LE | \
				 SNDRV_PCM_FMTBIT_U24_3LE | \
				 SNDRV_PCM_FMTBIT_S32_LE | \
				 SNDRV_PCM_FMTBIT_U32_LE | \
				 SNDRV_PCM_FMTBIT_S20_3LE | \
				 SNDRV_PCM_FMTBIT_U20_3LE | \
				 SNDRV_PCM_FMTBIT_FLOAT_LE)

static int fsl_easrc_iec958_put_bits(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct fsl_asrc *easrc = snd_soc_component_get_drvdata(comp);
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;
	unsigned int regval = ucontrol->value.integer.value[0];

	easrc_priv->bps_iec958[mc->regbase] = regval;

	return 0;
}

static int fsl_easrc_iec958_get_bits(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct fsl_asrc *easrc = snd_soc_component_get_drvdata(comp);
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;

	ucontrol->value.enumerated.item[0] = easrc_priv->bps_iec958[mc->regbase];

	return 0;
}

static int fsl_easrc_get_reg(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;
	unsigned int regval;

	regval = snd_soc_component_read(component, mc->regbase);

	ucontrol->value.integer.value[0] = regval;

	return 0;
}

static int fsl_easrc_set_reg(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;
	unsigned int regval = ucontrol->value.integer.value[0];
	int ret;

	ret = snd_soc_component_write(component, mc->regbase, regval);
	if (ret < 0)
		return ret;

	return 0;
}

#define SOC_SINGLE_REG_RW(xname, xreg) \
{	.iface = SNDRV_CTL_ELEM_IFACE_PCM, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.info = snd_soc_info_xr_sx, .get = fsl_easrc_get_reg, \
	.put = fsl_easrc_set_reg, \
	.private_value = (unsigned long)&(struct soc_mreg_control) \
		{ .regbase = xreg, .regcount = 1, .nbits = 32, \
		  .invert = 0, .min = 0, .max = 0xffffffff, } }

#define SOC_SINGLE_VAL_RW(xname, xreg) \
{	.iface = SNDRV_CTL_ELEM_IFACE_PCM, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.info = snd_soc_info_xr_sx, .get = fsl_easrc_iec958_get_bits, \
	.put = fsl_easrc_iec958_put_bits, \
	.private_value = (unsigned long)&(struct soc_mreg_control) \
		{ .regbase = xreg, .regcount = 1, .nbits = 32, \
		  .invert = 0, .min = 0, .max = 2, } }

static const struct snd_kcontrol_new fsl_easrc_snd_controls[] = {
	SOC_SINGLE("Context 0 Dither Switch", REG_EASRC_COC(0), 0, 1, 0),
	SOC_SINGLE("Context 1 Dither Switch", REG_EASRC_COC(1), 0, 1, 0),
	SOC_SINGLE("Context 2 Dither Switch", REG_EASRC_COC(2), 0, 1, 0),
	SOC_SINGLE("Context 3 Dither Switch", REG_EASRC_COC(3), 0, 1, 0),

	SOC_SINGLE("Context 0 IEC958 Validity", REG_EASRC_COC(0), 2, 1, 0),
	SOC_SINGLE("Context 1 IEC958 Validity", REG_EASRC_COC(1), 2, 1, 0),
	SOC_SINGLE("Context 2 IEC958 Validity", REG_EASRC_COC(2), 2, 1, 0),
	SOC_SINGLE("Context 3 IEC958 Validity", REG_EASRC_COC(3), 2, 1, 0),

	SOC_SINGLE_VAL_RW("Context 0 IEC958 Bits Per Sample", 0),
	SOC_SINGLE_VAL_RW("Context 1 IEC958 Bits Per Sample", 1),
	SOC_SINGLE_VAL_RW("Context 2 IEC958 Bits Per Sample", 2),
	SOC_SINGLE_VAL_RW("Context 3 IEC958 Bits Per Sample", 3),

	SOC_SINGLE_REG_RW("Context 0 IEC958 CS0", REG_EASRC_CS0(0)),
	SOC_SINGLE_REG_RW("Context 1 IEC958 CS0", REG_EASRC_CS0(1)),
	SOC_SINGLE_REG_RW("Context 2 IEC958 CS0", REG_EASRC_CS0(2)),
	SOC_SINGLE_REG_RW("Context 3 IEC958 CS0", REG_EASRC_CS0(3)),
	SOC_SINGLE_REG_RW("Context 0 IEC958 CS1", REG_EASRC_CS1(0)),
	SOC_SINGLE_REG_RW("Context 1 IEC958 CS1", REG_EASRC_CS1(1)),
	SOC_SINGLE_REG_RW("Context 2 IEC958 CS1", REG_EASRC_CS1(2)),
	SOC_SINGLE_REG_RW("Context 3 IEC958 CS1", REG_EASRC_CS1(3)),
	SOC_SINGLE_REG_RW("Context 0 IEC958 CS2", REG_EASRC_CS2(0)),
	SOC_SINGLE_REG_RW("Context 1 IEC958 CS2", REG_EASRC_CS2(1)),
	SOC_SINGLE_REG_RW("Context 2 IEC958 CS2", REG_EASRC_CS2(2)),
	SOC_SINGLE_REG_RW("Context 3 IEC958 CS2", REG_EASRC_CS2(3)),
	SOC_SINGLE_REG_RW("Context 0 IEC958 CS3", REG_EASRC_CS3(0)),
	SOC_SINGLE_REG_RW("Context 1 IEC958 CS3", REG_EASRC_CS3(1)),
	SOC_SINGLE_REG_RW("Context 2 IEC958 CS3", REG_EASRC_CS3(2)),
	SOC_SINGLE_REG_RW("Context 3 IEC958 CS3", REG_EASRC_CS3(3)),
	SOC_SINGLE_REG_RW("Context 0 IEC958 CS4", REG_EASRC_CS4(0)),
	SOC_SINGLE_REG_RW("Context 1 IEC958 CS4", REG_EASRC_CS4(1)),
	SOC_SINGLE_REG_RW("Context 2 IEC958 CS4", REG_EASRC_CS4(2)),
	SOC_SINGLE_REG_RW("Context 3 IEC958 CS4", REG_EASRC_CS4(3)),
	SOC_SINGLE_REG_RW("Context 0 IEC958 CS5", REG_EASRC_CS5(0)),
	SOC_SINGLE_REG_RW("Context 1 IEC958 CS5", REG_EASRC_CS5(1)),
	SOC_SINGLE_REG_RW("Context 2 IEC958 CS5", REG_EASRC_CS5(2)),
	SOC_SINGLE_REG_RW("Context 3 IEC958 CS5", REG_EASRC_CS5(3)),
};

/*
 * fsl_easrc_set_rs_ratio
 *
 * According to the resample taps, calculate the resample ratio
 * ratio = in_rate / out_rate
 */
static int fsl_easrc_set_rs_ratio(struct fsl_asrc_pair *ctx)
{
	struct fsl_asrc *easrc = ctx->asrc;
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	struct fsl_easrc_ctx_priv *ctx_priv = ctx->private;
	unsigned int in_rate = ctx_priv->in_params.norm_rate;
	unsigned int out_rate = ctx_priv->out_params.norm_rate;
	unsigned int frac_bits;
	u64 val;
	u32 *r;

	switch (easrc_priv->rs_num_taps) {
	case EASRC_RS_32_TAPS:
		/* integer bits = 5; */
		frac_bits = 39;
		break;
	case EASRC_RS_64_TAPS:
		/* integer bits = 6; */
		frac_bits = 38;
		break;
	case EASRC_RS_128_TAPS:
		/* integer bits = 7; */
		frac_bits = 37;
		break;
	default:
		return -EINVAL;
	}

	val = (u64)in_rate << frac_bits;
	do_div(val, out_rate);
	r = (uint32_t *)&val;

	if (r[1] & 0xFFFFF000) {
		dev_err(&easrc->pdev->dev, "ratio exceed range\n");
		return -EINVAL;
	}

	regmap_write(easrc->regmap, REG_EASRC_RRL(ctx->index),
		     EASRC_RRL_RS_RL(r[0]));
	regmap_write(easrc->regmap, REG_EASRC_RRH(ctx->index),
		     EASRC_RRH_RS_RH(r[1]));

	return 0;
}

/* Normalize input and output sample rates */
static void fsl_easrc_normalize_rates(struct fsl_asrc_pair *ctx)
{
	struct fsl_easrc_ctx_priv *ctx_priv;
	int a, b;

	if (!ctx)
		return;

	ctx_priv = ctx->private;

	a = ctx_priv->in_params.sample_rate;
	b = ctx_priv->out_params.sample_rate;

	a = gcd(a, b);

	/* Divide by gcd to normalize the rate */
	ctx_priv->in_params.norm_rate = ctx_priv->in_params.sample_rate / a;
	ctx_priv->out_params.norm_rate = ctx_priv->out_params.sample_rate / a;
}

/* Resets the pointer of the coeff memory pointers */
static int fsl_easrc_coeff_mem_ptr_reset(struct fsl_asrc *easrc,
					 unsigned int ctx_id, int mem_type)
{
	struct device *dev;
	u32 reg, mask, val;

	if (!easrc)
		return -ENODEV;

	dev = &easrc->pdev->dev;

	switch (mem_type) {
	case EASRC_PF_COEFF_MEM:
		/* This resets the prefilter memory pointer addr */
		if (ctx_id >= EASRC_CTX_MAX_NUM) {
			dev_err(dev, "Invalid context id[%d]\n", ctx_id);
			return -EINVAL;
		}

		reg = REG_EASRC_CCE1(ctx_id);
		mask = EASRC_CCE1_COEF_MEM_RST_MASK;
		val = EASRC_CCE1_COEF_MEM_RST;
		break;
	case EASRC_RS_COEFF_MEM:
		/* This resets the resampling memory pointer addr */
		reg = REG_EASRC_CRCC;
		mask = EASRC_CRCC_RS_CPR_MASK;
		val = EASRC_CRCC_RS_CPR;
		break;
	default:
		dev_err(dev, "Unknown memory type\n");
		return -EINVAL;
	}

	/*
	 * To reset the write pointer back to zero, the register field
	 * ASRC_CTX_CTRL_EXT1x[PF_COEFF_MEM_RST] can be toggled from
	 * 0x0 to 0x1 to 0x0.
	 */
	regmap_update_bits(easrc->regmap, reg, mask, 0);
	regmap_update_bits(easrc->regmap, reg, mask, val);
	regmap_update_bits(easrc->regmap, reg, mask, 0);

	return 0;
}

static inline uint32_t bits_taps_to_val(unsigned int t)
{
	switch (t) {
	case EASRC_RS_32_TAPS:
		return 32;
	case EASRC_RS_64_TAPS:
		return 64;
	case EASRC_RS_128_TAPS:
		return 128;
	}

	return 0;
}

static int fsl_easrc_resampler_config(struct fsl_asrc *easrc)
{
	struct device *dev = &easrc->pdev->dev;
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	struct asrc_firmware_hdr *hdr =  easrc_priv->firmware_hdr;
	struct interp_params *interp = easrc_priv->interp;
	struct interp_params *selected_interp = NULL;
	unsigned int num_coeff;
	unsigned int i;
	u64 *coef;
	u32 *r;
	int ret;

	if (!hdr) {
		dev_err(dev, "firmware not loaded!\n");
		return -ENODEV;
	}

	for (i = 0; i < hdr->interp_scen; i++) {
		if ((interp[i].num_taps - 1) !=
		    bits_taps_to_val(easrc_priv->rs_num_taps))
			continue;

		coef = interp[i].coeff;
		selected_interp = &interp[i];
		dev_dbg(dev, "Selected interp_filter: %u taps - %u phases\n",
			selected_interp->num_taps,
			selected_interp->num_phases);
		break;
	}

	if (!selected_interp) {
		dev_err(dev, "failed to get interpreter configuration\n");
		return -EINVAL;
	}

	/*
	 * RS_LOW - first half of center tap of the sinc function
	 * RS_HIGH - second half of center tap of the sinc function
	 * This is due to the fact the resampling function must be
	 * symetrical - i.e. odd number of taps
	 */
	r = (uint32_t *)&selected_interp->center_tap;
	regmap_write(easrc->regmap, REG_EASRC_RCTCL, EASRC_RCTCL_RS_CL(r[0]));
	regmap_write(easrc->regmap, REG_EASRC_RCTCH, EASRC_RCTCH_RS_CH(r[1]));

	/*
	 * Write Number of Resampling Coefficient Taps
	 * 00b - 32-Tap Resampling Filter
	 * 01b - 64-Tap Resampling Filter
	 * 10b - 128-Tap Resampling Filter
	 * 11b - N/A
	 */
	regmap_update_bits(easrc->regmap, REG_EASRC_CRCC,
			   EASRC_CRCC_RS_TAPS_MASK,
			   EASRC_CRCC_RS_TAPS(easrc_priv->rs_num_taps));

	/* Reset prefilter coefficient pointer back to 0 */
	ret = fsl_easrc_coeff_mem_ptr_reset(easrc, 0, EASRC_RS_COEFF_MEM);
	if (ret)
		return ret;

	/*
	 * When the filter is programmed to run in:
	 * 32-tap mode, 16-taps, 128-phases 4-coefficients per phase
	 * 64-tap mode, 32-taps, 64-phases 4-coefficients per phase
	 * 128-tap mode, 64-taps, 32-phases 4-coefficients per phase
	 * This means the number of writes is constant no matter
	 * the mode we are using
	 */
	num_coeff = 16 * 128 * 4;

	for (i = 0; i < num_coeff; i++) {
		r = (uint32_t *)&coef[i];
		regmap_write(easrc->regmap, REG_EASRC_CRCM,
			     EASRC_CRCM_RS_CWD(r[0]));
		regmap_write(easrc->regmap, REG_EASRC_CRCM,
			     EASRC_CRCM_RS_CWD(r[1]));
	}

	return 0;
}

/**
 *  fsl_easrc_normalize_filter - Scale filter coefficients (64 bits float)
 *  For input float32 normalized range (1.0,-1.0) -> output int[16,24,32]:
 *      scale it by multiplying filter coefficients by 2^31
 *  For input int[16, 24, 32] -> output float32
 *      scale it by multiplying filter coefficients by 2^-15, 2^-23, 2^-31
 *  input:
 *      @easrc:  Structure pointer of fsl_asrc
 *      @infilter : Pointer to non-scaled input filter
 *      @shift:  The multiply factor
 *  output:
 *      @outfilter: scaled filter
 */
static int fsl_easrc_normalize_filter(struct fsl_asrc *easrc,
				      u64 *infilter,
				      u64 *outfilter,
				      int shift)
{
	struct device *dev = &easrc->pdev->dev;
	u64 coef = *infilter;
	s64 exp  = (coef & 0x7ff0000000000000ll) >> 52;
	u64 outcoef;

	/*
	 * If exponent is zero (value == 0), or 7ff (value == NaNs)
	 * dont touch the content
	 */
	if (exp == 0 || exp == 0x7ff) {
		*outfilter = coef;
		return 0;
	}

	/* coef * 2^shift ==> exp + shift */
	exp += shift;

	if ((shift > 0 && exp >= 0x7ff) || (shift < 0 && exp <= 0)) {
		dev_err(dev, "coef out of range\n");
		return -EINVAL;
	}

	outcoef = (u64)(coef & 0x800FFFFFFFFFFFFFll) + ((u64)exp << 52);
	*outfilter = outcoef;

	return 0;
}

static int fsl_easrc_write_pf_coeff_mem(struct fsl_asrc *easrc, int ctx_id,
					u64 *coef, int n_taps, int shift)
{
	struct device *dev = &easrc->pdev->dev;
	int ret = 0;
	int i;
	u32 *r;
	u64 tmp;

	/* If STx_NUM_TAPS is set to 0x0 then return */
	if (!n_taps)
		return 0;

	if (!coef) {
		dev_err(dev, "coef table is NULL\n");
		return -EINVAL;
	}

	/*
	 * When switching between stages, the address pointer
	 * should be reset back to 0x0 before performing a write
	 */
	ret = fsl_easrc_coeff_mem_ptr_reset(easrc, ctx_id, EASRC_PF_COEFF_MEM);
	if (ret)
		return ret;

	for (i = 0; i < (n_taps + 1) / 2; i++) {
		ret = fsl_easrc_normalize_filter(easrc, &coef[i], &tmp, shift);
		if (ret)
			return ret;

		r = (uint32_t *)&tmp;
		regmap_write(easrc->regmap, REG_EASRC_PCF(ctx_id),
			     EASRC_PCF_CD(r[0]));
		regmap_write(easrc->regmap, REG_EASRC_PCF(ctx_id),
			     EASRC_PCF_CD(r[1]));
	}

	return 0;
}

static int fsl_easrc_prefilter_config(struct fsl_asrc *easrc,
				      unsigned int ctx_id)
{
	struct prefil_params *prefil, *selected_prefil = NULL;
	struct fsl_easrc_ctx_priv *ctx_priv;
	struct fsl_easrc_priv *easrc_priv;
	struct asrc_firmware_hdr *hdr;
	struct fsl_asrc_pair *ctx;
	struct device *dev;
	u32 inrate, outrate, offset = 0;
	u32 in_s_rate, out_s_rate, in_s_fmt, out_s_fmt;
	int ret, i;

	if (!easrc)
		return -ENODEV;

	dev = &easrc->pdev->dev;

	if (ctx_id >= EASRC_CTX_MAX_NUM) {
		dev_err(dev, "Invalid context id[%d]\n", ctx_id);
		return -EINVAL;
	}

	easrc_priv = easrc->private;

	ctx = easrc->pair[ctx_id];
	ctx_priv = ctx->private;

	in_s_rate = ctx_priv->in_params.sample_rate;
	out_s_rate = ctx_priv->out_params.sample_rate;
	in_s_fmt = ctx_priv->in_params.sample_format;
	out_s_fmt = ctx_priv->out_params.sample_format;

	ctx_priv->in_filled_sample = bits_taps_to_val(easrc_priv->rs_num_taps) / 2;
	ctx_priv->out_missed_sample = ctx_priv->in_filled_sample * out_s_rate / in_s_rate;

	ctx_priv->st1_num_taps = 0;
	ctx_priv->st2_num_taps = 0;

	regmap_write(easrc->regmap, REG_EASRC_CCE1(ctx_id), 0);
	regmap_write(easrc->regmap, REG_EASRC_CCE2(ctx_id), 0);

	/*
	 * The audio float point data range is (-1, 1), the asrc would output
	 * all zero for float point input and integer output case, that is to
	 * drop the fractional part of the data directly.
	 *
	 * In order to support float to int conversion or int to float
	 * conversion we need to do special operation on the coefficient to
	 * enlarge/reduce the data to the expected range.
	 *
	 * For float to int case:
	 * Up sampling:
	 * 1. Create a 1 tap filter with center tap (only tap) of 2^31
	 *    in 64 bits floating point.
	 *    double value = (double)(((uint64_t)1) << 31)
	 * 2. Program 1 tap prefilter with center tap above.
	 *
	 * Down sampling,
	 * 1. If the filter is single stage filter, add "shift" to the exponent
	 *    of stage 1 coefficients.
	 * 2. If the filter is two stage filter , add "shift" to the exponent
	 *    of stage 2 coefficients.
	 *
	 * The "shift" is 31, same for int16, int24, int32 case.
	 *
	 * For int to float case:
	 * Up sampling:
	 * 1. Create a 1 tap filter with center tap (only tap) of 2^-31
	 *    in 64 bits floating point.
	 * 2. Program 1 tap prefilter with center tap above.
	 *
	 * Down sampling,
	 * 1. If the filter is single stage filter, subtract "shift" to the
	 *    exponent of stage 1 coefficients.
	 * 2. If the filter is two stage filter , subtract "shift" to the
	 *    exponent of stage 2 coefficients.
	 *
	 * The "shift" is 15,23,31, different for int16, int24, int32 case.
	 *
	 */
	if (out_s_rate >= in_s_rate) {
		if (out_s_rate == in_s_rate)
			regmap_update_bits(easrc->regmap,
					   REG_EASRC_CCE1(ctx_id),
					   EASRC_CCE1_RS_BYPASS_MASK,
					   EASRC_CCE1_RS_BYPASS);

		ctx_priv->st1_num_taps = 1;
		ctx_priv->st1_coeff    = &easrc_priv->const_coeff;
		ctx_priv->st1_num_exp  = 1;
		ctx_priv->st2_num_taps = 0;

		if (in_s_fmt == SNDRV_PCM_FORMAT_FLOAT_LE &&
		    out_s_fmt != SNDRV_PCM_FORMAT_FLOAT_LE)
			ctx_priv->st1_addexp = 31;
		else if (in_s_fmt != SNDRV_PCM_FORMAT_FLOAT_LE &&
			 out_s_fmt == SNDRV_PCM_FORMAT_FLOAT_LE)
			ctx_priv->st1_addexp -= ctx_priv->in_params.fmt.addexp;
	} else {
		inrate = ctx_priv->in_params.norm_rate;
		outrate = ctx_priv->out_params.norm_rate;

		hdr = easrc_priv->firmware_hdr;
		prefil = easrc_priv->prefil;

		for (i = 0; i < hdr->prefil_scen; i++) {
			if (inrate == prefil[i].insr &&
			    outrate == prefil[i].outsr) {
				selected_prefil = &prefil[i];
				dev_dbg(dev, "Selected prefilter: %u insr, %u outsr, %u st1_taps, %u st2_taps\n",
					selected_prefil->insr,
					selected_prefil->outsr,
					selected_prefil->st1_taps,
					selected_prefil->st2_taps);
				break;
			}
		}

		if (!selected_prefil) {
			dev_err(dev, "Conversion from in ratio %u(%u) to out ratio %u(%u) is not supported\n",
				in_s_rate, inrate,
				out_s_rate, outrate);
			return -EINVAL;
		}

		/*
		 * In prefilter coeff array, first st1_num_taps represent the
		 * stage1 prefilter coefficients followed by next st2_num_taps
		 * representing stage 2 coefficients
		 */
		ctx_priv->st1_num_taps = selected_prefil->st1_taps;
		ctx_priv->st1_coeff    = selected_prefil->coeff;
		ctx_priv->st1_num_exp  = selected_prefil->st1_exp;

		offset = ((selected_prefil->st1_taps + 1) / 2);
		ctx_priv->st2_num_taps = selected_prefil->st2_taps;
		ctx_priv->st2_coeff    = selected_prefil->coeff + offset;

		if (in_s_fmt == SNDRV_PCM_FORMAT_FLOAT_LE &&
		    out_s_fmt != SNDRV_PCM_FORMAT_FLOAT_LE) {
			/* only change stage2 coefficient for 2 stage case */
			if (ctx_priv->st2_num_taps > 0)
				ctx_priv->st2_addexp = 31;
			else
				ctx_priv->st1_addexp = 31;
		} else if (in_s_fmt != SNDRV_PCM_FORMAT_FLOAT_LE &&
			   out_s_fmt == SNDRV_PCM_FORMAT_FLOAT_LE) {
			if (ctx_priv->st2_num_taps > 0)
				ctx_priv->st2_addexp -= ctx_priv->in_params.fmt.addexp;
			else
				ctx_priv->st1_addexp -= ctx_priv->in_params.fmt.addexp;
		}
	}

	ctx_priv->in_filled_sample += (ctx_priv->st1_num_taps / 2) * ctx_priv->st1_num_exp +
				  ctx_priv->st2_num_taps / 2;
	ctx_priv->out_missed_sample = ctx_priv->in_filled_sample * out_s_rate / in_s_rate;

	if (ctx_priv->in_filled_sample * out_s_rate % in_s_rate != 0)
		ctx_priv->out_missed_sample += 1;
	/*
	 * To modify the value of a prefilter coefficient, the user must
	 * perform a write to the register ASRC_PRE_COEFF_FIFOn[COEFF_DATA]
	 * while the respective context RUN_EN bit is set to 0b0
	 */
	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx_id),
			   EASRC_CC_EN_MASK, 0);

	if (ctx_priv->st1_num_taps > EASRC_MAX_PF_TAPS) {
		dev_err(dev, "ST1 taps [%d] mus be lower than %d\n",
			ctx_priv->st1_num_taps, EASRC_MAX_PF_TAPS);
		ret = -EINVAL;
		goto ctx_error;
	}

	/* Update ctx ST1_NUM_TAPS in Context Control Extended 2 register */
	regmap_update_bits(easrc->regmap, REG_EASRC_CCE2(ctx_id),
			   EASRC_CCE2_ST1_TAPS_MASK,
			   EASRC_CCE2_ST1_TAPS(ctx_priv->st1_num_taps - 1));

	/* Prefilter Coefficient Write Select to write in ST1 coeff */
	regmap_update_bits(easrc->regmap, REG_EASRC_CCE1(ctx_id),
			   EASRC_CCE1_COEF_WS_MASK,
			   EASRC_PF_ST1_COEFF_WR << EASRC_CCE1_COEF_WS_SHIFT);

	ret = fsl_easrc_write_pf_coeff_mem(easrc, ctx_id,
					   ctx_priv->st1_coeff,
					   ctx_priv->st1_num_taps,
					   ctx_priv->st1_addexp);
	if (ret)
		goto ctx_error;

	if (ctx_priv->st2_num_taps > 0) {
		if (ctx_priv->st2_num_taps + ctx_priv->st1_num_taps > EASRC_MAX_PF_TAPS) {
			dev_err(dev, "ST2 taps [%d] mus be lower than %d\n",
				ctx_priv->st2_num_taps, EASRC_MAX_PF_TAPS);
			ret = -EINVAL;
			goto ctx_error;
		}

		regmap_update_bits(easrc->regmap, REG_EASRC_CCE1(ctx_id),
				   EASRC_CCE1_PF_TSEN_MASK,
				   EASRC_CCE1_PF_TSEN);
		/*
		 * Enable prefilter stage1 writeback floating point
		 * which is used for FLOAT_LE case
		 */
		regmap_update_bits(easrc->regmap, REG_EASRC_CCE1(ctx_id),
				   EASRC_CCE1_PF_ST1_WBFP_MASK,
				   EASRC_CCE1_PF_ST1_WBFP);

		regmap_update_bits(easrc->regmap, REG_EASRC_CCE1(ctx_id),
				   EASRC_CCE1_PF_EXP_MASK,
				   EASRC_CCE1_PF_EXP(ctx_priv->st1_num_exp - 1));

		/* Update ctx ST2_NUM_TAPS in Context Control Extended 2 reg */
		regmap_update_bits(easrc->regmap, REG_EASRC_CCE2(ctx_id),
				   EASRC_CCE2_ST2_TAPS_MASK,
				   EASRC_CCE2_ST2_TAPS(ctx_priv->st2_num_taps - 1));

		/* Prefilter Coefficient Write Select to write in ST2 coeff */
		regmap_update_bits(easrc->regmap, REG_EASRC_CCE1(ctx_id),
				   EASRC_CCE1_COEF_WS_MASK,
				   EASRC_PF_ST2_COEFF_WR << EASRC_CCE1_COEF_WS_SHIFT);

		ret = fsl_easrc_write_pf_coeff_mem(easrc, ctx_id,
						   ctx_priv->st2_coeff,
						   ctx_priv->st2_num_taps,
						   ctx_priv->st2_addexp);
		if (ret)
			goto ctx_error;
	}

	return 0;

ctx_error:
	return ret;
}

static int fsl_easrc_max_ch_for_slot(struct fsl_asrc_pair *ctx,
				     struct fsl_easrc_slot *slot)
{
	struct fsl_easrc_ctx_priv *ctx_priv = ctx->private;
	int st1_mem_alloc = 0, st2_mem_alloc = 0;
	int pf_mem_alloc = 0;
	int max_channels = 8 - slot->num_channel;
	int channels = 0;

	if (ctx_priv->st1_num_taps > 0) {
		if (ctx_priv->st2_num_taps > 0)
			st1_mem_alloc =
				(ctx_priv->st1_num_taps - 1) * ctx_priv->st1_num_exp + 1;
		else
			st1_mem_alloc = ctx_priv->st1_num_taps;
	}

	if (ctx_priv->st2_num_taps > 0)
		st2_mem_alloc = ctx_priv->st2_num_taps;

	pf_mem_alloc = st1_mem_alloc + st2_mem_alloc;

	if (pf_mem_alloc != 0)
		channels = (6144 - slot->pf_mem_used) / pf_mem_alloc;
	else
		channels = 8;

	if (channels < max_channels)
		max_channels = channels;

	return max_channels;
}

static int fsl_easrc_config_one_slot(struct fsl_asrc_pair *ctx,
				     struct fsl_easrc_slot *slot,
				     unsigned int slot_ctx_idx,
				     unsigned int *req_channels,
				     unsigned int *start_channel,
				     unsigned int *avail_channel)
{
	struct fsl_asrc *easrc = ctx->asrc;
	struct fsl_easrc_ctx_priv *ctx_priv = ctx->private;
	int st1_chanxexp, st1_mem_alloc = 0, st2_mem_alloc;
	unsigned int reg0, reg1, reg2, reg3;
	unsigned int addr;

	if (slot->slot_index == 0) {
		reg0 = REG_EASRC_DPCS0R0(slot_ctx_idx);
		reg1 = REG_EASRC_DPCS0R1(slot_ctx_idx);
		reg2 = REG_EASRC_DPCS0R2(slot_ctx_idx);
		reg3 = REG_EASRC_DPCS0R3(slot_ctx_idx);
	} else {
		reg0 = REG_EASRC_DPCS1R0(slot_ctx_idx);
		reg1 = REG_EASRC_DPCS1R1(slot_ctx_idx);
		reg2 = REG_EASRC_DPCS1R2(slot_ctx_idx);
		reg3 = REG_EASRC_DPCS1R3(slot_ctx_idx);
	}

	if (*req_channels <= *avail_channel) {
		slot->num_channel = *req_channels;
		*req_channels = 0;
	} else {
		slot->num_channel = *avail_channel;
		*req_channels -= *avail_channel;
	}

	slot->min_channel = *start_channel;
	slot->max_channel = *start_channel + slot->num_channel - 1;
	slot->ctx_index = ctx->index;
	slot->busy = true;
	*start_channel += slot->num_channel;

	regmap_update_bits(easrc->regmap, reg0,
			   EASRC_DPCS0R0_MAXCH_MASK,
			   EASRC_DPCS0R0_MAXCH(slot->max_channel));

	regmap_update_bits(easrc->regmap, reg0,
			   EASRC_DPCS0R0_MINCH_MASK,
			   EASRC_DPCS0R0_MINCH(slot->min_channel));

	regmap_update_bits(easrc->regmap, reg0,
			   EASRC_DPCS0R0_NUMCH_MASK,
			   EASRC_DPCS0R0_NUMCH(slot->num_channel - 1));

	regmap_update_bits(easrc->regmap, reg0,
			   EASRC_DPCS0R0_CTXNUM_MASK,
			   EASRC_DPCS0R0_CTXNUM(slot->ctx_index));

	if (ctx_priv->st1_num_taps > 0) {
		if (ctx_priv->st2_num_taps > 0)
			st1_mem_alloc =
				(ctx_priv->st1_num_taps - 1) * slot->num_channel *
				ctx_priv->st1_num_exp + slot->num_channel;
		else
			st1_mem_alloc = ctx_priv->st1_num_taps * slot->num_channel;

		slot->pf_mem_used = st1_mem_alloc;
		regmap_update_bits(easrc->regmap, reg2,
				   EASRC_DPCS0R2_ST1_MA_MASK,
				   EASRC_DPCS0R2_ST1_MA(st1_mem_alloc));

		if (slot->slot_index == 1)
			addr = PREFILTER_MEM_LEN - st1_mem_alloc;
		else
			addr = 0;

		regmap_update_bits(easrc->regmap, reg2,
				   EASRC_DPCS0R2_ST1_SA_MASK,
				   EASRC_DPCS0R2_ST1_SA(addr));
	}

	if (ctx_priv->st2_num_taps > 0) {
		st1_chanxexp = slot->num_channel * (ctx_priv->st1_num_exp - 1);

		regmap_update_bits(easrc->regmap, reg1,
				   EASRC_DPCS0R1_ST1_EXP_MASK,
				   EASRC_DPCS0R1_ST1_EXP(st1_chanxexp));

		st2_mem_alloc = slot->num_channel * ctx_priv->st2_num_taps;
		slot->pf_mem_used += st2_mem_alloc;
		regmap_update_bits(easrc->regmap, reg3,
				   EASRC_DPCS0R3_ST2_MA_MASK,
				   EASRC_DPCS0R3_ST2_MA(st2_mem_alloc));

		if (slot->slot_index == 1)
			addr = PREFILTER_MEM_LEN - st1_mem_alloc - st2_mem_alloc;
		else
			addr = st1_mem_alloc;

		regmap_update_bits(easrc->regmap, reg3,
				   EASRC_DPCS0R3_ST2_SA_MASK,
				   EASRC_DPCS0R3_ST2_SA(addr));
	}

	regmap_update_bits(easrc->regmap, reg0,
			   EASRC_DPCS0R0_EN_MASK, EASRC_DPCS0R0_EN);

	return 0;
}

/*
 * fsl_easrc_config_slot
 *
 * A single context can be split amongst any of the 4 context processing pipes
 * in the design.
 * The total number of channels consumed within the context processor must be
 * less than or equal to 8. if a single context is configured to contain more
 * than 8 channels then it must be distributed across multiple context
 * processing pipe slots.
 *
 */
static int fsl_easrc_config_slot(struct fsl_asrc *easrc, unsigned int ctx_id)
{
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	struct fsl_asrc_pair *ctx = easrc->pair[ctx_id];
	int req_channels = ctx->channels;
	int start_channel = 0, avail_channel;
	struct fsl_easrc_slot *slot0, *slot1;
	struct fsl_easrc_slot *slota, *slotb;
	int i, ret;

	if (req_channels <= 0)
		return -EINVAL;

	for (i = 0; i < EASRC_CTX_MAX_NUM; i++) {
		slot0 = &easrc_priv->slot[i][0];
		slot1 = &easrc_priv->slot[i][1];

		if (slot0->busy && slot1->busy) {
			continue;
		} else if ((slot0->busy && slot0->ctx_index == ctx->index) ||
			 (slot1->busy && slot1->ctx_index == ctx->index)) {
			continue;
		} else if (!slot0->busy) {
			slota = slot0;
			slotb = slot1;
			slota->slot_index = 0;
		} else if (!slot1->busy) {
			slota = slot1;
			slotb = slot0;
			slota->slot_index = 1;
		}

		if (!slota || !slotb)
			continue;

		avail_channel = fsl_easrc_max_ch_for_slot(ctx, slotb);
		if (avail_channel <= 0)
			continue;

		ret = fsl_easrc_config_one_slot(ctx, slota, i, &req_channels,
						&start_channel, &avail_channel);
		if (ret)
			return ret;

		if (req_channels > 0)
			continue;
		else
			break;
	}

	if (req_channels > 0) {
		dev_err(&easrc->pdev->dev, "no avail slot.\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * fsl_easrc_release_slot
 *
 * Clear the slot configuration
 */
static int fsl_easrc_release_slot(struct fsl_asrc *easrc, unsigned int ctx_id)
{
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	struct fsl_asrc_pair *ctx = easrc->pair[ctx_id];
	int i;

	for (i = 0; i < EASRC_CTX_MAX_NUM; i++) {
		if (easrc_priv->slot[i][0].busy &&
		    easrc_priv->slot[i][0].ctx_index == ctx->index) {
			easrc_priv->slot[i][0].busy = false;
			easrc_priv->slot[i][0].num_channel = 0;
			easrc_priv->slot[i][0].pf_mem_used = 0;
			/* set registers */
			regmap_write(easrc->regmap, REG_EASRC_DPCS0R0(i), 0);
			regmap_write(easrc->regmap, REG_EASRC_DPCS0R1(i), 0);
			regmap_write(easrc->regmap, REG_EASRC_DPCS0R2(i), 0);
			regmap_write(easrc->regmap, REG_EASRC_DPCS0R3(i), 0);
		}

		if (easrc_priv->slot[i][1].busy &&
		    easrc_priv->slot[i][1].ctx_index == ctx->index) {
			easrc_priv->slot[i][1].busy = false;
			easrc_priv->slot[i][1].num_channel = 0;
			easrc_priv->slot[i][1].pf_mem_used = 0;
			/* set registers */
			regmap_write(easrc->regmap, REG_EASRC_DPCS1R0(i), 0);
			regmap_write(easrc->regmap, REG_EASRC_DPCS1R1(i), 0);
			regmap_write(easrc->regmap, REG_EASRC_DPCS1R2(i), 0);
			regmap_write(easrc->regmap, REG_EASRC_DPCS1R3(i), 0);
		}
	}

	return 0;
}

/*
 * fsl_easrc_config_context
 *
 * Configure the register relate with context.
 */
static int fsl_easrc_config_context(struct fsl_asrc *easrc, unsigned int ctx_id)
{
	struct fsl_easrc_ctx_priv *ctx_priv;
	struct fsl_asrc_pair *ctx;
	struct device *dev;
	unsigned long lock_flags;
	int ret;

	if (!easrc)
		return -ENODEV;

	dev = &easrc->pdev->dev;

	if (ctx_id >= EASRC_CTX_MAX_NUM) {
		dev_err(dev, "Invalid context id[%d]\n", ctx_id);
		return -EINVAL;
	}

	ctx = easrc->pair[ctx_id];

	ctx_priv = ctx->private;

	fsl_easrc_normalize_rates(ctx);

	ret = fsl_easrc_set_rs_ratio(ctx);
	if (ret)
		return ret;

	/* Initialize the context coeficients */
	ret = fsl_easrc_prefilter_config(easrc, ctx->index);
	if (ret)
		return ret;

	spin_lock_irqsave(&easrc->lock, lock_flags);
	ret = fsl_easrc_config_slot(easrc, ctx->index);
	spin_unlock_irqrestore(&easrc->lock, lock_flags);
	if (ret)
		return ret;

	/*
	 * Both prefilter and resampling filters can use following
	 * initialization modes:
	 * 2 - zero-fil mode
	 * 1 - replication mode
	 * 0 - software control
	 */
	regmap_update_bits(easrc->regmap, REG_EASRC_CCE1(ctx_id),
			   EASRC_CCE1_RS_INIT_MASK,
			   EASRC_CCE1_RS_INIT(ctx_priv->rs_init_mode));

	regmap_update_bits(easrc->regmap, REG_EASRC_CCE1(ctx_id),
			   EASRC_CCE1_PF_INIT_MASK,
			   EASRC_CCE1_PF_INIT(ctx_priv->pf_init_mode));

	/*
	 * Context Input FIFO Watermark
	 * DMA request is generated when input FIFO < FIFO_WTMK
	 */
	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx_id),
			   EASRC_CC_FIFO_WTMK_MASK,
			   EASRC_CC_FIFO_WTMK(ctx_priv->in_params.fifo_wtmk));

	/*
	 * Context Output FIFO Watermark
	 * DMA request is generated when output FIFO > FIFO_WTMK
	 * So we set fifo_wtmk -1 to register.
	 */
	regmap_update_bits(easrc->regmap, REG_EASRC_COC(ctx_id),
			   EASRC_COC_FIFO_WTMK_MASK,
			   EASRC_COC_FIFO_WTMK(ctx_priv->out_params.fifo_wtmk - 1));

	/* Number of channels */
	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx_id),
			   EASRC_CC_CHEN_MASK,
			   EASRC_CC_CHEN(ctx->channels - 1));
	return 0;
}

static int fsl_easrc_process_format(struct fsl_asrc_pair *ctx,
				    struct fsl_easrc_data_fmt *fmt,
				    snd_pcm_format_t raw_fmt)
{
	struct fsl_asrc *easrc = ctx->asrc;
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	int ret;

	if (!fmt)
		return -EINVAL;

	/*
	 * Context Input Floating Point Format
	 * 0 - Integer Format
	 * 1 - Single Precision FP Format
	 */
	fmt->floating_point = !snd_pcm_format_linear(raw_fmt);
	fmt->sample_pos = 0;
	fmt->iec958 = 0;

	/* Get the data width */
	switch (snd_pcm_format_width(raw_fmt)) {
	case 16:
		fmt->width = EASRC_WIDTH_16_BIT;
		fmt->addexp = 15;
		break;
	case 20:
		fmt->width = EASRC_WIDTH_20_BIT;
		fmt->addexp = 19;
		break;
	case 24:
		fmt->width = EASRC_WIDTH_24_BIT;
		fmt->addexp = 23;
		break;
	case 32:
		fmt->width = EASRC_WIDTH_32_BIT;
		fmt->addexp = 31;
		break;
	default:
		return -EINVAL;
	}

	switch (raw_fmt) {
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
		fmt->width = easrc_priv->bps_iec958[ctx->index];
		fmt->iec958 = 1;
		fmt->floating_point = 0;
		if (fmt->width == EASRC_WIDTH_16_BIT) {
			fmt->sample_pos = 12;
			fmt->addexp = 15;
		} else if (fmt->width == EASRC_WIDTH_20_BIT) {
			fmt->sample_pos = 8;
			fmt->addexp = 19;
		} else if (fmt->width == EASRC_WIDTH_24_BIT) {
			fmt->sample_pos = 4;
			fmt->addexp = 23;
		}
		break;
	default:
		break;
	}

	/*
	 * Data Endianness
	 * 0 - Little-Endian
	 * 1 - Big-Endian
	 */
	ret = snd_pcm_format_big_endian(raw_fmt);
	if (ret < 0)
		return ret;

	fmt->endianness = ret;

	/*
	 * Input Data sign
	 * 0b - Signed Format
	 * 1b - Unsigned Format
	 */
	fmt->unsign = snd_pcm_format_unsigned(raw_fmt) > 0 ? 1 : 0;

	return 0;
}

static int fsl_easrc_set_ctx_format(struct fsl_asrc_pair *ctx,
				    snd_pcm_format_t *in_raw_format,
				    snd_pcm_format_t *out_raw_format)
{
	struct fsl_asrc *easrc = ctx->asrc;
	struct fsl_easrc_ctx_priv *ctx_priv = ctx->private;
	struct fsl_easrc_data_fmt *in_fmt = &ctx_priv->in_params.fmt;
	struct fsl_easrc_data_fmt *out_fmt = &ctx_priv->out_params.fmt;
	int ret = 0;

	/* Get the bitfield values for input data format */
	if (in_raw_format && out_raw_format) {
		ret = fsl_easrc_process_format(ctx, in_fmt, *in_raw_format);
		if (ret)
			return ret;
	}

	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx->index),
			   EASRC_CC_BPS_MASK,
			   EASRC_CC_BPS(in_fmt->width));
	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx->index),
			   EASRC_CC_ENDIANNESS_MASK,
			   in_fmt->endianness << EASRC_CC_ENDIANNESS_SHIFT);
	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx->index),
			   EASRC_CC_FMT_MASK,
			   in_fmt->floating_point << EASRC_CC_FMT_SHIFT);
	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx->index),
			   EASRC_CC_INSIGN_MASK,
			   in_fmt->unsign << EASRC_CC_INSIGN_SHIFT);

	/* In Sample Position */
	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx->index),
			   EASRC_CC_SAMPLE_POS_MASK,
			   EASRC_CC_SAMPLE_POS(in_fmt->sample_pos));

	/* Get the bitfield values for input data format */
	if (in_raw_format && out_raw_format) {
		ret = fsl_easrc_process_format(ctx, out_fmt, *out_raw_format);
		if (ret)
			return ret;
	}

	regmap_update_bits(easrc->regmap, REG_EASRC_COC(ctx->index),
			   EASRC_COC_BPS_MASK,
			   EASRC_COC_BPS(out_fmt->width));
	regmap_update_bits(easrc->regmap, REG_EASRC_COC(ctx->index),
			   EASRC_COC_ENDIANNESS_MASK,
			   out_fmt->endianness << EASRC_COC_ENDIANNESS_SHIFT);
	regmap_update_bits(easrc->regmap, REG_EASRC_COC(ctx->index),
			   EASRC_COC_FMT_MASK,
			   out_fmt->floating_point << EASRC_COC_FMT_SHIFT);
	regmap_update_bits(easrc->regmap, REG_EASRC_COC(ctx->index),
			   EASRC_COC_OUTSIGN_MASK,
			   out_fmt->unsign << EASRC_COC_OUTSIGN_SHIFT);

	/* Out Sample Position */
	regmap_update_bits(easrc->regmap, REG_EASRC_COC(ctx->index),
			   EASRC_COC_SAMPLE_POS_MASK,
			   EASRC_COC_SAMPLE_POS(out_fmt->sample_pos));

	regmap_update_bits(easrc->regmap, REG_EASRC_COC(ctx->index),
			   EASRC_COC_IEC_EN_MASK,
			   out_fmt->iec958 << EASRC_COC_IEC_EN_SHIFT);

	return ret;
}

/*
 * The ASRC provides interleaving support in hardware to ensure that a
 * variety of sample sources can be internally combined
 * to conform with this format. Interleaving parameters are accessed
 * through the ASRC_CTRL_IN_ACCESSa and ASRC_CTRL_OUT_ACCESSa registers
 */
static int fsl_easrc_set_ctx_organziation(struct fsl_asrc_pair *ctx)
{
	struct fsl_easrc_ctx_priv *ctx_priv;
	struct fsl_asrc *easrc;

	if (!ctx)
		return -ENODEV;

	easrc = ctx->asrc;
	ctx_priv = ctx->private;

	/* input interleaving parameters */
	regmap_update_bits(easrc->regmap, REG_EASRC_CIA(ctx->index),
			   EASRC_CIA_ITER_MASK,
			   EASRC_CIA_ITER(ctx_priv->in_params.iterations));
	regmap_update_bits(easrc->regmap, REG_EASRC_CIA(ctx->index),
			   EASRC_CIA_GRLEN_MASK,
			   EASRC_CIA_GRLEN(ctx_priv->in_params.group_len));
	regmap_update_bits(easrc->regmap, REG_EASRC_CIA(ctx->index),
			   EASRC_CIA_ACCLEN_MASK,
			   EASRC_CIA_ACCLEN(ctx_priv->in_params.access_len));

	/* output interleaving parameters */
	regmap_update_bits(easrc->regmap, REG_EASRC_COA(ctx->index),
			   EASRC_COA_ITER_MASK,
			   EASRC_COA_ITER(ctx_priv->out_params.iterations));
	regmap_update_bits(easrc->regmap, REG_EASRC_COA(ctx->index),
			   EASRC_COA_GRLEN_MASK,
			   EASRC_COA_GRLEN(ctx_priv->out_params.group_len));
	regmap_update_bits(easrc->regmap, REG_EASRC_COA(ctx->index),
			   EASRC_COA_ACCLEN_MASK,
			   EASRC_COA_ACCLEN(ctx_priv->out_params.access_len));

	return 0;
}

/*
 * Request one of the available contexts
 *
 * Returns a negative number on error and >=0 as context id
 * on success
 */
static int fsl_easrc_request_context(int channels, struct fsl_asrc_pair *ctx)
{
	enum asrc_pair_index index = ASRC_INVALID_PAIR;
	struct fsl_asrc *easrc = ctx->asrc;
	struct device *dev;
	unsigned long lock_flags;
	int ret = 0;
	int i;

	dev = &easrc->pdev->dev;

	spin_lock_irqsave(&easrc->lock, lock_flags);

	for (i = ASRC_PAIR_A; i < EASRC_CTX_MAX_NUM; i++) {
		if (easrc->pair[i])
			continue;

		index = i;
		break;
	}

	if (index == ASRC_INVALID_PAIR) {
		dev_err(dev, "all contexts are busy\n");
		ret = -EBUSY;
	} else if (channels > easrc->channel_avail) {
		dev_err(dev, "can't give the required channels: %d\n",
			channels);
		ret = -EINVAL;
	} else {
		ctx->index = index;
		ctx->channels = channels;
		easrc->pair[index] = ctx;
		easrc->channel_avail -= channels;
	}

	spin_unlock_irqrestore(&easrc->lock, lock_flags);

	return ret;
}

/*
 * Release the context
 *
 * This funciton is mainly doing the revert thing in request context
 */
static void fsl_easrc_release_context(struct fsl_asrc_pair *ctx)
{
	unsigned long lock_flags;
	struct fsl_asrc *easrc;

	if (!ctx)
		return;

	easrc = ctx->asrc;

	spin_lock_irqsave(&easrc->lock, lock_flags);

	fsl_easrc_release_slot(easrc, ctx->index);

	easrc->channel_avail += ctx->channels;
	easrc->pair[ctx->index] = NULL;

	spin_unlock_irqrestore(&easrc->lock, lock_flags);
}

/*
 * Start the context
 *
 * Enable the DMA request and context
 */
static int fsl_easrc_start_context(struct fsl_asrc_pair *ctx)
{
	struct fsl_asrc *easrc = ctx->asrc;

	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx->index),
			   EASRC_CC_FWMDE_MASK, EASRC_CC_FWMDE);
	regmap_update_bits(easrc->regmap, REG_EASRC_COC(ctx->index),
			   EASRC_COC_FWMDE_MASK, EASRC_COC_FWMDE);
	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx->index),
			   EASRC_CC_EN_MASK, EASRC_CC_EN);
	return 0;
}

/*
 * Stop the context
 *
 * Disable the DMA request and context
 */
static int fsl_easrc_stop_context(struct fsl_asrc_pair *ctx)
{
	struct fsl_asrc *easrc = ctx->asrc;
	int val, i;
	int size;
	int retry = 200;

	regmap_read(easrc->regmap, REG_EASRC_CC(ctx->index), &val);

	if (val & EASRC_CC_EN_MASK) {
		regmap_update_bits(easrc->regmap,
				   REG_EASRC_CC(ctx->index),
				   EASRC_CC_STOP_MASK, EASRC_CC_STOP);
		do {
			regmap_read(easrc->regmap, REG_EASRC_SFS(ctx->index), &val);
			val &= EASRC_SFS_NSGO_MASK;
			size = val >> EASRC_SFS_NSGO_SHIFT;

			/* Read FIFO, drop the data */
			for (i = 0; i < size * ctx->channels; i++)
				regmap_read(easrc->regmap, REG_EASRC_RDFIFO(ctx->index), &val);
			/* Check RUN_STOP_DONE */
			regmap_read(easrc->regmap, REG_EASRC_IRQF, &val);
			if (val & EASRC_IRQF_RSD(1 << ctx->index)) {
				/*Clear RUN_STOP_DONE*/
				regmap_write_bits(easrc->regmap,
						  REG_EASRC_IRQF,
						  EASRC_IRQF_RSD(1 << ctx->index),
						  EASRC_IRQF_RSD(1 << ctx->index));
				break;
			}
			udelay(100);
		} while (--retry);

		if (retry == 0)
			dev_warn(&easrc->pdev->dev, "RUN STOP fail\n");
	}

	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx->index),
			   EASRC_CC_EN_MASK | EASRC_CC_STOP_MASK, 0);
	regmap_update_bits(easrc->regmap, REG_EASRC_CC(ctx->index),
			   EASRC_CC_FWMDE_MASK, 0);
	regmap_update_bits(easrc->regmap, REG_EASRC_COC(ctx->index),
			   EASRC_COC_FWMDE_MASK, 0);
	return 0;
}

static struct dma_chan *fsl_easrc_get_dma_channel(struct fsl_asrc_pair *ctx,
						  bool dir)
{
	struct fsl_asrc *easrc = ctx->asrc;
	enum asrc_pair_index index = ctx->index;
	char name[8];

	/* Example of dma name: ctx0_rx */
	sprintf(name, "ctx%c_%cx", index + '0', dir == IN ? 'r' : 't');

	return dma_request_slave_channel(&easrc->pdev->dev, name);
};

static const unsigned int easrc_rates[] = {
	8000, 11025, 12000, 16000,
	22050, 24000, 32000, 44100,
	48000, 64000, 88200, 96000,
	128000, 176400, 192000, 256000,
	352800, 384000, 705600, 768000,
};

static const struct snd_pcm_hw_constraint_list easrc_rate_constraints = {
	.count = ARRAY_SIZE(easrc_rates),
	.list = easrc_rates,
};

static int fsl_easrc_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &easrc_rate_constraints);
}

static int fsl_easrc_trigger(struct snd_pcm_substream *substream,
			     int cmd, struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *ctx = runtime->private_data;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = fsl_easrc_start_context(ctx);
		if (ret)
			return ret;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = fsl_easrc_stop_context(ctx);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fsl_easrc_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct fsl_asrc *easrc = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = &easrc->pdev->dev;
	struct fsl_asrc_pair *ctx = runtime->private_data;
	struct fsl_easrc_ctx_priv *ctx_priv = ctx->private;
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	snd_pcm_format_t format = params_format(params);
	int ret;

	ret = fsl_easrc_request_context(channels, ctx);
	if (ret) {
		dev_err(dev, "failed to request context\n");
		return ret;
	}

	ctx_priv->ctx_streams |= BIT(substream->stream);

	/*
	 * Set the input and output ratio so we can compute
	 * the resampling ratio in RS_LOW/HIGH
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ctx_priv->in_params.sample_rate = rate;
		ctx_priv->in_params.sample_format = format;
		ctx_priv->out_params.sample_rate = easrc->asrc_rate;
		ctx_priv->out_params.sample_format = easrc->asrc_format;
	} else {
		ctx_priv->out_params.sample_rate = rate;
		ctx_priv->out_params.sample_format = format;
		ctx_priv->in_params.sample_rate = easrc->asrc_rate;
		ctx_priv->in_params.sample_format = easrc->asrc_format;
	}

	ctx->channels = channels;
	ctx_priv->in_params.fifo_wtmk  = 0x20;
	ctx_priv->out_params.fifo_wtmk = 0x20;

	/*
	 * Do only rate conversion and keep the same format for input
	 * and output data
	 */
	ret = fsl_easrc_set_ctx_format(ctx,
				       &ctx_priv->in_params.sample_format,
				       &ctx_priv->out_params.sample_format);
	if (ret) {
		dev_err(dev, "failed to set format %d", ret);
		return ret;
	}

	ret = fsl_easrc_config_context(easrc, ctx->index);
	if (ret) {
		dev_err(dev, "failed to config context\n");
		return ret;
	}

	ctx_priv->in_params.iterations = 1;
	ctx_priv->in_params.group_len = ctx->channels;
	ctx_priv->in_params.access_len = ctx->channels;
	ctx_priv->out_params.iterations = 1;
	ctx_priv->out_params.group_len = ctx->channels;
	ctx_priv->out_params.access_len = ctx->channels;

	ret = fsl_easrc_set_ctx_organziation(ctx);
	if (ret) {
		dev_err(dev, "failed to set fifo organization\n");
		return ret;
	}

	return 0;
}

static int fsl_easrc_hw_free(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fsl_asrc_pair *ctx = runtime->private_data;
	struct fsl_easrc_ctx_priv *ctx_priv;

	if (!ctx)
		return -EINVAL;

	ctx_priv = ctx->private;

	if (ctx_priv->ctx_streams & BIT(substream->stream)) {
		ctx_priv->ctx_streams &= ~BIT(substream->stream);
		fsl_easrc_release_context(ctx);
	}

	return 0;
}

static const struct snd_soc_dai_ops fsl_easrc_dai_ops = {
	.startup = fsl_easrc_startup,
	.trigger = fsl_easrc_trigger,
	.hw_params = fsl_easrc_hw_params,
	.hw_free = fsl_easrc_hw_free,
};

static int fsl_easrc_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct fsl_asrc *easrc = dev_get_drvdata(cpu_dai->dev);

	snd_soc_dai_init_dma_data(cpu_dai,
				  &easrc->dma_params_tx,
				  &easrc->dma_params_rx);
	return 0;
}

static struct snd_soc_dai_driver fsl_easrc_dai = {
	.probe = fsl_easrc_dai_probe,
	.playback = {
		.stream_name = "ASRC-Playback",
		.channels_min = 1,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 768000,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = FSL_EASRC_FORMATS,
	},
	.capture = {
		.stream_name = "ASRC-Capture",
		.channels_min = 1,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 768000,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = FSL_EASRC_FORMATS |
			   SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE,
	},
	.ops = &fsl_easrc_dai_ops,
};

static const struct snd_soc_component_driver fsl_easrc_component = {
	.name		= "fsl-easrc-dai",
	.controls       = fsl_easrc_snd_controls,
	.num_controls   = ARRAY_SIZE(fsl_easrc_snd_controls),
};

static const struct reg_default fsl_easrc_reg_defaults[] = {
	{REG_EASRC_WRFIFO(0),	0x00000000},
	{REG_EASRC_WRFIFO(1),	0x00000000},
	{REG_EASRC_WRFIFO(2),	0x00000000},
	{REG_EASRC_WRFIFO(3),	0x00000000},
	{REG_EASRC_RDFIFO(0),	0x00000000},
	{REG_EASRC_RDFIFO(1),	0x00000000},
	{REG_EASRC_RDFIFO(2),	0x00000000},
	{REG_EASRC_RDFIFO(3),	0x00000000},
	{REG_EASRC_CC(0),	0x00000000},
	{REG_EASRC_CC(1),	0x00000000},
	{REG_EASRC_CC(2),	0x00000000},
	{REG_EASRC_CC(3),	0x00000000},
	{REG_EASRC_CCE1(0),	0x00000000},
	{REG_EASRC_CCE1(1),	0x00000000},
	{REG_EASRC_CCE1(2),	0x00000000},
	{REG_EASRC_CCE1(3),	0x00000000},
	{REG_EASRC_CCE2(0),	0x00000000},
	{REG_EASRC_CCE2(1),	0x00000000},
	{REG_EASRC_CCE2(2),	0x00000000},
	{REG_EASRC_CCE2(3),	0x00000000},
	{REG_EASRC_CIA(0),	0x00000000},
	{REG_EASRC_CIA(1),	0x00000000},
	{REG_EASRC_CIA(2),	0x00000000},
	{REG_EASRC_CIA(3),	0x00000000},
	{REG_EASRC_DPCS0R0(0),	0x00000000},
	{REG_EASRC_DPCS0R0(1),	0x00000000},
	{REG_EASRC_DPCS0R0(2),	0x00000000},
	{REG_EASRC_DPCS0R0(3),	0x00000000},
	{REG_EASRC_DPCS0R1(0),	0x00000000},
	{REG_EASRC_DPCS0R1(1),	0x00000000},
	{REG_EASRC_DPCS0R1(2),	0x00000000},
	{REG_EASRC_DPCS0R1(3),	0x00000000},
	{REG_EASRC_DPCS0R2(0),	0x00000000},
	{REG_EASRC_DPCS0R2(1),	0x00000000},
	{REG_EASRC_DPCS0R2(2),	0x00000000},
	{REG_EASRC_DPCS0R2(3),	0x00000000},
	{REG_EASRC_DPCS0R3(0),	0x00000000},
	{REG_EASRC_DPCS0R3(1),	0x00000000},
	{REG_EASRC_DPCS0R3(2),	0x00000000},
	{REG_EASRC_DPCS0R3(3),	0x00000000},
	{REG_EASRC_DPCS1R0(0),	0x00000000},
	{REG_EASRC_DPCS1R0(1),	0x00000000},
	{REG_EASRC_DPCS1R0(2),	0x00000000},
	{REG_EASRC_DPCS1R0(3),	0x00000000},
	{REG_EASRC_DPCS1R1(0),	0x00000000},
	{REG_EASRC_DPCS1R1(1),	0x00000000},
	{REG_EASRC_DPCS1R1(2),	0x00000000},
	{REG_EASRC_DPCS1R1(3),	0x00000000},
	{REG_EASRC_DPCS1R2(0),	0x00000000},
	{REG_EASRC_DPCS1R2(1),	0x00000000},
	{REG_EASRC_DPCS1R2(2),	0x00000000},
	{REG_EASRC_DPCS1R2(3),	0x00000000},
	{REG_EASRC_DPCS1R3(0),	0x00000000},
	{REG_EASRC_DPCS1R3(1),	0x00000000},
	{REG_EASRC_DPCS1R3(2),	0x00000000},
	{REG_EASRC_DPCS1R3(3),	0x00000000},
	{REG_EASRC_COC(0),	0x00000000},
	{REG_EASRC_COC(1),	0x00000000},
	{REG_EASRC_COC(2),	0x00000000},
	{REG_EASRC_COC(3),	0x00000000},
	{REG_EASRC_COA(0),	0x00000000},
	{REG_EASRC_COA(1),	0x00000000},
	{REG_EASRC_COA(2),	0x00000000},
	{REG_EASRC_COA(3),	0x00000000},
	{REG_EASRC_SFS(0),	0x00000000},
	{REG_EASRC_SFS(1),	0x00000000},
	{REG_EASRC_SFS(2),	0x00000000},
	{REG_EASRC_SFS(3),	0x00000000},
	{REG_EASRC_RRL(0),	0x00000000},
	{REG_EASRC_RRL(1),	0x00000000},
	{REG_EASRC_RRL(2),	0x00000000},
	{REG_EASRC_RRL(3),	0x00000000},
	{REG_EASRC_RRH(0),	0x00000000},
	{REG_EASRC_RRH(1),	0x00000000},
	{REG_EASRC_RRH(2),	0x00000000},
	{REG_EASRC_RRH(3),	0x00000000},
	{REG_EASRC_RUC(0),	0x00000000},
	{REG_EASRC_RUC(1),	0x00000000},
	{REG_EASRC_RUC(2),	0x00000000},
	{REG_EASRC_RUC(3),	0x00000000},
	{REG_EASRC_RUR(0),	0x7FFFFFFF},
	{REG_EASRC_RUR(1),	0x7FFFFFFF},
	{REG_EASRC_RUR(2),	0x7FFFFFFF},
	{REG_EASRC_RUR(3),	0x7FFFFFFF},
	{REG_EASRC_RCTCL,	0x00000000},
	{REG_EASRC_RCTCH,	0x00000000},
	{REG_EASRC_PCF(0),	0x00000000},
	{REG_EASRC_PCF(1),	0x00000000},
	{REG_EASRC_PCF(2),	0x00000000},
	{REG_EASRC_PCF(3),	0x00000000},
	{REG_EASRC_CRCM,	0x00000000},
	{REG_EASRC_CRCC,	0x00000000},
	{REG_EASRC_IRQC,	0x00000FFF},
	{REG_EASRC_IRQF,	0x00000000},
	{REG_EASRC_CS0(0),	0x00000000},
	{REG_EASRC_CS0(1),	0x00000000},
	{REG_EASRC_CS0(2),	0x00000000},
	{REG_EASRC_CS0(3),	0x00000000},
	{REG_EASRC_CS1(0),	0x00000000},
	{REG_EASRC_CS1(1),	0x00000000},
	{REG_EASRC_CS1(2),	0x00000000},
	{REG_EASRC_CS1(3),	0x00000000},
	{REG_EASRC_CS2(0),	0x00000000},
	{REG_EASRC_CS2(1),	0x00000000},
	{REG_EASRC_CS2(2),	0x00000000},
	{REG_EASRC_CS2(3),	0x00000000},
	{REG_EASRC_CS3(0),	0x00000000},
	{REG_EASRC_CS3(1),	0x00000000},
	{REG_EASRC_CS3(2),	0x00000000},
	{REG_EASRC_CS3(3),	0x00000000},
	{REG_EASRC_CS4(0),	0x00000000},
	{REG_EASRC_CS4(1),	0x00000000},
	{REG_EASRC_CS4(2),	0x00000000},
	{REG_EASRC_CS4(3),	0x00000000},
	{REG_EASRC_CS5(0),	0x00000000},
	{REG_EASRC_CS5(1),	0x00000000},
	{REG_EASRC_CS5(2),	0x00000000},
	{REG_EASRC_CS5(3),	0x00000000},
	{REG_EASRC_DBGC,	0x00000000},
	{REG_EASRC_DBGS,	0x00000000},
};

static const struct regmap_range fsl_easrc_readable_ranges[] = {
	regmap_reg_range(REG_EASRC_RDFIFO(0), REG_EASRC_RCTCH),
	regmap_reg_range(REG_EASRC_PCF(0), REG_EASRC_PCF(3)),
	regmap_reg_range(REG_EASRC_CRCC, REG_EASRC_DBGS),
};

static const struct regmap_access_table fsl_easrc_readable_table = {
	.yes_ranges = fsl_easrc_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(fsl_easrc_readable_ranges),
};

static const struct regmap_range fsl_easrc_writeable_ranges[] = {
	regmap_reg_range(REG_EASRC_WRFIFO(0), REG_EASRC_WRFIFO(3)),
	regmap_reg_range(REG_EASRC_CC(0), REG_EASRC_COA(3)),
	regmap_reg_range(REG_EASRC_RRL(0), REG_EASRC_RCTCH),
	regmap_reg_range(REG_EASRC_PCF(0), REG_EASRC_DBGC),
};

static const struct regmap_access_table fsl_easrc_writeable_table = {
	.yes_ranges = fsl_easrc_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(fsl_easrc_writeable_ranges),
};

static const struct regmap_range fsl_easrc_volatileable_ranges[] = {
	regmap_reg_range(REG_EASRC_RDFIFO(0), REG_EASRC_RDFIFO(3)),
	regmap_reg_range(REG_EASRC_SFS(0), REG_EASRC_SFS(3)),
	regmap_reg_range(REG_EASRC_IRQF, REG_EASRC_IRQF),
	regmap_reg_range(REG_EASRC_DBGS, REG_EASRC_DBGS),
};

static const struct regmap_access_table fsl_easrc_volatileable_table = {
	.yes_ranges = fsl_easrc_volatileable_ranges,
	.n_yes_ranges = ARRAY_SIZE(fsl_easrc_volatileable_ranges),
};

static const struct regmap_config fsl_easrc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,

	.max_register = REG_EASRC_DBGS,
	.reg_defaults = fsl_easrc_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(fsl_easrc_reg_defaults),
	.rd_table = &fsl_easrc_readable_table,
	.wr_table = &fsl_easrc_writeable_table,
	.volatile_table = &fsl_easrc_volatileable_table,
	.cache_type = REGCACHE_RBTREE,
};

#ifdef DEBUG
static void fsl_easrc_dump_firmware(struct fsl_asrc *easrc)
{
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	struct asrc_firmware_hdr *firm = easrc_priv->firmware_hdr;
	struct interp_params *interp = easrc_priv->interp;
	struct prefil_params *prefil = easrc_priv->prefil;
	struct device *dev = &easrc->pdev->dev;
	int i;

	if (firm->magic != FIRMWARE_MAGIC) {
		dev_err(dev, "Wrong magic. Something went wrong!");
		return;
	}

	dev_dbg(dev, "Firmware v%u dump:\n", firm->firmware_version);
	dev_dbg(dev, "Num prefilter scenarios: %u\n", firm->prefil_scen);
	dev_dbg(dev, "Num interpolation scenarios: %u\n", firm->interp_scen);
	dev_dbg(dev, "\nInterpolation scenarios:\n");

	for (i = 0; i < firm->interp_scen; i++) {
		if (interp[i].magic != FIRMWARE_MAGIC) {
			dev_dbg(dev, "%d. wrong interp magic: %x\n",
				i, interp[i].magic);
			continue;
		}
		dev_dbg(dev, "%d. taps: %u, phases: %u, center: %llu\n", i,
			interp[i].num_taps, interp[i].num_phases,
			interp[i].center_tap);
	}

	for (i = 0; i < firm->prefil_scen; i++) {
		if (prefil[i].magic != FIRMWARE_MAGIC) {
			dev_dbg(dev, "%d. wrong prefil magic: %x\n",
				i, prefil[i].magic);
			continue;
		}
		dev_dbg(dev, "%d. insr: %u, outsr: %u, st1: %u, st2: %u\n", i,
			prefil[i].insr, prefil[i].outsr,
			prefil[i].st1_taps, prefil[i].st2_taps);
	}

	dev_dbg(dev, "end of firmware dump\n");
}
#endif

static int fsl_easrc_get_firmware(struct fsl_asrc *easrc)
{
	struct fsl_easrc_priv *easrc_priv;
	const struct firmware **fw_p;
	u32 pnum, inum, offset;
	const u8 *data;
	int ret;

	if (!easrc)
		return -EINVAL;

	easrc_priv = easrc->private;
	fw_p = &easrc_priv->fw;

	ret = request_firmware(fw_p, easrc_priv->fw_name, &easrc->pdev->dev);
	if (ret)
		return ret;

	data = easrc_priv->fw->data;

	easrc_priv->firmware_hdr = (struct asrc_firmware_hdr *)data;
	pnum = easrc_priv->firmware_hdr->prefil_scen;
	inum = easrc_priv->firmware_hdr->interp_scen;

	if (inum) {
		offset = sizeof(struct asrc_firmware_hdr);
		easrc_priv->interp = (struct interp_params *)(data + offset);
	}

	if (pnum) {
		offset = sizeof(struct asrc_firmware_hdr) +
				inum * sizeof(struct interp_params);
		easrc_priv->prefil = (struct prefil_params *)(data + offset);
	}

#ifdef DEBUG
	fsl_easrc_dump_firmware(easrc);
#endif

	return 0;
}

static irqreturn_t fsl_easrc_isr(int irq, void *dev_id)
{
	struct fsl_asrc *easrc = (struct fsl_asrc *)dev_id;
	struct device *dev = &easrc->pdev->dev;
	int val;

	regmap_read(easrc->regmap, REG_EASRC_IRQF, &val);

	if (val & EASRC_IRQF_OER_MASK)
		dev_dbg(dev, "output FIFO underflow\n");

	if (val & EASRC_IRQF_IFO_MASK)
		dev_dbg(dev, "input FIFO overflow\n");

	return IRQ_HANDLED;
}

static int fsl_easrc_get_fifo_addr(u8 dir, enum asrc_pair_index index)
{
	return REG_EASRC_FIFO(dir, index);
}

static const struct of_device_id fsl_easrc_dt_ids[] = {
	{ .compatible = "fsl,imx8mn-easrc",},
	{}
};
MODULE_DEVICE_TABLE(of, fsl_easrc_dt_ids);

static int fsl_easrc_probe(struct platform_device *pdev)
{
	struct fsl_easrc_priv *easrc_priv;
	struct device *dev = &pdev->dev;
	struct fsl_asrc *easrc;
	struct resource *res;
	struct device_node *np;
	void __iomem *regs;
	int ret, irq;

	easrc = devm_kzalloc(dev, sizeof(*easrc), GFP_KERNEL);
	if (!easrc)
		return -ENOMEM;

	easrc_priv = devm_kzalloc(dev, sizeof(*easrc_priv), GFP_KERNEL);
	if (!easrc_priv)
		return -ENOMEM;

	easrc->pdev = pdev;
	easrc->private = easrc_priv;
	np = dev->of_node;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	easrc->paddr = res->start;

	easrc->regmap = devm_regmap_init_mmio(dev, regs, &fsl_easrc_regmap_config);
	if (IS_ERR(easrc->regmap)) {
		dev_err(dev, "failed to init regmap");
		return PTR_ERR(easrc->regmap);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq for node %pOF\n", np);
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, fsl_easrc_isr, 0,
			       dev_name(dev), easrc);
	if (ret) {
		dev_err(dev, "failed to claim irq %u: %d\n", irq, ret);
		return ret;
	}

	easrc->mem_clk = devm_clk_get(dev, "mem");
	if (IS_ERR(easrc->mem_clk)) {
		dev_err(dev, "failed to get mem clock\n");
		return PTR_ERR(easrc->mem_clk);
	}

	/* Set default value */
	easrc->channel_avail = 32;
	easrc->get_dma_channel = fsl_easrc_get_dma_channel;
	easrc->request_pair = fsl_easrc_request_context;
	easrc->release_pair = fsl_easrc_release_context;
	easrc->get_fifo_addr = fsl_easrc_get_fifo_addr;
	easrc->pair_priv_size = sizeof(struct fsl_easrc_ctx_priv);

	easrc_priv->rs_num_taps = EASRC_RS_32_TAPS;
	easrc_priv->const_coeff = 0x3FF0000000000000;

	ret = of_property_read_u32(np, "fsl,asrc-rate", &easrc->asrc_rate);
	if (ret) {
		dev_err(dev, "failed to asrc rate\n");
		return ret;
	}

	ret = of_property_read_u32(np, "fsl,asrc-format", &easrc->asrc_format);
	if (ret) {
		dev_err(dev, "failed to asrc format\n");
		return ret;
	}

	if (!(FSL_EASRC_FORMATS & (1ULL << easrc->asrc_format))) {
		dev_warn(dev, "unsupported format, switching to S24_LE\n");
		easrc->asrc_format = SNDRV_PCM_FORMAT_S24_LE;
	}

	ret = of_property_read_string(np, "firmware-name",
				      &easrc_priv->fw_name);
	if (ret) {
		dev_err(dev, "failed to get firmware name\n");
		return ret;
	}

	platform_set_drvdata(pdev, easrc);
	pm_runtime_enable(dev);

	spin_lock_init(&easrc->lock);

	regcache_cache_only(easrc->regmap, true);

	ret = devm_snd_soc_register_component(dev, &fsl_easrc_component,
					      &fsl_easrc_dai, 1);
	if (ret) {
		dev_err(dev, "failed to register ASoC DAI\n");
		return ret;
	}

	ret = devm_snd_soc_register_component(dev, &fsl_asrc_component,
					      NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "failed to register ASoC platform\n");
		return ret;
	}

	return 0;
}

static int fsl_easrc_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static __maybe_unused int fsl_easrc_runtime_suspend(struct device *dev)
{
	struct fsl_asrc *easrc = dev_get_drvdata(dev);
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	unsigned long lock_flags;

	regcache_cache_only(easrc->regmap, true);

	clk_disable_unprepare(easrc->mem_clk);

	spin_lock_irqsave(&easrc->lock, lock_flags);
	easrc_priv->firmware_loaded = 0;
	spin_unlock_irqrestore(&easrc->lock, lock_flags);

	return 0;
}

static __maybe_unused int fsl_easrc_runtime_resume(struct device *dev)
{
	struct fsl_asrc *easrc = dev_get_drvdata(dev);
	struct fsl_easrc_priv *easrc_priv = easrc->private;
	struct fsl_easrc_ctx_priv *ctx_priv;
	struct fsl_asrc_pair *ctx;
	unsigned long lock_flags;
	int ret;
	int i;

	ret = clk_prepare_enable(easrc->mem_clk);
	if (ret)
		return ret;

	regcache_cache_only(easrc->regmap, false);
	regcache_mark_dirty(easrc->regmap);
	regcache_sync(easrc->regmap);

	spin_lock_irqsave(&easrc->lock, lock_flags);
	if (easrc_priv->firmware_loaded) {
		spin_unlock_irqrestore(&easrc->lock, lock_flags);
		goto skip_load;
	}
	easrc_priv->firmware_loaded = 1;
	spin_unlock_irqrestore(&easrc->lock, lock_flags);

	ret = fsl_easrc_get_firmware(easrc);
	if (ret) {
		dev_err(dev, "failed to get firmware\n");
		goto disable_mem_clk;
	}

	/*
	 * Write Resampling Coefficients
	 * The coefficient RAM must be configured prior to beginning of
	 * any context processing within the ASRC
	 */
	ret = fsl_easrc_resampler_config(easrc);
	if (ret) {
		dev_err(dev, "resampler config failed\n");
		goto disable_mem_clk;
	}

	for (i = ASRC_PAIR_A; i < EASRC_CTX_MAX_NUM; i++) {
		ctx = easrc->pair[i];
		if (!ctx)
			continue;

		ctx_priv = ctx->private;
		fsl_easrc_set_rs_ratio(ctx);
		ctx_priv->out_missed_sample = ctx_priv->in_filled_sample *
					      ctx_priv->out_params.sample_rate /
					      ctx_priv->in_params.sample_rate;
		if (ctx_priv->in_filled_sample * ctx_priv->out_params.sample_rate
		    % ctx_priv->in_params.sample_rate != 0)
			ctx_priv->out_missed_sample += 1;

		ret = fsl_easrc_write_pf_coeff_mem(easrc, i,
						   ctx_priv->st1_coeff,
						   ctx_priv->st1_num_taps,
						   ctx_priv->st1_addexp);
		if (ret)
			goto disable_mem_clk;

		ret = fsl_easrc_write_pf_coeff_mem(easrc, i,
						   ctx_priv->st2_coeff,
						   ctx_priv->st2_num_taps,
						   ctx_priv->st2_addexp);
		if (ret)
			goto disable_mem_clk;
	}

skip_load:
	return 0;

disable_mem_clk:
	clk_disable_unprepare(easrc->mem_clk);
	return ret;
}

static const struct dev_pm_ops fsl_easrc_pm_ops = {
	SET_RUNTIME_PM_OPS(fsl_easrc_runtime_suspend,
			   fsl_easrc_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver fsl_easrc_driver = {
	.probe = fsl_easrc_probe,
	.remove = fsl_easrc_remove,
	.driver = {
		.name = "fsl-easrc",
		.pm = &fsl_easrc_pm_ops,
		.of_match_table = fsl_easrc_dt_ids,
	},
};
module_platform_driver(fsl_easrc_driver);

MODULE_DESCRIPTION("NXP Enhanced Asynchronous Sample Rate (eASRC) driver");
MODULE_LICENSE("GPL v2");
