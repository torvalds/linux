// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "i9xx_wm.h"
#include "intel_atomic.h"
#include "intel_display.h"
#include "intel_display_trace.h"
#include "intel_mchbar_regs.h"
#include "intel_wm.h"
#include "skl_watermark.h"
#include "vlv_sideband.h"

/* used in computing the new watermarks state */
struct intel_wm_config {
	unsigned int num_pipes_active;
	bool sprites_enabled;
	bool sprites_scaled;
};

struct cxsr_latency {
	bool is_desktop : 1;
	bool is_ddr3 : 1;
	u16 fsb_freq;
	u16 mem_freq;
	u16 display_sr;
	u16 display_hpll_disable;
	u16 cursor_sr;
	u16 cursor_hpll_disable;
};

static const struct cxsr_latency cxsr_latency_table[] = {
	{1, 0, 800, 400, 3382, 33382, 3983, 33983},    /* DDR2-400 SC */
	{1, 0, 800, 667, 3354, 33354, 3807, 33807},    /* DDR2-667 SC */
	{1, 0, 800, 800, 3347, 33347, 3763, 33763},    /* DDR2-800 SC */
	{1, 1, 800, 667, 6420, 36420, 6873, 36873},    /* DDR3-667 SC */
	{1, 1, 800, 800, 5902, 35902, 6318, 36318},    /* DDR3-800 SC */

	{1, 0, 667, 400, 3400, 33400, 4021, 34021},    /* DDR2-400 SC */
	{1, 0, 667, 667, 3372, 33372, 3845, 33845},    /* DDR2-667 SC */
	{1, 0, 667, 800, 3386, 33386, 3822, 33822},    /* DDR2-800 SC */
	{1, 1, 667, 667, 6438, 36438, 6911, 36911},    /* DDR3-667 SC */
	{1, 1, 667, 800, 5941, 35941, 6377, 36377},    /* DDR3-800 SC */

	{1, 0, 400, 400, 3472, 33472, 4173, 34173},    /* DDR2-400 SC */
	{1, 0, 400, 667, 3443, 33443, 3996, 33996},    /* DDR2-667 SC */
	{1, 0, 400, 800, 3430, 33430, 3946, 33946},    /* DDR2-800 SC */
	{1, 1, 400, 667, 6509, 36509, 7062, 37062},    /* DDR3-667 SC */
	{1, 1, 400, 800, 5985, 35985, 6501, 36501},    /* DDR3-800 SC */

	{0, 0, 800, 400, 3438, 33438, 4065, 34065},    /* DDR2-400 SC */
	{0, 0, 800, 667, 3410, 33410, 3889, 33889},    /* DDR2-667 SC */
	{0, 0, 800, 800, 3403, 33403, 3845, 33845},    /* DDR2-800 SC */
	{0, 1, 800, 667, 6476, 36476, 6955, 36955},    /* DDR3-667 SC */
	{0, 1, 800, 800, 5958, 35958, 6400, 36400},    /* DDR3-800 SC */

	{0, 0, 667, 400, 3456, 33456, 4103, 34106},    /* DDR2-400 SC */
	{0, 0, 667, 667, 3428, 33428, 3927, 33927},    /* DDR2-667 SC */
	{0, 0, 667, 800, 3443, 33443, 3905, 33905},    /* DDR2-800 SC */
	{0, 1, 667, 667, 6494, 36494, 6993, 36993},    /* DDR3-667 SC */
	{0, 1, 667, 800, 5998, 35998, 6460, 36460},    /* DDR3-800 SC */

	{0, 0, 400, 400, 3528, 33528, 4255, 34255},    /* DDR2-400 SC */
	{0, 0, 400, 667, 3500, 33500, 4079, 34079},    /* DDR2-667 SC */
	{0, 0, 400, 800, 3487, 33487, 4029, 34029},    /* DDR2-800 SC */
	{0, 1, 400, 667, 6566, 36566, 7145, 37145},    /* DDR3-667 SC */
	{0, 1, 400, 800, 6042, 36042, 6584, 36584},    /* DDR3-800 SC */
};

static const struct cxsr_latency *pnv_get_cxsr_latency(struct drm_i915_private *i915)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cxsr_latency_table); i++) {
		const struct cxsr_latency *latency = &cxsr_latency_table[i];
		bool is_desktop = !IS_MOBILE(i915);

		if (is_desktop == latency->is_desktop &&
		    i915->is_ddr3 == latency->is_ddr3 &&
		    DIV_ROUND_CLOSEST(i915->fsb_freq, 1000) == latency->fsb_freq &&
		    DIV_ROUND_CLOSEST(i915->mem_freq, 1000) == latency->mem_freq)
			return latency;
	}

	drm_dbg_kms(&i915->drm,
		    "Could not find CxSR latency for DDR%s, FSB %u kHz, MEM %u kHz\n",
		    i915->is_ddr3 ? "3" : "2", i915->fsb_freq, i915->mem_freq);

	return NULL;
}

static void chv_set_memory_dvfs(struct drm_i915_private *dev_priv, bool enable)
{
	u32 val;

	vlv_punit_get(dev_priv);

	val = vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2);
	if (enable)
		val &= ~FORCE_DDR_HIGH_FREQ;
	else
		val |= FORCE_DDR_HIGH_FREQ;
	val &= ~FORCE_DDR_LOW_FREQ;
	val |= FORCE_DDR_FREQ_REQ_ACK;
	vlv_punit_write(dev_priv, PUNIT_REG_DDR_SETUP2, val);

	if (wait_for((vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2) &
		      FORCE_DDR_FREQ_REQ_ACK) == 0, 3))
		drm_err(&dev_priv->drm,
			"timed out waiting for Punit DDR DVFS request\n");

	vlv_punit_put(dev_priv);
}

static void chv_set_memory_pm5(struct drm_i915_private *dev_priv, bool enable)
{
	u32 val;

	vlv_punit_get(dev_priv);

	val = vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM);
	if (enable)
		val |= DSP_MAXFIFO_PM5_ENABLE;
	else
		val &= ~DSP_MAXFIFO_PM5_ENABLE;
	vlv_punit_write(dev_priv, PUNIT_REG_DSPSSPM, val);

	vlv_punit_put(dev_priv);
}

#define FW_WM(value, plane) \
	(((value) << DSPFW_ ## plane ## _SHIFT) & DSPFW_ ## plane ## _MASK)

static bool _intel_set_memory_cxsr(struct drm_i915_private *dev_priv, bool enable)
{
	bool was_enabled;
	u32 val;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		was_enabled = intel_uncore_read(&dev_priv->uncore, FW_BLC_SELF_VLV) & FW_CSPWRDWNEN;
		intel_uncore_write(&dev_priv->uncore, FW_BLC_SELF_VLV, enable ? FW_CSPWRDWNEN : 0);
		intel_uncore_posting_read(&dev_priv->uncore, FW_BLC_SELF_VLV);
	} else if (IS_G4X(dev_priv) || IS_I965GM(dev_priv)) {
		was_enabled = intel_uncore_read(&dev_priv->uncore, FW_BLC_SELF) & FW_BLC_SELF_EN;
		intel_uncore_write(&dev_priv->uncore, FW_BLC_SELF, enable ? FW_BLC_SELF_EN : 0);
		intel_uncore_posting_read(&dev_priv->uncore, FW_BLC_SELF);
	} else if (IS_PINEVIEW(dev_priv)) {
		val = intel_uncore_read(&dev_priv->uncore, DSPFW3(dev_priv));
		was_enabled = val & PINEVIEW_SELF_REFRESH_EN;
		if (enable)
			val |= PINEVIEW_SELF_REFRESH_EN;
		else
			val &= ~PINEVIEW_SELF_REFRESH_EN;
		intel_uncore_write(&dev_priv->uncore, DSPFW3(dev_priv), val);
		intel_uncore_posting_read(&dev_priv->uncore, DSPFW3(dev_priv));
	} else if (IS_I945G(dev_priv) || IS_I945GM(dev_priv)) {
		was_enabled = intel_uncore_read(&dev_priv->uncore, FW_BLC_SELF) & FW_BLC_SELF_EN;
		val = enable ? _MASKED_BIT_ENABLE(FW_BLC_SELF_EN) :
			       _MASKED_BIT_DISABLE(FW_BLC_SELF_EN);
		intel_uncore_write(&dev_priv->uncore, FW_BLC_SELF, val);
		intel_uncore_posting_read(&dev_priv->uncore, FW_BLC_SELF);
	} else if (IS_I915GM(dev_priv)) {
		/*
		 * FIXME can't find a bit like this for 915G, and
		 * yet it does have the related watermark in
		 * FW_BLC_SELF. What's going on?
		 */
		was_enabled = intel_uncore_read(&dev_priv->uncore, INSTPM) & INSTPM_SELF_EN;
		val = enable ? _MASKED_BIT_ENABLE(INSTPM_SELF_EN) :
			       _MASKED_BIT_DISABLE(INSTPM_SELF_EN);
		intel_uncore_write(&dev_priv->uncore, INSTPM, val);
		intel_uncore_posting_read(&dev_priv->uncore, INSTPM);
	} else {
		return false;
	}

	trace_intel_memory_cxsr(dev_priv, was_enabled, enable);

	drm_dbg_kms(&dev_priv->drm, "memory self-refresh is %s (was %s)\n",
		    str_enabled_disabled(enable),
		    str_enabled_disabled(was_enabled));

	return was_enabled;
}

/**
 * intel_set_memory_cxsr - Configure CxSR state
 * @dev_priv: i915 device
 * @enable: Allow vs. disallow CxSR
 *
 * Allow or disallow the system to enter a special CxSR
 * (C-state self refresh) state. What typically happens in CxSR mode
 * is that several display FIFOs may get combined into a single larger
 * FIFO for a particular plane (so called max FIFO mode) to allow the
 * system to defer memory fetches longer, and the memory will enter
 * self refresh.
 *
 * Note that enabling CxSR does not guarantee that the system enter
 * this special mode, nor does it guarantee that the system stays
 * in that mode once entered. So this just allows/disallows the system
 * to autonomously utilize the CxSR mode. Other factors such as core
 * C-states will affect when/if the system actually enters/exits the
 * CxSR mode.
 *
 * Note that on VLV/CHV this actually only controls the max FIFO mode,
 * and the system is free to enter/exit memory self refresh at any time
 * even when the use of CxSR has been disallowed.
 *
 * While the system is actually in the CxSR/max FIFO mode, some plane
 * control registers will not get latched on vblank. Thus in order to
 * guarantee the system will respond to changes in the plane registers
 * we must always disallow CxSR prior to making changes to those registers.
 * Unfortunately the system will re-evaluate the CxSR conditions at
 * frame start which happens after vblank start (which is when the plane
 * registers would get latched), so we can't proceed with the plane update
 * during the same frame where we disallowed CxSR.
 *
 * Certain platforms also have a deeper HPLL SR mode. Fortunately the
 * HPLL SR mode depends on CxSR itself, so we don't have to hand hold
 * the hardware w.r.t. HPLL SR when writing to plane registers.
 * Disallowing just CxSR is sufficient.
 */
bool intel_set_memory_cxsr(struct drm_i915_private *dev_priv, bool enable)
{
	bool ret;

	mutex_lock(&dev_priv->display.wm.wm_mutex);
	ret = _intel_set_memory_cxsr(dev_priv, enable);
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		dev_priv->display.wm.vlv.cxsr = enable;
	else if (IS_G4X(dev_priv))
		dev_priv->display.wm.g4x.cxsr = enable;
	mutex_unlock(&dev_priv->display.wm.wm_mutex);

	return ret;
}

/*
 * Latency for FIFO fetches is dependent on several factors:
 *   - memory configuration (speed, channels)
 *   - chipset
 *   - current MCH state
 * It can be fairly high in some situations, so here we assume a fairly
 * pessimal value.  It's a tradeoff between extra memory fetches (if we
 * set this value too high, the FIFO will fetch frequently to stay full)
 * and power consumption (set it too low to save power and we might see
 * FIFO underruns and display "flicker").
 *
 * A value of 5us seems to be a good balance; safe for very low end
 * platforms but not overly aggressive on lower latency configs.
 */
static const int pessimal_latency_ns = 5000;

#define VLV_FIFO_START(dsparb, dsparb2, lo_shift, hi_shift) \
	((((dsparb) >> (lo_shift)) & 0xff) | ((((dsparb2) >> (hi_shift)) & 0x1) << 8))

static void vlv_get_fifo_size(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct vlv_fifo_state *fifo_state = &crtc_state->wm.vlv.fifo_state;
	enum pipe pipe = crtc->pipe;
	int sprite0_start, sprite1_start;
	u32 dsparb, dsparb2, dsparb3;

	switch (pipe) {
	case PIPE_A:
		dsparb = intel_uncore_read(&dev_priv->uncore,
					   DSPARB(dev_priv));
		dsparb2 = intel_uncore_read(&dev_priv->uncore, DSPARB2);
		sprite0_start = VLV_FIFO_START(dsparb, dsparb2, 0, 0);
		sprite1_start = VLV_FIFO_START(dsparb, dsparb2, 8, 4);
		break;
	case PIPE_B:
		dsparb = intel_uncore_read(&dev_priv->uncore,
					   DSPARB(dev_priv));
		dsparb2 = intel_uncore_read(&dev_priv->uncore, DSPARB2);
		sprite0_start = VLV_FIFO_START(dsparb, dsparb2, 16, 8);
		sprite1_start = VLV_FIFO_START(dsparb, dsparb2, 24, 12);
		break;
	case PIPE_C:
		dsparb2 = intel_uncore_read(&dev_priv->uncore, DSPARB2);
		dsparb3 = intel_uncore_read(&dev_priv->uncore, DSPARB3);
		sprite0_start = VLV_FIFO_START(dsparb3, dsparb2, 0, 16);
		sprite1_start = VLV_FIFO_START(dsparb3, dsparb2, 8, 20);
		break;
	default:
		MISSING_CASE(pipe);
		return;
	}

	fifo_state->plane[PLANE_PRIMARY] = sprite0_start;
	fifo_state->plane[PLANE_SPRITE0] = sprite1_start - sprite0_start;
	fifo_state->plane[PLANE_SPRITE1] = 511 - sprite1_start;
	fifo_state->plane[PLANE_CURSOR] = 63;
}

static int i9xx_get_fifo_size(struct drm_i915_private *dev_priv,
			      enum i9xx_plane_id i9xx_plane)
{
	u32 dsparb = intel_uncore_read(&dev_priv->uncore, DSPARB(dev_priv));
	int size;

	size = dsparb & 0x7f;
	if (i9xx_plane == PLANE_B)
		size = ((dsparb >> DSPARB_CSTART_SHIFT) & 0x7f) - size;

	drm_dbg_kms(&dev_priv->drm, "FIFO size - (0x%08x) %c: %d\n",
		    dsparb, plane_name(i9xx_plane), size);

	return size;
}

static int i830_get_fifo_size(struct drm_i915_private *dev_priv,
			      enum i9xx_plane_id i9xx_plane)
{
	u32 dsparb = intel_uncore_read(&dev_priv->uncore, DSPARB(dev_priv));
	int size;

	size = dsparb & 0x1ff;
	if (i9xx_plane == PLANE_B)
		size = ((dsparb >> DSPARB_BEND_SHIFT) & 0x1ff) - size;
	size >>= 1; /* Convert to cachelines */

	drm_dbg_kms(&dev_priv->drm, "FIFO size - (0x%08x) %c: %d\n",
		    dsparb, plane_name(i9xx_plane), size);

	return size;
}

static int i845_get_fifo_size(struct drm_i915_private *dev_priv,
			      enum i9xx_plane_id i9xx_plane)
{
	u32 dsparb = intel_uncore_read(&dev_priv->uncore, DSPARB(dev_priv));
	int size;

	size = dsparb & 0x7f;
	size >>= 2; /* Convert to cachelines */

	drm_dbg_kms(&dev_priv->drm, "FIFO size - (0x%08x) %c: %d\n",
		    dsparb, plane_name(i9xx_plane), size);

	return size;
}

/* Pineview has different values for various configs */
static const struct intel_watermark_params pnv_display_wm = {
	.fifo_size = PINEVIEW_DISPLAY_FIFO,
	.max_wm = PINEVIEW_MAX_WM,
	.default_wm = PINEVIEW_DFT_WM,
	.guard_size = PINEVIEW_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};

static const struct intel_watermark_params pnv_display_hplloff_wm = {
	.fifo_size = PINEVIEW_DISPLAY_FIFO,
	.max_wm = PINEVIEW_MAX_WM,
	.default_wm = PINEVIEW_DFT_HPLLOFF_WM,
	.guard_size = PINEVIEW_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};

static const struct intel_watermark_params pnv_cursor_wm = {
	.fifo_size = PINEVIEW_CURSOR_FIFO,
	.max_wm = PINEVIEW_CURSOR_MAX_WM,
	.default_wm = PINEVIEW_CURSOR_DFT_WM,
	.guard_size = PINEVIEW_CURSOR_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};

static const struct intel_watermark_params pnv_cursor_hplloff_wm = {
	.fifo_size = PINEVIEW_CURSOR_FIFO,
	.max_wm = PINEVIEW_CURSOR_MAX_WM,
	.default_wm = PINEVIEW_CURSOR_DFT_WM,
	.guard_size = PINEVIEW_CURSOR_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};

static const struct intel_watermark_params i965_cursor_wm_info = {
	.fifo_size = I965_CURSOR_FIFO,
	.max_wm = I965_CURSOR_MAX_WM,
	.default_wm = I965_CURSOR_DFT_WM,
	.guard_size = 2,
	.cacheline_size = I915_FIFO_LINE_SIZE,
};

static const struct intel_watermark_params i945_wm_info = {
	.fifo_size = I945_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I915_FIFO_LINE_SIZE,
};

static const struct intel_watermark_params i915_wm_info = {
	.fifo_size = I915_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I915_FIFO_LINE_SIZE,
};

static const struct intel_watermark_params i830_a_wm_info = {
	.fifo_size = I855GM_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I830_FIFO_LINE_SIZE,
};

static const struct intel_watermark_params i830_bc_wm_info = {
	.fifo_size = I855GM_FIFO_SIZE,
	.max_wm = I915_MAX_WM / 2,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I830_FIFO_LINE_SIZE,
};

static const struct intel_watermark_params i845_wm_info = {
	.fifo_size = I830_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I830_FIFO_LINE_SIZE,
};

/**
 * intel_wm_method1 - Method 1 / "small buffer" watermark formula
 * @pixel_rate: Pipe pixel rate in kHz
 * @cpp: Plane bytes per pixel
 * @latency: Memory wakeup latency in 0.1us units
 *
 * Compute the watermark using the method 1 or "small buffer"
 * formula. The caller may additonally add extra cachelines
 * to account for TLB misses and clock crossings.
 *
 * This method is concerned with the short term drain rate
 * of the FIFO, ie. it does not account for blanking periods
 * which would effectively reduce the average drain rate across
 * a longer period. The name "small" refers to the fact the
 * FIFO is relatively small compared to the amount of data
 * fetched.
 *
 * The FIFO level vs. time graph might look something like:
 *
 *   |\   |\
 *   | \  | \
 * __---__---__ (- plane active, _ blanking)
 * -> time
 *
 * or perhaps like this:
 *
 *   |\|\  |\|\
 * __----__----__ (- plane active, _ blanking)
 * -> time
 *
 * Returns:
 * The watermark in bytes
 */
static unsigned int intel_wm_method1(unsigned int pixel_rate,
				     unsigned int cpp,
				     unsigned int latency)
{
	u64 ret;

	ret = mul_u32_u32(pixel_rate, cpp * latency);
	ret = DIV_ROUND_UP_ULL(ret, 10000);

	return ret;
}

/**
 * intel_wm_method2 - Method 2 / "large buffer" watermark formula
 * @pixel_rate: Pipe pixel rate in kHz
 * @htotal: Pipe horizontal total
 * @width: Plane width in pixels
 * @cpp: Plane bytes per pixel
 * @latency: Memory wakeup latency in 0.1us units
 *
 * Compute the watermark using the method 2 or "large buffer"
 * formula. The caller may additonally add extra cachelines
 * to account for TLB misses and clock crossings.
 *
 * This method is concerned with the long term drain rate
 * of the FIFO, ie. it does account for blanking periods
 * which effectively reduce the average drain rate across
 * a longer period. The name "large" refers to the fact the
 * FIFO is relatively large compared to the amount of data
 * fetched.
 *
 * The FIFO level vs. time graph might look something like:
 *
 *    |\___       |\___
 *    |    \___   |    \___
 *    |        \  |        \
 * __ --__--__--__--__--__--__ (- plane active, _ blanking)
 * -> time
 *
 * Returns:
 * The watermark in bytes
 */
static unsigned int intel_wm_method2(unsigned int pixel_rate,
				     unsigned int htotal,
				     unsigned int width,
				     unsigned int cpp,
				     unsigned int latency)
{
	unsigned int ret;

	/*
	 * FIXME remove once all users are computing
	 * watermarks in the correct place.
	 */
	if (WARN_ON_ONCE(htotal == 0))
		htotal = 1;

	ret = (latency * pixel_rate) / (htotal * 10000);
	ret = (ret + 1) * width * cpp;

	return ret;
}

/**
 * intel_calculate_wm - calculate watermark level
 * @i915: the device
 * @pixel_rate: pixel clock
 * @wm: chip FIFO params
 * @fifo_size: size of the FIFO buffer
 * @cpp: bytes per pixel
 * @latency_ns: memory latency for the platform
 *
 * Calculate the watermark level (the level at which the display plane will
 * start fetching from memory again).  Each chip has a different display
 * FIFO size and allocation, so the caller needs to figure that out and pass
 * in the correct intel_watermark_params structure.
 *
 * As the pixel clock runs, the FIFO will be drained at a rate that depends
 * on the pixel size.  When it reaches the watermark level, it'll start
 * fetching FIFO line sized based chunks from memory until the FIFO fills
 * past the watermark point.  If the FIFO drains completely, a FIFO underrun
 * will occur, and a display engine hang could result.
 */
static unsigned int intel_calculate_wm(struct drm_i915_private *i915,
				       int pixel_rate,
				       const struct intel_watermark_params *wm,
				       int fifo_size, int cpp,
				       unsigned int latency_ns)
{
	int entries, wm_size;

	/*
	 * Note: we need to make sure we don't overflow for various clock &
	 * latency values.
	 * clocks go from a few thousand to several hundred thousand.
	 * latency is usually a few thousand
	 */
	entries = intel_wm_method1(pixel_rate, cpp,
				   latency_ns / 100);
	entries = DIV_ROUND_UP(entries, wm->cacheline_size) +
		wm->guard_size;
	drm_dbg_kms(&i915->drm, "FIFO entries required for mode: %d\n", entries);

	wm_size = fifo_size - entries;
	drm_dbg_kms(&i915->drm, "FIFO watermark level: %d\n", wm_size);

	/* Don't promote wm_size to unsigned... */
	if (wm_size > wm->max_wm)
		wm_size = wm->max_wm;
	if (wm_size <= 0)
		wm_size = wm->default_wm;

	/*
	 * Bspec seems to indicate that the value shouldn't be lower than
	 * 'burst size + 1'. Certainly 830 is quite unhappy with low values.
	 * Lets go for 8 which is the burst size since certain platforms
	 * already use a hardcoded 8 (which is what the spec says should be
	 * done).
	 */
	if (wm_size <= 8)
		wm_size = 8;

	return wm_size;
}

static bool is_disabling(int old, int new, int threshold)
{
	return old >= threshold && new < threshold;
}

static bool is_enabling(int old, int new, int threshold)
{
	return old < threshold && new >= threshold;
}

static bool intel_crtc_active(struct intel_crtc *crtc)
{
	/* Be paranoid as we can arrive here with only partial
	 * state retrieved from the hardware during setup.
	 *
	 * We can ditch the adjusted_mode.crtc_clock check as soon
	 * as Haswell has gained clock readout/fastboot support.
	 *
	 * We can ditch the crtc->primary->state->fb check as soon as we can
	 * properly reconstruct framebuffers.
	 *
	 * FIXME: The intel_crtc->active here should be switched to
	 * crtc->state->active once we have proper CRTC states wired up
	 * for atomic.
	 */
	return crtc->active && crtc->base.primary->state->fb &&
		crtc->config->hw.adjusted_mode.crtc_clock;
}

static struct intel_crtc *single_enabled_crtc(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc, *enabled = NULL;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		if (intel_crtc_active(crtc)) {
			if (enabled)
				return NULL;
			enabled = crtc;
		}
	}

	return enabled;
}

static void pnv_update_wm(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;
	const struct cxsr_latency *latency;
	u32 reg;
	unsigned int wm;

	latency = pnv_get_cxsr_latency(dev_priv);
	if (!latency) {
		drm_dbg_kms(&dev_priv->drm, "Unknown FSB/MEM, disabling CxSR\n");
		intel_set_memory_cxsr(dev_priv, false);
		return;
	}

	crtc = single_enabled_crtc(dev_priv);
	if (crtc) {
		const struct drm_framebuffer *fb =
			crtc->base.primary->state->fb;
		int pixel_rate = crtc->config->pixel_rate;
		int cpp = fb->format->cpp[0];

		/* Display SR */
		wm = intel_calculate_wm(dev_priv, pixel_rate,
					&pnv_display_wm,
					pnv_display_wm.fifo_size,
					cpp, latency->display_sr);
		reg = intel_uncore_read(&dev_priv->uncore, DSPFW1(dev_priv));
		reg &= ~DSPFW_SR_MASK;
		reg |= FW_WM(wm, SR);
		intel_uncore_write(&dev_priv->uncore, DSPFW1(dev_priv), reg);
		drm_dbg_kms(&dev_priv->drm, "DSPFW1 register is %x\n", reg);

		/* cursor SR */
		wm = intel_calculate_wm(dev_priv, pixel_rate,
					&pnv_cursor_wm,
					pnv_display_wm.fifo_size,
					4, latency->cursor_sr);
		intel_uncore_rmw(&dev_priv->uncore, DSPFW3(dev_priv),
				 DSPFW_CURSOR_SR_MASK,
				 FW_WM(wm, CURSOR_SR));

		/* Display HPLL off SR */
		wm = intel_calculate_wm(dev_priv, pixel_rate,
					&pnv_display_hplloff_wm,
					pnv_display_hplloff_wm.fifo_size,
					cpp, latency->display_hpll_disable);
		intel_uncore_rmw(&dev_priv->uncore, DSPFW3(dev_priv),
				 DSPFW_HPLL_SR_MASK, FW_WM(wm, HPLL_SR));

		/* cursor HPLL off SR */
		wm = intel_calculate_wm(dev_priv, pixel_rate,
					&pnv_cursor_hplloff_wm,
					pnv_display_hplloff_wm.fifo_size,
					4, latency->cursor_hpll_disable);
		reg = intel_uncore_read(&dev_priv->uncore, DSPFW3(dev_priv));
		reg &= ~DSPFW_HPLL_CURSOR_MASK;
		reg |= FW_WM(wm, HPLL_CURSOR);
		intel_uncore_write(&dev_priv->uncore, DSPFW3(dev_priv), reg);
		drm_dbg_kms(&dev_priv->drm, "DSPFW3 register is %x\n", reg);

		intel_set_memory_cxsr(dev_priv, true);
	} else {
		intel_set_memory_cxsr(dev_priv, false);
	}
}

/*
 * Documentation says:
 * "If the line size is small, the TLB fetches can get in the way of the
 *  data fetches, causing some lag in the pixel data return which is not
 *  accounted for in the above formulas. The following adjustment only
 *  needs to be applied if eight whole lines fit in the buffer at once.
 *  The WM is adjusted upwards by the difference between the FIFO size
 *  and the size of 8 whole lines. This adjustment is always performed
 *  in the actual pixel depth regardless of whether FBC is enabled or not."
 */
static unsigned int g4x_tlb_miss_wa(int fifo_size, int width, int cpp)
{
	int tlb_miss = fifo_size * 64 - width * cpp * 8;

	return max(0, tlb_miss);
}

static void g4x_write_wm_values(struct drm_i915_private *dev_priv,
				const struct g4x_wm_values *wm)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		trace_g4x_wm(intel_crtc_for_pipe(dev_priv, pipe), wm);

	intel_uncore_write(&dev_priv->uncore, DSPFW1(dev_priv),
			   FW_WM(wm->sr.plane, SR) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_CURSOR], CURSORB) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_PRIMARY], PLANEB) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_PRIMARY], PLANEA));
	intel_uncore_write(&dev_priv->uncore, DSPFW2(dev_priv),
			   (wm->fbc_en ? DSPFW_FBC_SR_EN : 0) |
			   FW_WM(wm->sr.fbc, FBC_SR) |
			   FW_WM(wm->hpll.fbc, FBC_HPLL_SR) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE0], SPRITEB) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_CURSOR], CURSORA) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE0], SPRITEA));
	intel_uncore_write(&dev_priv->uncore, DSPFW3(dev_priv),
			   (wm->hpll_en ? DSPFW_HPLL_SR_EN : 0) |
			   FW_WM(wm->sr.cursor, CURSOR_SR) |
			   FW_WM(wm->hpll.cursor, HPLL_CURSOR) |
			   FW_WM(wm->hpll.plane, HPLL_SR));

	intel_uncore_posting_read(&dev_priv->uncore, DSPFW1(dev_priv));
}

#define FW_WM_VLV(value, plane) \
	(((value) << DSPFW_ ## plane ## _SHIFT) & DSPFW_ ## plane ## _MASK_VLV)

static void vlv_write_wm_values(struct drm_i915_private *dev_priv,
				const struct vlv_wm_values *wm)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		trace_vlv_wm(intel_crtc_for_pipe(dev_priv, pipe), wm);

		intel_uncore_write(&dev_priv->uncore, VLV_DDL(pipe),
				   (wm->ddl[pipe].plane[PLANE_CURSOR] << DDL_CURSOR_SHIFT) |
				   (wm->ddl[pipe].plane[PLANE_SPRITE1] << DDL_SPRITE_SHIFT(1)) |
				   (wm->ddl[pipe].plane[PLANE_SPRITE0] << DDL_SPRITE_SHIFT(0)) |
				   (wm->ddl[pipe].plane[PLANE_PRIMARY] << DDL_PLANE_SHIFT));
	}

	/*
	 * Zero the (unused) WM1 watermarks, and also clear all the
	 * high order bits so that there are no out of bounds values
	 * present in the registers during the reprogramming.
	 */
	intel_uncore_write(&dev_priv->uncore, DSPHOWM, 0);
	intel_uncore_write(&dev_priv->uncore, DSPHOWM1, 0);
	intel_uncore_write(&dev_priv->uncore, DSPFW4, 0);
	intel_uncore_write(&dev_priv->uncore, DSPFW5, 0);
	intel_uncore_write(&dev_priv->uncore, DSPFW6, 0);

	intel_uncore_write(&dev_priv->uncore, DSPFW1(dev_priv),
			   FW_WM(wm->sr.plane, SR) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_CURSOR], CURSORB) |
			   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_PRIMARY], PLANEB) |
			   FW_WM_VLV(wm->pipe[PIPE_A].plane[PLANE_PRIMARY], PLANEA));
	intel_uncore_write(&dev_priv->uncore, DSPFW2(dev_priv),
			   FW_WM_VLV(wm->pipe[PIPE_A].plane[PLANE_SPRITE1], SPRITEB) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_CURSOR], CURSORA) |
			   FW_WM_VLV(wm->pipe[PIPE_A].plane[PLANE_SPRITE0], SPRITEA));
	intel_uncore_write(&dev_priv->uncore, DSPFW3(dev_priv),
			   FW_WM(wm->sr.cursor, CURSOR_SR));

	if (IS_CHERRYVIEW(dev_priv)) {
		intel_uncore_write(&dev_priv->uncore, DSPFW7_CHV,
				   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_SPRITE1], SPRITED) |
				   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_SPRITE0], SPRITEC));
		intel_uncore_write(&dev_priv->uncore, DSPFW8_CHV,
				   FW_WM_VLV(wm->pipe[PIPE_C].plane[PLANE_SPRITE1], SPRITEF) |
				   FW_WM_VLV(wm->pipe[PIPE_C].plane[PLANE_SPRITE0], SPRITEE));
		intel_uncore_write(&dev_priv->uncore, DSPFW9_CHV,
				   FW_WM_VLV(wm->pipe[PIPE_C].plane[PLANE_PRIMARY], PLANEC) |
				   FW_WM(wm->pipe[PIPE_C].plane[PLANE_CURSOR], CURSORC));
		intel_uncore_write(&dev_priv->uncore, DSPHOWM,
				   FW_WM(wm->sr.plane >> 9, SR_HI) |
				   FW_WM(wm->pipe[PIPE_C].plane[PLANE_SPRITE1] >> 8, SPRITEF_HI) |
				   FW_WM(wm->pipe[PIPE_C].plane[PLANE_SPRITE0] >> 8, SPRITEE_HI) |
				   FW_WM(wm->pipe[PIPE_C].plane[PLANE_PRIMARY] >> 8, PLANEC_HI) |
				   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE1] >> 8, SPRITED_HI) |
				   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE0] >> 8, SPRITEC_HI) |
				   FW_WM(wm->pipe[PIPE_B].plane[PLANE_PRIMARY] >> 8, PLANEB_HI) |
				   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE1] >> 8, SPRITEB_HI) |
				   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE0] >> 8, SPRITEA_HI) |
				   FW_WM(wm->pipe[PIPE_A].plane[PLANE_PRIMARY] >> 8, PLANEA_HI));
	} else {
		intel_uncore_write(&dev_priv->uncore, DSPFW7,
				   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_SPRITE1], SPRITED) |
				   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_SPRITE0], SPRITEC));
		intel_uncore_write(&dev_priv->uncore, DSPHOWM,
				   FW_WM(wm->sr.plane >> 9, SR_HI) |
				   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE1] >> 8, SPRITED_HI) |
				   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE0] >> 8, SPRITEC_HI) |
				   FW_WM(wm->pipe[PIPE_B].plane[PLANE_PRIMARY] >> 8, PLANEB_HI) |
				   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE1] >> 8, SPRITEB_HI) |
				   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE0] >> 8, SPRITEA_HI) |
				   FW_WM(wm->pipe[PIPE_A].plane[PLANE_PRIMARY] >> 8, PLANEA_HI));
	}

	intel_uncore_posting_read(&dev_priv->uncore, DSPFW1(dev_priv));
}

#undef FW_WM_VLV

static void g4x_setup_wm_latency(struct drm_i915_private *dev_priv)
{
	/* all latencies in usec */
	dev_priv->display.wm.pri_latency[G4X_WM_LEVEL_NORMAL] = 5;
	dev_priv->display.wm.pri_latency[G4X_WM_LEVEL_SR] = 12;
	dev_priv->display.wm.pri_latency[G4X_WM_LEVEL_HPLL] = 35;

	dev_priv->display.wm.num_levels = G4X_WM_LEVEL_HPLL + 1;
}

static int g4x_plane_fifo_size(enum plane_id plane_id, int level)
{
	/*
	 * DSPCNTR[13] supposedly controls whether the
	 * primary plane can use the FIFO space otherwise
	 * reserved for the sprite plane. It's not 100% clear
	 * what the actual FIFO size is, but it looks like we
	 * can happily set both primary and sprite watermarks
	 * up to 127 cachelines. So that would seem to mean
	 * that either DSPCNTR[13] doesn't do anything, or that
	 * the total FIFO is >= 256 cachelines in size. Either
	 * way, we don't seem to have to worry about this
	 * repartitioning as the maximum watermark value the
	 * register can hold for each plane is lower than the
	 * minimum FIFO size.
	 */
	switch (plane_id) {
	case PLANE_CURSOR:
		return 63;
	case PLANE_PRIMARY:
		return level == G4X_WM_LEVEL_NORMAL ? 127 : 511;
	case PLANE_SPRITE0:
		return level == G4X_WM_LEVEL_NORMAL ? 127 : 0;
	default:
		MISSING_CASE(plane_id);
		return 0;
	}
}

static int g4x_fbc_fifo_size(int level)
{
	switch (level) {
	case G4X_WM_LEVEL_SR:
		return 7;
	case G4X_WM_LEVEL_HPLL:
		return 15;
	default:
		MISSING_CASE(level);
		return 0;
	}
}

static u16 g4x_compute_wm(const struct intel_crtc_state *crtc_state,
			  const struct intel_plane_state *plane_state,
			  int level)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_display_mode *pipe_mode =
		&crtc_state->hw.pipe_mode;
	unsigned int latency = dev_priv->display.wm.pri_latency[level] * 10;
	unsigned int pixel_rate, htotal, cpp, width, wm;

	if (latency == 0)
		return USHRT_MAX;

	if (!intel_wm_plane_visible(crtc_state, plane_state))
		return 0;

	cpp = plane_state->hw.fb->format->cpp[0];

	/*
	 * WaUse32BppForSRWM:ctg,elk
	 *
	 * The spec fails to list this restriction for the
	 * HPLL watermark, which seems a little strange.
	 * Let's use 32bpp for the HPLL watermark as well.
	 */
	if (plane->id == PLANE_PRIMARY &&
	    level != G4X_WM_LEVEL_NORMAL)
		cpp = max(cpp, 4u);

	pixel_rate = crtc_state->pixel_rate;
	htotal = pipe_mode->crtc_htotal;
	width = drm_rect_width(&plane_state->uapi.src) >> 16;

	if (plane->id == PLANE_CURSOR) {
		wm = intel_wm_method2(pixel_rate, htotal, width, cpp, latency);
	} else if (plane->id == PLANE_PRIMARY &&
		   level == G4X_WM_LEVEL_NORMAL) {
		wm = intel_wm_method1(pixel_rate, cpp, latency);
	} else {
		unsigned int small, large;

		small = intel_wm_method1(pixel_rate, cpp, latency);
		large = intel_wm_method2(pixel_rate, htotal, width, cpp, latency);

		wm = min(small, large);
	}

	wm += g4x_tlb_miss_wa(g4x_plane_fifo_size(plane->id, level),
			      width, cpp);

	wm = DIV_ROUND_UP(wm, 64) + 2;

	return min_t(unsigned int, wm, USHRT_MAX);
}

static bool g4x_raw_plane_wm_set(struct intel_crtc_state *crtc_state,
				 int level, enum plane_id plane_id, u16 value)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	bool dirty = false;

	for (; level < dev_priv->display.wm.num_levels; level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.g4x.raw[level];

		dirty |= raw->plane[plane_id] != value;
		raw->plane[plane_id] = value;
	}

	return dirty;
}

static bool g4x_raw_fbc_wm_set(struct intel_crtc_state *crtc_state,
			       int level, u16 value)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	bool dirty = false;

	/* NORMAL level doesn't have an FBC watermark */
	level = max(level, G4X_WM_LEVEL_SR);

	for (; level < dev_priv->display.wm.num_levels; level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.g4x.raw[level];

		dirty |= raw->fbc != value;
		raw->fbc = value;
	}

	return dirty;
}

static u32 ilk_compute_fbc_wm(const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state,
			      u32 pri_val);

static bool g4x_raw_plane_wm_compute(struct intel_crtc_state *crtc_state,
				     const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	enum plane_id plane_id = plane->id;
	bool dirty = false;
	int level;

	if (!intel_wm_plane_visible(crtc_state, plane_state)) {
		dirty |= g4x_raw_plane_wm_set(crtc_state, 0, plane_id, 0);
		if (plane_id == PLANE_PRIMARY)
			dirty |= g4x_raw_fbc_wm_set(crtc_state, 0, 0);
		goto out;
	}

	for (level = 0; level < dev_priv->display.wm.num_levels; level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.g4x.raw[level];
		int wm, max_wm;

		wm = g4x_compute_wm(crtc_state, plane_state, level);
		max_wm = g4x_plane_fifo_size(plane_id, level);

		if (wm > max_wm)
			break;

		dirty |= raw->plane[plane_id] != wm;
		raw->plane[plane_id] = wm;

		if (plane_id != PLANE_PRIMARY ||
		    level == G4X_WM_LEVEL_NORMAL)
			continue;

		wm = ilk_compute_fbc_wm(crtc_state, plane_state,
					raw->plane[plane_id]);
		max_wm = g4x_fbc_fifo_size(level);

		/*
		 * FBC wm is not mandatory as we
		 * can always just disable its use.
		 */
		if (wm > max_wm)
			wm = USHRT_MAX;

		dirty |= raw->fbc != wm;
		raw->fbc = wm;
	}

	/* mark watermarks as invalid */
	dirty |= g4x_raw_plane_wm_set(crtc_state, level, plane_id, USHRT_MAX);

	if (plane_id == PLANE_PRIMARY)
		dirty |= g4x_raw_fbc_wm_set(crtc_state, level, USHRT_MAX);

 out:
	if (dirty) {
		drm_dbg_kms(&dev_priv->drm,
			    "%s watermarks: normal=%d, SR=%d, HPLL=%d\n",
			    plane->base.name,
			    crtc_state->wm.g4x.raw[G4X_WM_LEVEL_NORMAL].plane[plane_id],
			    crtc_state->wm.g4x.raw[G4X_WM_LEVEL_SR].plane[plane_id],
			    crtc_state->wm.g4x.raw[G4X_WM_LEVEL_HPLL].plane[plane_id]);

		if (plane_id == PLANE_PRIMARY)
			drm_dbg_kms(&dev_priv->drm,
				    "FBC watermarks: SR=%d, HPLL=%d\n",
				    crtc_state->wm.g4x.raw[G4X_WM_LEVEL_SR].fbc,
				    crtc_state->wm.g4x.raw[G4X_WM_LEVEL_HPLL].fbc);
	}

	return dirty;
}

static bool g4x_raw_plane_wm_is_valid(const struct intel_crtc_state *crtc_state,
				      enum plane_id plane_id, int level)
{
	const struct g4x_pipe_wm *raw = &crtc_state->wm.g4x.raw[level];

	return raw->plane[plane_id] <= g4x_plane_fifo_size(plane_id, level);
}

static bool g4x_raw_crtc_wm_is_valid(const struct intel_crtc_state *crtc_state,
				     int level)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);

	if (level >= dev_priv->display.wm.num_levels)
		return false;

	return g4x_raw_plane_wm_is_valid(crtc_state, PLANE_PRIMARY, level) &&
		g4x_raw_plane_wm_is_valid(crtc_state, PLANE_SPRITE0, level) &&
		g4x_raw_plane_wm_is_valid(crtc_state, PLANE_CURSOR, level);
}

/* mark all levels starting from 'level' as invalid */
static void g4x_invalidate_wms(struct intel_crtc *crtc,
			       struct g4x_wm_state *wm_state, int level)
{
	if (level <= G4X_WM_LEVEL_NORMAL) {
		enum plane_id plane_id;

		for_each_plane_id_on_crtc(crtc, plane_id)
			wm_state->wm.plane[plane_id] = USHRT_MAX;
	}

	if (level <= G4X_WM_LEVEL_SR) {
		wm_state->cxsr = false;
		wm_state->sr.cursor = USHRT_MAX;
		wm_state->sr.plane = USHRT_MAX;
		wm_state->sr.fbc = USHRT_MAX;
	}

	if (level <= G4X_WM_LEVEL_HPLL) {
		wm_state->hpll_en = false;
		wm_state->hpll.cursor = USHRT_MAX;
		wm_state->hpll.plane = USHRT_MAX;
		wm_state->hpll.fbc = USHRT_MAX;
	}
}

static bool g4x_compute_fbc_en(const struct g4x_wm_state *wm_state,
			       int level)
{
	if (level < G4X_WM_LEVEL_SR)
		return false;

	if (level >= G4X_WM_LEVEL_SR &&
	    wm_state->sr.fbc > g4x_fbc_fifo_size(G4X_WM_LEVEL_SR))
		return false;

	if (level >= G4X_WM_LEVEL_HPLL &&
	    wm_state->hpll.fbc > g4x_fbc_fifo_size(G4X_WM_LEVEL_HPLL))
		return false;

	return true;
}

static int _g4x_compute_pipe_wm(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct g4x_wm_state *wm_state = &crtc_state->wm.g4x.optimal;
	u8 active_planes = crtc_state->active_planes & ~BIT(PLANE_CURSOR);
	const struct g4x_pipe_wm *raw;
	enum plane_id plane_id;
	int level;

	level = G4X_WM_LEVEL_NORMAL;
	if (!g4x_raw_crtc_wm_is_valid(crtc_state, level))
		goto out;

	raw = &crtc_state->wm.g4x.raw[level];
	for_each_plane_id_on_crtc(crtc, plane_id)
		wm_state->wm.plane[plane_id] = raw->plane[plane_id];

	level = G4X_WM_LEVEL_SR;
	if (!g4x_raw_crtc_wm_is_valid(crtc_state, level))
		goto out;

	raw = &crtc_state->wm.g4x.raw[level];
	wm_state->sr.plane = raw->plane[PLANE_PRIMARY];
	wm_state->sr.cursor = raw->plane[PLANE_CURSOR];
	wm_state->sr.fbc = raw->fbc;

	wm_state->cxsr = active_planes == BIT(PLANE_PRIMARY);

	level = G4X_WM_LEVEL_HPLL;
	if (!g4x_raw_crtc_wm_is_valid(crtc_state, level))
		goto out;

	raw = &crtc_state->wm.g4x.raw[level];
	wm_state->hpll.plane = raw->plane[PLANE_PRIMARY];
	wm_state->hpll.cursor = raw->plane[PLANE_CURSOR];
	wm_state->hpll.fbc = raw->fbc;

	wm_state->hpll_en = wm_state->cxsr;

	level++;

 out:
	if (level == G4X_WM_LEVEL_NORMAL)
		return -EINVAL;

	/* invalidate the higher levels */
	g4x_invalidate_wms(crtc, wm_state, level);

	/*
	 * Determine if the FBC watermark(s) can be used. IF
	 * this isn't the case we prefer to disable the FBC
	 * watermark(s) rather than disable the SR/HPLL
	 * level(s) entirely. 'level-1' is the highest valid
	 * level here.
	 */
	wm_state->fbc_en = g4x_compute_fbc_en(wm_state, level - 1);

	return 0;
}

static int g4x_compute_pipe_wm(struct intel_atomic_state *state,
			       struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_plane_state *old_plane_state;
	const struct intel_plane_state *new_plane_state;
	struct intel_plane *plane;
	unsigned int dirty = 0;
	int i;

	for_each_oldnew_intel_plane_in_state(state, plane,
					     old_plane_state,
					     new_plane_state, i) {
		if (new_plane_state->hw.crtc != &crtc->base &&
		    old_plane_state->hw.crtc != &crtc->base)
			continue;

		if (g4x_raw_plane_wm_compute(crtc_state, new_plane_state))
			dirty |= BIT(plane->id);
	}

	if (!dirty)
		return 0;

	return _g4x_compute_pipe_wm(crtc_state);
}

static int g4x_compute_intermediate_wm(struct intel_atomic_state *state,
				       struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct g4x_wm_state *intermediate = &new_crtc_state->wm.g4x.intermediate;
	const struct g4x_wm_state *optimal = &new_crtc_state->wm.g4x.optimal;
	const struct g4x_wm_state *active = &old_crtc_state->wm.g4x.optimal;
	enum plane_id plane_id;

	if (!new_crtc_state->hw.active ||
	    intel_crtc_needs_modeset(new_crtc_state)) {
		*intermediate = *optimal;

		intermediate->cxsr = false;
		intermediate->hpll_en = false;
		goto out;
	}

	intermediate->cxsr = optimal->cxsr && active->cxsr &&
		!new_crtc_state->disable_cxsr;
	intermediate->hpll_en = optimal->hpll_en && active->hpll_en &&
		!new_crtc_state->disable_cxsr;
	intermediate->fbc_en = optimal->fbc_en && active->fbc_en;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		intermediate->wm.plane[plane_id] =
			max(optimal->wm.plane[plane_id],
			    active->wm.plane[plane_id]);

		drm_WARN_ON(&dev_priv->drm, intermediate->wm.plane[plane_id] >
			    g4x_plane_fifo_size(plane_id, G4X_WM_LEVEL_NORMAL));
	}

	intermediate->sr.plane = max(optimal->sr.plane,
				     active->sr.plane);
	intermediate->sr.cursor = max(optimal->sr.cursor,
				      active->sr.cursor);
	intermediate->sr.fbc = max(optimal->sr.fbc,
				   active->sr.fbc);

	intermediate->hpll.plane = max(optimal->hpll.plane,
				       active->hpll.plane);
	intermediate->hpll.cursor = max(optimal->hpll.cursor,
					active->hpll.cursor);
	intermediate->hpll.fbc = max(optimal->hpll.fbc,
				     active->hpll.fbc);

	drm_WARN_ON(&dev_priv->drm,
		    (intermediate->sr.plane >
		     g4x_plane_fifo_size(PLANE_PRIMARY, G4X_WM_LEVEL_SR) ||
		     intermediate->sr.cursor >
		     g4x_plane_fifo_size(PLANE_CURSOR, G4X_WM_LEVEL_SR)) &&
		    intermediate->cxsr);
	drm_WARN_ON(&dev_priv->drm,
		    (intermediate->sr.plane >
		     g4x_plane_fifo_size(PLANE_PRIMARY, G4X_WM_LEVEL_HPLL) ||
		     intermediate->sr.cursor >
		     g4x_plane_fifo_size(PLANE_CURSOR, G4X_WM_LEVEL_HPLL)) &&
		    intermediate->hpll_en);

	drm_WARN_ON(&dev_priv->drm,
		    intermediate->sr.fbc > g4x_fbc_fifo_size(1) &&
		    intermediate->fbc_en && intermediate->cxsr);
	drm_WARN_ON(&dev_priv->drm,
		    intermediate->hpll.fbc > g4x_fbc_fifo_size(2) &&
		    intermediate->fbc_en && intermediate->hpll_en);

out:
	/*
	 * If our intermediate WM are identical to the final WM, then we can
	 * omit the post-vblank programming; only update if it's different.
	 */
	if (memcmp(intermediate, optimal, sizeof(*intermediate)) != 0)
		new_crtc_state->wm.need_postvbl_update = true;

	return 0;
}

static void g4x_merge_wm(struct drm_i915_private *dev_priv,
			 struct g4x_wm_values *wm)
{
	struct intel_crtc *crtc;
	int num_active_pipes = 0;

	wm->cxsr = true;
	wm->hpll_en = true;
	wm->fbc_en = true;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct g4x_wm_state *wm_state = &crtc->wm.active.g4x;

		if (!crtc->active)
			continue;

		if (!wm_state->cxsr)
			wm->cxsr = false;
		if (!wm_state->hpll_en)
			wm->hpll_en = false;
		if (!wm_state->fbc_en)
			wm->fbc_en = false;

		num_active_pipes++;
	}

	if (num_active_pipes != 1) {
		wm->cxsr = false;
		wm->hpll_en = false;
		wm->fbc_en = false;
	}

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct g4x_wm_state *wm_state = &crtc->wm.active.g4x;
		enum pipe pipe = crtc->pipe;

		wm->pipe[pipe] = wm_state->wm;
		if (crtc->active && wm->cxsr)
			wm->sr = wm_state->sr;
		if (crtc->active && wm->hpll_en)
			wm->hpll = wm_state->hpll;
	}
}

static void g4x_program_watermarks(struct drm_i915_private *dev_priv)
{
	struct g4x_wm_values *old_wm = &dev_priv->display.wm.g4x;
	struct g4x_wm_values new_wm = {};

	g4x_merge_wm(dev_priv, &new_wm);

	if (memcmp(old_wm, &new_wm, sizeof(new_wm)) == 0)
		return;

	if (is_disabling(old_wm->cxsr, new_wm.cxsr, true))
		_intel_set_memory_cxsr(dev_priv, false);

	g4x_write_wm_values(dev_priv, &new_wm);

	if (is_enabling(old_wm->cxsr, new_wm.cxsr, true))
		_intel_set_memory_cxsr(dev_priv, true);

	*old_wm = new_wm;
}

static void g4x_initial_watermarks(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	mutex_lock(&dev_priv->display.wm.wm_mutex);
	crtc->wm.active.g4x = crtc_state->wm.g4x.intermediate;
	g4x_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->display.wm.wm_mutex);
}

static void g4x_optimize_watermarks(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!crtc_state->wm.need_postvbl_update)
		return;

	mutex_lock(&dev_priv->display.wm.wm_mutex);
	crtc->wm.active.g4x = crtc_state->wm.g4x.optimal;
	g4x_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->display.wm.wm_mutex);
}

/* latency must be in 0.1us units. */
static unsigned int vlv_wm_method2(unsigned int pixel_rate,
				   unsigned int htotal,
				   unsigned int width,
				   unsigned int cpp,
				   unsigned int latency)
{
	unsigned int ret;

	ret = intel_wm_method2(pixel_rate, htotal,
			       width, cpp, latency);
	ret = DIV_ROUND_UP(ret, 64);

	return ret;
}

static void vlv_setup_wm_latency(struct drm_i915_private *dev_priv)
{
	/* all latencies in usec */
	dev_priv->display.wm.pri_latency[VLV_WM_LEVEL_PM2] = 3;

	dev_priv->display.wm.num_levels = VLV_WM_LEVEL_PM2 + 1;

	if (IS_CHERRYVIEW(dev_priv)) {
		dev_priv->display.wm.pri_latency[VLV_WM_LEVEL_PM5] = 12;
		dev_priv->display.wm.pri_latency[VLV_WM_LEVEL_DDR_DVFS] = 33;

		dev_priv->display.wm.num_levels = VLV_WM_LEVEL_DDR_DVFS + 1;
	}
}

static u16 vlv_compute_wm_level(const struct intel_crtc_state *crtc_state,
				const struct intel_plane_state *plane_state,
				int level)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_display_mode *pipe_mode =
		&crtc_state->hw.pipe_mode;
	unsigned int pixel_rate, htotal, cpp, width, wm;

	if (dev_priv->display.wm.pri_latency[level] == 0)
		return USHRT_MAX;

	if (!intel_wm_plane_visible(crtc_state, plane_state))
		return 0;

	cpp = plane_state->hw.fb->format->cpp[0];
	pixel_rate = crtc_state->pixel_rate;
	htotal = pipe_mode->crtc_htotal;
	width = drm_rect_width(&plane_state->uapi.src) >> 16;

	if (plane->id == PLANE_CURSOR) {
		/*
		 * FIXME the formula gives values that are
		 * too big for the cursor FIFO, and hence we
		 * would never be able to use cursors. For
		 * now just hardcode the watermark.
		 */
		wm = 63;
	} else {
		wm = vlv_wm_method2(pixel_rate, htotal, width, cpp,
				    dev_priv->display.wm.pri_latency[level] * 10);
	}

	return min_t(unsigned int, wm, USHRT_MAX);
}

static bool vlv_need_sprite0_fifo_workaround(unsigned int active_planes)
{
	return (active_planes & (BIT(PLANE_SPRITE0) |
				 BIT(PLANE_SPRITE1))) == BIT(PLANE_SPRITE1);
}

static int vlv_compute_fifo(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct g4x_pipe_wm *raw =
		&crtc_state->wm.vlv.raw[VLV_WM_LEVEL_PM2];
	struct vlv_fifo_state *fifo_state = &crtc_state->wm.vlv.fifo_state;
	u8 active_planes = crtc_state->active_planes & ~BIT(PLANE_CURSOR);
	int num_active_planes = hweight8(active_planes);
	const int fifo_size = 511;
	int fifo_extra, fifo_left = fifo_size;
	int sprite0_fifo_extra = 0;
	unsigned int total_rate;
	enum plane_id plane_id;

	/*
	 * When enabling sprite0 after sprite1 has already been enabled
	 * we tend to get an underrun unless sprite0 already has some
	 * FIFO space allcoated. Hence we always allocate at least one
	 * cacheline for sprite0 whenever sprite1 is enabled.
	 *
	 * All other plane enable sequences appear immune to this problem.
	 */
	if (vlv_need_sprite0_fifo_workaround(active_planes))
		sprite0_fifo_extra = 1;

	total_rate = raw->plane[PLANE_PRIMARY] +
		raw->plane[PLANE_SPRITE0] +
		raw->plane[PLANE_SPRITE1] +
		sprite0_fifo_extra;

	if (total_rate > fifo_size)
		return -EINVAL;

	if (total_rate == 0)
		total_rate = 1;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		unsigned int rate;

		if ((active_planes & BIT(plane_id)) == 0) {
			fifo_state->plane[plane_id] = 0;
			continue;
		}

		rate = raw->plane[plane_id];
		fifo_state->plane[plane_id] = fifo_size * rate / total_rate;
		fifo_left -= fifo_state->plane[plane_id];
	}

	fifo_state->plane[PLANE_SPRITE0] += sprite0_fifo_extra;
	fifo_left -= sprite0_fifo_extra;

	fifo_state->plane[PLANE_CURSOR] = 63;

	fifo_extra = DIV_ROUND_UP(fifo_left, num_active_planes ?: 1);

	/* spread the remainder evenly */
	for_each_plane_id_on_crtc(crtc, plane_id) {
		int plane_extra;

		if (fifo_left == 0)
			break;

		if ((active_planes & BIT(plane_id)) == 0)
			continue;

		plane_extra = min(fifo_extra, fifo_left);
		fifo_state->plane[plane_id] += plane_extra;
		fifo_left -= plane_extra;
	}

	drm_WARN_ON(&dev_priv->drm, active_planes != 0 && fifo_left != 0);

	/* give it all to the first plane if none are active */
	if (active_planes == 0) {
		drm_WARN_ON(&dev_priv->drm, fifo_left != fifo_size);
		fifo_state->plane[PLANE_PRIMARY] = fifo_left;
	}

	return 0;
}

/* mark all levels starting from 'level' as invalid */
static void vlv_invalidate_wms(struct intel_crtc *crtc,
			       struct vlv_wm_state *wm_state, int level)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	for (; level < dev_priv->display.wm.num_levels; level++) {
		enum plane_id plane_id;

		for_each_plane_id_on_crtc(crtc, plane_id)
			wm_state->wm[level].plane[plane_id] = USHRT_MAX;

		wm_state->sr[level].cursor = USHRT_MAX;
		wm_state->sr[level].plane = USHRT_MAX;
	}
}

static u16 vlv_invert_wm_value(u16 wm, u16 fifo_size)
{
	if (wm > fifo_size)
		return USHRT_MAX;
	else
		return fifo_size - wm;
}

/*
 * Starting from 'level' set all higher
 * levels to 'value' in the "raw" watermarks.
 */
static bool vlv_raw_plane_wm_set(struct intel_crtc_state *crtc_state,
				 int level, enum plane_id plane_id, u16 value)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	bool dirty = false;

	for (; level < dev_priv->display.wm.num_levels; level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.vlv.raw[level];

		dirty |= raw->plane[plane_id] != value;
		raw->plane[plane_id] = value;
	}

	return dirty;
}

static bool vlv_raw_plane_wm_compute(struct intel_crtc_state *crtc_state,
				     const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	enum plane_id plane_id = plane->id;
	int level;
	bool dirty = false;

	if (!intel_wm_plane_visible(crtc_state, plane_state)) {
		dirty |= vlv_raw_plane_wm_set(crtc_state, 0, plane_id, 0);
		goto out;
	}

	for (level = 0; level < dev_priv->display.wm.num_levels; level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.vlv.raw[level];
		int wm = vlv_compute_wm_level(crtc_state, plane_state, level);
		int max_wm = plane_id == PLANE_CURSOR ? 63 : 511;

		if (wm > max_wm)
			break;

		dirty |= raw->plane[plane_id] != wm;
		raw->plane[plane_id] = wm;
	}

	/* mark all higher levels as invalid */
	dirty |= vlv_raw_plane_wm_set(crtc_state, level, plane_id, USHRT_MAX);

out:
	if (dirty)
		drm_dbg_kms(&dev_priv->drm,
			    "%s watermarks: PM2=%d, PM5=%d, DDR DVFS=%d\n",
			    plane->base.name,
			    crtc_state->wm.vlv.raw[VLV_WM_LEVEL_PM2].plane[plane_id],
			    crtc_state->wm.vlv.raw[VLV_WM_LEVEL_PM5].plane[plane_id],
			    crtc_state->wm.vlv.raw[VLV_WM_LEVEL_DDR_DVFS].plane[plane_id]);

	return dirty;
}

static bool vlv_raw_plane_wm_is_valid(const struct intel_crtc_state *crtc_state,
				      enum plane_id plane_id, int level)
{
	const struct g4x_pipe_wm *raw =
		&crtc_state->wm.vlv.raw[level];
	const struct vlv_fifo_state *fifo_state =
		&crtc_state->wm.vlv.fifo_state;

	return raw->plane[plane_id] <= fifo_state->plane[plane_id];
}

static bool vlv_raw_crtc_wm_is_valid(const struct intel_crtc_state *crtc_state, int level)
{
	return vlv_raw_plane_wm_is_valid(crtc_state, PLANE_PRIMARY, level) &&
		vlv_raw_plane_wm_is_valid(crtc_state, PLANE_SPRITE0, level) &&
		vlv_raw_plane_wm_is_valid(crtc_state, PLANE_SPRITE1, level) &&
		vlv_raw_plane_wm_is_valid(crtc_state, PLANE_CURSOR, level);
}

static int _vlv_compute_pipe_wm(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct vlv_wm_state *wm_state = &crtc_state->wm.vlv.optimal;
	const struct vlv_fifo_state *fifo_state =
		&crtc_state->wm.vlv.fifo_state;
	u8 active_planes = crtc_state->active_planes & ~BIT(PLANE_CURSOR);
	int num_active_planes = hweight8(active_planes);
	enum plane_id plane_id;
	int level;

	/* initially allow all levels */
	wm_state->num_levels = dev_priv->display.wm.num_levels;
	/*
	 * Note that enabling cxsr with no primary/sprite planes
	 * enabled can wedge the pipe. Hence we only allow cxsr
	 * with exactly one enabled primary/sprite plane.
	 */
	wm_state->cxsr = crtc->pipe != PIPE_C && num_active_planes == 1;

	for (level = 0; level < wm_state->num_levels; level++) {
		const struct g4x_pipe_wm *raw = &crtc_state->wm.vlv.raw[level];
		const int sr_fifo_size = INTEL_NUM_PIPES(dev_priv) * 512 - 1;

		if (!vlv_raw_crtc_wm_is_valid(crtc_state, level))
			break;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			wm_state->wm[level].plane[plane_id] =
				vlv_invert_wm_value(raw->plane[plane_id],
						    fifo_state->plane[plane_id]);
		}

		wm_state->sr[level].plane =
			vlv_invert_wm_value(max3(raw->plane[PLANE_PRIMARY],
						 raw->plane[PLANE_SPRITE0],
						 raw->plane[PLANE_SPRITE1]),
					    sr_fifo_size);

		wm_state->sr[level].cursor =
			vlv_invert_wm_value(raw->plane[PLANE_CURSOR],
					    63);
	}

	if (level == 0)
		return -EINVAL;

	/* limit to only levels we can actually handle */
	wm_state->num_levels = level;

	/* invalidate the higher levels */
	vlv_invalidate_wms(crtc, wm_state, level);

	return 0;
}

static int vlv_compute_pipe_wm(struct intel_atomic_state *state,
			       struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_plane_state *old_plane_state;
	const struct intel_plane_state *new_plane_state;
	struct intel_plane *plane;
	unsigned int dirty = 0;
	int i;

	for_each_oldnew_intel_plane_in_state(state, plane,
					     old_plane_state,
					     new_plane_state, i) {
		if (new_plane_state->hw.crtc != &crtc->base &&
		    old_plane_state->hw.crtc != &crtc->base)
			continue;

		if (vlv_raw_plane_wm_compute(crtc_state, new_plane_state))
			dirty |= BIT(plane->id);
	}

	/*
	 * DSPARB registers may have been reset due to the
	 * power well being turned off. Make sure we restore
	 * them to a consistent state even if no primary/sprite
	 * planes are initially active. We also force a FIFO
	 * recomputation so that we are sure to sanitize the
	 * FIFO setting we took over from the BIOS even if there
	 * are no active planes on the crtc.
	 */
	if (intel_crtc_needs_modeset(crtc_state))
		dirty = ~0;

	if (!dirty)
		return 0;

	/* cursor changes don't warrant a FIFO recompute */
	if (dirty & ~BIT(PLANE_CURSOR)) {
		const struct intel_crtc_state *old_crtc_state =
			intel_atomic_get_old_crtc_state(state, crtc);
		const struct vlv_fifo_state *old_fifo_state =
			&old_crtc_state->wm.vlv.fifo_state;
		const struct vlv_fifo_state *new_fifo_state =
			&crtc_state->wm.vlv.fifo_state;
		int ret;

		ret = vlv_compute_fifo(crtc_state);
		if (ret)
			return ret;

		if (intel_crtc_needs_modeset(crtc_state) ||
		    memcmp(old_fifo_state, new_fifo_state,
			   sizeof(*new_fifo_state)) != 0)
			crtc_state->fifo_changed = true;
	}

	return _vlv_compute_pipe_wm(crtc_state);
}

#define VLV_FIFO(plane, value) \
	(((value) << DSPARB_ ## plane ## _SHIFT_VLV) & DSPARB_ ## plane ## _MASK_VLV)

static void vlv_atomic_update_fifo(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_uncore *uncore = &dev_priv->uncore;
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct vlv_fifo_state *fifo_state =
		&crtc_state->wm.vlv.fifo_state;
	int sprite0_start, sprite1_start, fifo_size;
	u32 dsparb, dsparb2, dsparb3;

	if (!crtc_state->fifo_changed)
		return;

	sprite0_start = fifo_state->plane[PLANE_PRIMARY];
	sprite1_start = fifo_state->plane[PLANE_SPRITE0] + sprite0_start;
	fifo_size = fifo_state->plane[PLANE_SPRITE1] + sprite1_start;

	drm_WARN_ON(&dev_priv->drm, fifo_state->plane[PLANE_CURSOR] != 63);
	drm_WARN_ON(&dev_priv->drm, fifo_size != 511);

	trace_vlv_fifo_size(crtc, sprite0_start, sprite1_start, fifo_size);

	/*
	 * uncore.lock serves a double purpose here. It allows us to
	 * use the less expensive I915_{READ,WRITE}_FW() functions, and
	 * it protects the DSPARB registers from getting clobbered by
	 * parallel updates from multiple pipes.
	 *
	 * intel_pipe_update_start() has already disabled interrupts
	 * for us, so a plain spin_lock() is sufficient here.
	 */
	spin_lock(&uncore->lock);

	switch (crtc->pipe) {
	case PIPE_A:
		dsparb = intel_uncore_read_fw(uncore, DSPARB(dev_priv));
		dsparb2 = intel_uncore_read_fw(uncore, DSPARB2);

		dsparb &= ~(VLV_FIFO(SPRITEA, 0xff) |
			    VLV_FIFO(SPRITEB, 0xff));
		dsparb |= (VLV_FIFO(SPRITEA, sprite0_start) |
			   VLV_FIFO(SPRITEB, sprite1_start));

		dsparb2 &= ~(VLV_FIFO(SPRITEA_HI, 0x1) |
			     VLV_FIFO(SPRITEB_HI, 0x1));
		dsparb2 |= (VLV_FIFO(SPRITEA_HI, sprite0_start >> 8) |
			   VLV_FIFO(SPRITEB_HI, sprite1_start >> 8));

		intel_uncore_write_fw(uncore, DSPARB(dev_priv), dsparb);
		intel_uncore_write_fw(uncore, DSPARB2, dsparb2);
		break;
	case PIPE_B:
		dsparb = intel_uncore_read_fw(uncore, DSPARB(dev_priv));
		dsparb2 = intel_uncore_read_fw(uncore, DSPARB2);

		dsparb &= ~(VLV_FIFO(SPRITEC, 0xff) |
			    VLV_FIFO(SPRITED, 0xff));
		dsparb |= (VLV_FIFO(SPRITEC, sprite0_start) |
			   VLV_FIFO(SPRITED, sprite1_start));

		dsparb2 &= ~(VLV_FIFO(SPRITEC_HI, 0xff) |
			     VLV_FIFO(SPRITED_HI, 0xff));
		dsparb2 |= (VLV_FIFO(SPRITEC_HI, sprite0_start >> 8) |
			   VLV_FIFO(SPRITED_HI, sprite1_start >> 8));

		intel_uncore_write_fw(uncore, DSPARB(dev_priv), dsparb);
		intel_uncore_write_fw(uncore, DSPARB2, dsparb2);
		break;
	case PIPE_C:
		dsparb3 = intel_uncore_read_fw(uncore, DSPARB3);
		dsparb2 = intel_uncore_read_fw(uncore, DSPARB2);

		dsparb3 &= ~(VLV_FIFO(SPRITEE, 0xff) |
			     VLV_FIFO(SPRITEF, 0xff));
		dsparb3 |= (VLV_FIFO(SPRITEE, sprite0_start) |
			    VLV_FIFO(SPRITEF, sprite1_start));

		dsparb2 &= ~(VLV_FIFO(SPRITEE_HI, 0xff) |
			     VLV_FIFO(SPRITEF_HI, 0xff));
		dsparb2 |= (VLV_FIFO(SPRITEE_HI, sprite0_start >> 8) |
			   VLV_FIFO(SPRITEF_HI, sprite1_start >> 8));

		intel_uncore_write_fw(uncore, DSPARB3, dsparb3);
		intel_uncore_write_fw(uncore, DSPARB2, dsparb2);
		break;
	default:
		break;
	}

	intel_uncore_posting_read_fw(uncore, DSPARB(dev_priv));

	spin_unlock(&uncore->lock);
}

#undef VLV_FIFO

static int vlv_compute_intermediate_wm(struct intel_atomic_state *state,
				       struct intel_crtc *crtc)
{
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct vlv_wm_state *intermediate = &new_crtc_state->wm.vlv.intermediate;
	const struct vlv_wm_state *optimal = &new_crtc_state->wm.vlv.optimal;
	const struct vlv_wm_state *active = &old_crtc_state->wm.vlv.optimal;
	int level;

	if (!new_crtc_state->hw.active ||
	    intel_crtc_needs_modeset(new_crtc_state)) {
		*intermediate = *optimal;

		intermediate->cxsr = false;
		goto out;
	}

	intermediate->num_levels = min(optimal->num_levels, active->num_levels);
	intermediate->cxsr = optimal->cxsr && active->cxsr &&
		!new_crtc_state->disable_cxsr;

	for (level = 0; level < intermediate->num_levels; level++) {
		enum plane_id plane_id;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			intermediate->wm[level].plane[plane_id] =
				min(optimal->wm[level].plane[plane_id],
				    active->wm[level].plane[plane_id]);
		}

		intermediate->sr[level].plane = min(optimal->sr[level].plane,
						    active->sr[level].plane);
		intermediate->sr[level].cursor = min(optimal->sr[level].cursor,
						     active->sr[level].cursor);
	}

	vlv_invalidate_wms(crtc, intermediate, level);

out:
	/*
	 * If our intermediate WM are identical to the final WM, then we can
	 * omit the post-vblank programming; only update if it's different.
	 */
	if (memcmp(intermediate, optimal, sizeof(*intermediate)) != 0)
		new_crtc_state->wm.need_postvbl_update = true;

	return 0;
}

static void vlv_merge_wm(struct drm_i915_private *dev_priv,
			 struct vlv_wm_values *wm)
{
	struct intel_crtc *crtc;
	int num_active_pipes = 0;

	wm->level = dev_priv->display.wm.num_levels - 1;
	wm->cxsr = true;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct vlv_wm_state *wm_state = &crtc->wm.active.vlv;

		if (!crtc->active)
			continue;

		if (!wm_state->cxsr)
			wm->cxsr = false;

		num_active_pipes++;
		wm->level = min_t(int, wm->level, wm_state->num_levels - 1);
	}

	if (num_active_pipes != 1)
		wm->cxsr = false;

	if (num_active_pipes > 1)
		wm->level = VLV_WM_LEVEL_PM2;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct vlv_wm_state *wm_state = &crtc->wm.active.vlv;
		enum pipe pipe = crtc->pipe;

		wm->pipe[pipe] = wm_state->wm[wm->level];
		if (crtc->active && wm->cxsr)
			wm->sr = wm_state->sr[wm->level];

		wm->ddl[pipe].plane[PLANE_PRIMARY] = DDL_PRECISION_HIGH | 2;
		wm->ddl[pipe].plane[PLANE_SPRITE0] = DDL_PRECISION_HIGH | 2;
		wm->ddl[pipe].plane[PLANE_SPRITE1] = DDL_PRECISION_HIGH | 2;
		wm->ddl[pipe].plane[PLANE_CURSOR] = DDL_PRECISION_HIGH | 2;
	}
}

static void vlv_program_watermarks(struct drm_i915_private *dev_priv)
{
	struct vlv_wm_values *old_wm = &dev_priv->display.wm.vlv;
	struct vlv_wm_values new_wm = {};

	vlv_merge_wm(dev_priv, &new_wm);

	if (memcmp(old_wm, &new_wm, sizeof(new_wm)) == 0)
		return;

	if (is_disabling(old_wm->level, new_wm.level, VLV_WM_LEVEL_DDR_DVFS))
		chv_set_memory_dvfs(dev_priv, false);

	if (is_disabling(old_wm->level, new_wm.level, VLV_WM_LEVEL_PM5))
		chv_set_memory_pm5(dev_priv, false);

	if (is_disabling(old_wm->cxsr, new_wm.cxsr, true))
		_intel_set_memory_cxsr(dev_priv, false);

	vlv_write_wm_values(dev_priv, &new_wm);

	if (is_enabling(old_wm->cxsr, new_wm.cxsr, true))
		_intel_set_memory_cxsr(dev_priv, true);

	if (is_enabling(old_wm->level, new_wm.level, VLV_WM_LEVEL_PM5))
		chv_set_memory_pm5(dev_priv, true);

	if (is_enabling(old_wm->level, new_wm.level, VLV_WM_LEVEL_DDR_DVFS))
		chv_set_memory_dvfs(dev_priv, true);

	*old_wm = new_wm;
}

static void vlv_initial_watermarks(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	mutex_lock(&dev_priv->display.wm.wm_mutex);
	crtc->wm.active.vlv = crtc_state->wm.vlv.intermediate;
	vlv_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->display.wm.wm_mutex);
}

static void vlv_optimize_watermarks(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!crtc_state->wm.need_postvbl_update)
		return;

	mutex_lock(&dev_priv->display.wm.wm_mutex);
	crtc->wm.active.vlv = crtc_state->wm.vlv.optimal;
	vlv_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->display.wm.wm_mutex);
}

static void i965_update_wm(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;
	int srwm = 1;
	int cursor_sr = 16;
	bool cxsr_enabled;

	/* Calc sr entries for one plane configs */
	crtc = single_enabled_crtc(dev_priv);
	if (crtc) {
		/* self-refresh has much higher latency */
		static const int sr_latency_ns = 12000;
		const struct drm_display_mode *pipe_mode =
			&crtc->config->hw.pipe_mode;
		const struct drm_framebuffer *fb =
			crtc->base.primary->state->fb;
		int pixel_rate = crtc->config->pixel_rate;
		int htotal = pipe_mode->crtc_htotal;
		int width = drm_rect_width(&crtc->base.primary->state->src) >> 16;
		int cpp = fb->format->cpp[0];
		int entries;

		entries = intel_wm_method2(pixel_rate, htotal,
					   width, cpp, sr_latency_ns / 100);
		entries = DIV_ROUND_UP(entries, I915_FIFO_LINE_SIZE);
		srwm = I965_FIFO_SIZE - entries;
		if (srwm < 0)
			srwm = 1;
		srwm &= 0x1ff;
		drm_dbg_kms(&dev_priv->drm,
			    "self-refresh entries: %d, wm: %d\n",
			    entries, srwm);

		entries = intel_wm_method2(pixel_rate, htotal,
					   crtc->base.cursor->state->crtc_w, 4,
					   sr_latency_ns / 100);
		entries = DIV_ROUND_UP(entries,
				       i965_cursor_wm_info.cacheline_size) +
			i965_cursor_wm_info.guard_size;

		cursor_sr = i965_cursor_wm_info.fifo_size - entries;
		if (cursor_sr > i965_cursor_wm_info.max_wm)
			cursor_sr = i965_cursor_wm_info.max_wm;

		drm_dbg_kms(&dev_priv->drm,
			    "self-refresh watermark: display plane %d "
			    "cursor %d\n", srwm, cursor_sr);

		cxsr_enabled = true;
	} else {
		cxsr_enabled = false;
		/* Turn off self refresh if both pipes are enabled */
		intel_set_memory_cxsr(dev_priv, false);
	}

	drm_dbg_kms(&dev_priv->drm,
		    "Setting FIFO watermarks - A: 8, B: 8, C: 8, SR %d\n",
		    srwm);

	/* 965 has limitations... */
	intel_uncore_write(&dev_priv->uncore, DSPFW1(dev_priv),
			   FW_WM(srwm, SR) |
			   FW_WM(8, CURSORB) |
			   FW_WM(8, PLANEB) |
			   FW_WM(8, PLANEA));
	intel_uncore_write(&dev_priv->uncore, DSPFW2(dev_priv),
			   FW_WM(8, CURSORA) |
			   FW_WM(8, PLANEC_OLD));
	/* update cursor SR watermark */
	intel_uncore_write(&dev_priv->uncore, DSPFW3(dev_priv),
			   FW_WM(cursor_sr, CURSOR_SR));

	if (cxsr_enabled)
		intel_set_memory_cxsr(dev_priv, true);
}

#undef FW_WM

static struct intel_crtc *intel_crtc_for_plane(struct drm_i915_private *i915,
					       enum i9xx_plane_id i9xx_plane)
{
	struct intel_plane *plane;

	for_each_intel_plane(&i915->drm, plane) {
		if (plane->id == PLANE_PRIMARY &&
		    plane->i9xx_plane == i9xx_plane)
			return intel_crtc_for_pipe(i915, plane->pipe);
	}

	return NULL;
}

static void i9xx_update_wm(struct drm_i915_private *dev_priv)
{
	const struct intel_watermark_params *wm_info;
	u32 fwater_lo;
	u32 fwater_hi;
	int cwm, srwm = 1;
	int fifo_size;
	int planea_wm, planeb_wm;
	struct intel_crtc *crtc;

	if (IS_I945GM(dev_priv))
		wm_info = &i945_wm_info;
	else if (DISPLAY_VER(dev_priv) != 2)
		wm_info = &i915_wm_info;
	else
		wm_info = &i830_a_wm_info;

	if (DISPLAY_VER(dev_priv) == 2)
		fifo_size = i830_get_fifo_size(dev_priv, PLANE_A);
	else
		fifo_size = i9xx_get_fifo_size(dev_priv, PLANE_A);
	crtc = intel_crtc_for_plane(dev_priv, PLANE_A);
	if (intel_crtc_active(crtc)) {
		const struct drm_framebuffer *fb =
			crtc->base.primary->state->fb;
		int cpp;

		if (DISPLAY_VER(dev_priv) == 2)
			cpp = 4;
		else
			cpp = fb->format->cpp[0];

		planea_wm = intel_calculate_wm(dev_priv, crtc->config->pixel_rate,
					       wm_info, fifo_size, cpp,
					       pessimal_latency_ns);
	} else {
		planea_wm = fifo_size - wm_info->guard_size;
		if (planea_wm > (long)wm_info->max_wm)
			planea_wm = wm_info->max_wm;
	}

	if (DISPLAY_VER(dev_priv) == 2)
		wm_info = &i830_bc_wm_info;

	if (DISPLAY_VER(dev_priv) == 2)
		fifo_size = i830_get_fifo_size(dev_priv, PLANE_B);
	else
		fifo_size = i9xx_get_fifo_size(dev_priv, PLANE_B);
	crtc = intel_crtc_for_plane(dev_priv, PLANE_B);
	if (intel_crtc_active(crtc)) {
		const struct drm_framebuffer *fb =
			crtc->base.primary->state->fb;
		int cpp;

		if (DISPLAY_VER(dev_priv) == 2)
			cpp = 4;
		else
			cpp = fb->format->cpp[0];

		planeb_wm = intel_calculate_wm(dev_priv, crtc->config->pixel_rate,
					       wm_info, fifo_size, cpp,
					       pessimal_latency_ns);
	} else {
		planeb_wm = fifo_size - wm_info->guard_size;
		if (planeb_wm > (long)wm_info->max_wm)
			planeb_wm = wm_info->max_wm;
	}

	drm_dbg_kms(&dev_priv->drm,
		    "FIFO watermarks - A: %d, B: %d\n", planea_wm, planeb_wm);

	crtc = single_enabled_crtc(dev_priv);
	if (IS_I915GM(dev_priv) && crtc) {
		struct drm_i915_gem_object *obj;

		obj = intel_fb_obj(crtc->base.primary->state->fb);

		/* self-refresh seems busted with untiled */
		if (!i915_gem_object_is_tiled(obj))
			crtc = NULL;
	}

	/*
	 * Overlay gets an aggressive default since video jitter is bad.
	 */
	cwm = 2;

	/* Play safe and disable self-refresh before adjusting watermarks. */
	intel_set_memory_cxsr(dev_priv, false);

	/* Calc sr entries for one plane configs */
	if (HAS_FW_BLC(dev_priv) && crtc) {
		/* self-refresh has much higher latency */
		static const int sr_latency_ns = 6000;
		const struct drm_display_mode *pipe_mode =
			&crtc->config->hw.pipe_mode;
		const struct drm_framebuffer *fb =
			crtc->base.primary->state->fb;
		int pixel_rate = crtc->config->pixel_rate;
		int htotal = pipe_mode->crtc_htotal;
		int width = drm_rect_width(&crtc->base.primary->state->src) >> 16;
		int cpp;
		int entries;

		if (IS_I915GM(dev_priv) || IS_I945GM(dev_priv))
			cpp = 4;
		else
			cpp = fb->format->cpp[0];

		entries = intel_wm_method2(pixel_rate, htotal, width, cpp,
					   sr_latency_ns / 100);
		entries = DIV_ROUND_UP(entries, wm_info->cacheline_size);
		drm_dbg_kms(&dev_priv->drm,
			    "self-refresh entries: %d\n", entries);
		srwm = wm_info->fifo_size - entries;
		if (srwm < 0)
			srwm = 1;

		if (IS_I945G(dev_priv) || IS_I945GM(dev_priv))
			intel_uncore_write(&dev_priv->uncore, FW_BLC_SELF,
				   FW_BLC_SELF_FIFO_MASK | (srwm & 0xff));
		else
			intel_uncore_write(&dev_priv->uncore, FW_BLC_SELF, srwm & 0x3f);
	}

	drm_dbg_kms(&dev_priv->drm,
		    "Setting FIFO watermarks - A: %d, B: %d, C: %d, SR %d\n",
		     planea_wm, planeb_wm, cwm, srwm);

	fwater_lo = ((planeb_wm & 0x3f) << 16) | (planea_wm & 0x3f);
	fwater_hi = (cwm & 0x1f);

	/* Set request length to 8 cachelines per fetch */
	fwater_lo = fwater_lo | (1 << 24) | (1 << 8);
	fwater_hi = fwater_hi | (1 << 8);

	intel_uncore_write(&dev_priv->uncore, FW_BLC, fwater_lo);
	intel_uncore_write(&dev_priv->uncore, FW_BLC2, fwater_hi);

	if (crtc)
		intel_set_memory_cxsr(dev_priv, true);
}

static void i845_update_wm(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;
	u32 fwater_lo;
	int planea_wm;

	crtc = single_enabled_crtc(dev_priv);
	if (crtc == NULL)
		return;

	planea_wm = intel_calculate_wm(dev_priv, crtc->config->pixel_rate,
				       &i845_wm_info,
				       i845_get_fifo_size(dev_priv, PLANE_A),
				       4, pessimal_latency_ns);
	fwater_lo = intel_uncore_read(&dev_priv->uncore, FW_BLC) & ~0xfff;
	fwater_lo |= (3<<8) | planea_wm;

	drm_dbg_kms(&dev_priv->drm,
		    "Setting FIFO watermarks - A: %d\n", planea_wm);

	intel_uncore_write(&dev_priv->uncore, FW_BLC, fwater_lo);
}

/* latency must be in 0.1us units. */
static unsigned int ilk_wm_method1(unsigned int pixel_rate,
				   unsigned int cpp,
				   unsigned int latency)
{
	unsigned int ret;

	ret = intel_wm_method1(pixel_rate, cpp, latency);
	ret = DIV_ROUND_UP(ret, 64) + 2;

	return ret;
}

/* latency must be in 0.1us units. */
static unsigned int ilk_wm_method2(unsigned int pixel_rate,
				   unsigned int htotal,
				   unsigned int width,
				   unsigned int cpp,
				   unsigned int latency)
{
	unsigned int ret;

	ret = intel_wm_method2(pixel_rate, htotal,
			       width, cpp, latency);
	ret = DIV_ROUND_UP(ret, 64) + 2;

	return ret;
}

static u32 ilk_wm_fbc(u32 pri_val, u32 horiz_pixels, u8 cpp)
{
	/*
	 * Neither of these should be possible since this function shouldn't be
	 * called if the CRTC is off or the plane is invisible.  But let's be
	 * extra paranoid to avoid a potential divide-by-zero if we screw up
	 * elsewhere in the driver.
	 */
	if (WARN_ON(!cpp))
		return 0;
	if (WARN_ON(!horiz_pixels))
		return 0;

	return DIV_ROUND_UP(pri_val * 64, horiz_pixels * cpp) + 2;
}

struct ilk_wm_maximums {
	u16 pri;
	u16 spr;
	u16 cur;
	u16 fbc;
};

/*
 * For both WM_PIPE and WM_LP.
 * mem_value must be in 0.1us units.
 */
static u32 ilk_compute_pri_wm(const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state,
			      u32 mem_value, bool is_lp)
{
	u32 method1, method2;
	int cpp;

	if (mem_value == 0)
		return U32_MAX;

	if (!intel_wm_plane_visible(crtc_state, plane_state))
		return 0;

	cpp = plane_state->hw.fb->format->cpp[0];

	method1 = ilk_wm_method1(crtc_state->pixel_rate, cpp, mem_value);

	if (!is_lp)
		return method1;

	method2 = ilk_wm_method2(crtc_state->pixel_rate,
				 crtc_state->hw.pipe_mode.crtc_htotal,
				 drm_rect_width(&plane_state->uapi.src) >> 16,
				 cpp, mem_value);

	return min(method1, method2);
}

/*
 * For both WM_PIPE and WM_LP.
 * mem_value must be in 0.1us units.
 */
static u32 ilk_compute_spr_wm(const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state,
			      u32 mem_value)
{
	u32 method1, method2;
	int cpp;

	if (mem_value == 0)
		return U32_MAX;

	if (!intel_wm_plane_visible(crtc_state, plane_state))
		return 0;

	cpp = plane_state->hw.fb->format->cpp[0];

	method1 = ilk_wm_method1(crtc_state->pixel_rate, cpp, mem_value);
	method2 = ilk_wm_method2(crtc_state->pixel_rate,
				 crtc_state->hw.pipe_mode.crtc_htotal,
				 drm_rect_width(&plane_state->uapi.src) >> 16,
				 cpp, mem_value);
	return min(method1, method2);
}

/*
 * For both WM_PIPE and WM_LP.
 * mem_value must be in 0.1us units.
 */
static u32 ilk_compute_cur_wm(const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state,
			      u32 mem_value)
{
	int cpp;

	if (mem_value == 0)
		return U32_MAX;

	if (!intel_wm_plane_visible(crtc_state, plane_state))
		return 0;

	cpp = plane_state->hw.fb->format->cpp[0];

	return ilk_wm_method2(crtc_state->pixel_rate,
			      crtc_state->hw.pipe_mode.crtc_htotal,
			      drm_rect_width(&plane_state->uapi.src) >> 16,
			      cpp, mem_value);
}

/* Only for WM_LP. */
static u32 ilk_compute_fbc_wm(const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state,
			      u32 pri_val)
{
	int cpp;

	if (!intel_wm_plane_visible(crtc_state, plane_state))
		return 0;

	cpp = plane_state->hw.fb->format->cpp[0];

	return ilk_wm_fbc(pri_val, drm_rect_width(&plane_state->uapi.src) >> 16,
			  cpp);
}

static unsigned int
ilk_display_fifo_size(const struct drm_i915_private *dev_priv)
{
	if (DISPLAY_VER(dev_priv) >= 8)
		return 3072;
	else if (DISPLAY_VER(dev_priv) >= 7)
		return 768;
	else
		return 512;
}

static unsigned int
ilk_plane_wm_reg_max(const struct drm_i915_private *dev_priv,
		     int level, bool is_sprite)
{
	if (DISPLAY_VER(dev_priv) >= 8)
		/* BDW primary/sprite plane watermarks */
		return level == 0 ? 255 : 2047;
	else if (DISPLAY_VER(dev_priv) >= 7)
		/* IVB/HSW primary/sprite plane watermarks */
		return level == 0 ? 127 : 1023;
	else if (!is_sprite)
		/* ILK/SNB primary plane watermarks */
		return level == 0 ? 127 : 511;
	else
		/* ILK/SNB sprite plane watermarks */
		return level == 0 ? 63 : 255;
}

static unsigned int
ilk_cursor_wm_reg_max(const struct drm_i915_private *dev_priv, int level)
{
	if (DISPLAY_VER(dev_priv) >= 7)
		return level == 0 ? 63 : 255;
	else
		return level == 0 ? 31 : 63;
}

static unsigned int ilk_fbc_wm_reg_max(const struct drm_i915_private *dev_priv)
{
	if (DISPLAY_VER(dev_priv) >= 8)
		return 31;
	else
		return 15;
}

/* Calculate the maximum primary/sprite plane watermark */
static unsigned int ilk_plane_wm_max(const struct drm_i915_private *dev_priv,
				     int level,
				     const struct intel_wm_config *config,
				     enum intel_ddb_partitioning ddb_partitioning,
				     bool is_sprite)
{
	unsigned int fifo_size = ilk_display_fifo_size(dev_priv);

	/* if sprites aren't enabled, sprites get nothing */
	if (is_sprite && !config->sprites_enabled)
		return 0;

	/* HSW allows LP1+ watermarks even with multiple pipes */
	if (level == 0 || config->num_pipes_active > 1) {
		fifo_size /= INTEL_NUM_PIPES(dev_priv);

		/*
		 * For some reason the non self refresh
		 * FIFO size is only half of the self
		 * refresh FIFO size on ILK/SNB.
		 */
		if (DISPLAY_VER(dev_priv) < 7)
			fifo_size /= 2;
	}

	if (config->sprites_enabled) {
		/* level 0 is always calculated with 1:1 split */
		if (level > 0 && ddb_partitioning == INTEL_DDB_PART_5_6) {
			if (is_sprite)
				fifo_size *= 5;
			fifo_size /= 6;
		} else {
			fifo_size /= 2;
		}
	}

	/* clamp to max that the registers can hold */
	return min(fifo_size, ilk_plane_wm_reg_max(dev_priv, level, is_sprite));
}

/* Calculate the maximum cursor plane watermark */
static unsigned int ilk_cursor_wm_max(const struct drm_i915_private *dev_priv,
				      int level,
				      const struct intel_wm_config *config)
{
	/* HSW LP1+ watermarks w/ multiple pipes */
	if (level > 0 && config->num_pipes_active > 1)
		return 64;

	/* otherwise just report max that registers can hold */
	return ilk_cursor_wm_reg_max(dev_priv, level);
}

static void ilk_compute_wm_maximums(const struct drm_i915_private *dev_priv,
				    int level,
				    const struct intel_wm_config *config,
				    enum intel_ddb_partitioning ddb_partitioning,
				    struct ilk_wm_maximums *max)
{
	max->pri = ilk_plane_wm_max(dev_priv, level, config, ddb_partitioning, false);
	max->spr = ilk_plane_wm_max(dev_priv, level, config, ddb_partitioning, true);
	max->cur = ilk_cursor_wm_max(dev_priv, level, config);
	max->fbc = ilk_fbc_wm_reg_max(dev_priv);
}

static void ilk_compute_wm_reg_maximums(const struct drm_i915_private *dev_priv,
					int level,
					struct ilk_wm_maximums *max)
{
	max->pri = ilk_plane_wm_reg_max(dev_priv, level, false);
	max->spr = ilk_plane_wm_reg_max(dev_priv, level, true);
	max->cur = ilk_cursor_wm_reg_max(dev_priv, level);
	max->fbc = ilk_fbc_wm_reg_max(dev_priv);
}

static bool ilk_validate_wm_level(struct drm_i915_private *i915,
				  int level,
				  const struct ilk_wm_maximums *max,
				  struct intel_wm_level *result)
{
	bool ret;

	/* already determined to be invalid? */
	if (!result->enable)
		return false;

	result->enable = result->pri_val <= max->pri &&
			 result->spr_val <= max->spr &&
			 result->cur_val <= max->cur;

	ret = result->enable;

	/*
	 * HACK until we can pre-compute everything,
	 * and thus fail gracefully if LP0 watermarks
	 * are exceeded...
	 */
	if (level == 0 && !result->enable) {
		if (result->pri_val > max->pri)
			drm_dbg_kms(&i915->drm,
				    "Primary WM%d too large %u (max %u)\n",
				    level, result->pri_val, max->pri);
		if (result->spr_val > max->spr)
			drm_dbg_kms(&i915->drm,
				    "Sprite WM%d too large %u (max %u)\n",
				    level, result->spr_val, max->spr);
		if (result->cur_val > max->cur)
			drm_dbg_kms(&i915->drm,
				    "Cursor WM%d too large %u (max %u)\n",
				    level, result->cur_val, max->cur);

		result->pri_val = min_t(u32, result->pri_val, max->pri);
		result->spr_val = min_t(u32, result->spr_val, max->spr);
		result->cur_val = min_t(u32, result->cur_val, max->cur);
		result->enable = true;
	}

	return ret;
}

static void ilk_compute_wm_level(const struct drm_i915_private *dev_priv,
				 const struct intel_crtc *crtc,
				 int level,
				 struct intel_crtc_state *crtc_state,
				 const struct intel_plane_state *pristate,
				 const struct intel_plane_state *sprstate,
				 const struct intel_plane_state *curstate,
				 struct intel_wm_level *result)
{
	u16 pri_latency = dev_priv->display.wm.pri_latency[level];
	u16 spr_latency = dev_priv->display.wm.spr_latency[level];
	u16 cur_latency = dev_priv->display.wm.cur_latency[level];

	/* WM1+ latency values stored in 0.5us units */
	if (level > 0) {
		pri_latency *= 5;
		spr_latency *= 5;
		cur_latency *= 5;
	}

	if (pristate) {
		result->pri_val = ilk_compute_pri_wm(crtc_state, pristate,
						     pri_latency, level);
		result->fbc_val = ilk_compute_fbc_wm(crtc_state, pristate, result->pri_val);
	}

	if (sprstate)
		result->spr_val = ilk_compute_spr_wm(crtc_state, sprstate, spr_latency);

	if (curstate)
		result->cur_val = ilk_compute_cur_wm(crtc_state, curstate, cur_latency);

	result->enable = true;
}

static void hsw_read_wm_latency(struct drm_i915_private *i915, u16 wm[])
{
	u64 sskpd;

	i915->display.wm.num_levels = 5;

	sskpd = intel_uncore_read64(&i915->uncore, MCH_SSKPD);

	wm[0] = REG_FIELD_GET64(SSKPD_NEW_WM0_MASK_HSW, sskpd);
	if (wm[0] == 0)
		wm[0] = REG_FIELD_GET64(SSKPD_OLD_WM0_MASK_HSW, sskpd);
	wm[1] = REG_FIELD_GET64(SSKPD_WM1_MASK_HSW, sskpd);
	wm[2] = REG_FIELD_GET64(SSKPD_WM2_MASK_HSW, sskpd);
	wm[3] = REG_FIELD_GET64(SSKPD_WM3_MASK_HSW, sskpd);
	wm[4] = REG_FIELD_GET64(SSKPD_WM4_MASK_HSW, sskpd);
}

static void snb_read_wm_latency(struct drm_i915_private *i915, u16 wm[])
{
	u32 sskpd;

	i915->display.wm.num_levels = 4;

	sskpd = intel_uncore_read(&i915->uncore, MCH_SSKPD);

	wm[0] = REG_FIELD_GET(SSKPD_WM0_MASK_SNB, sskpd);
	wm[1] = REG_FIELD_GET(SSKPD_WM1_MASK_SNB, sskpd);
	wm[2] = REG_FIELD_GET(SSKPD_WM2_MASK_SNB, sskpd);
	wm[3] = REG_FIELD_GET(SSKPD_WM3_MASK_SNB, sskpd);
}

static void ilk_read_wm_latency(struct drm_i915_private *i915, u16 wm[])
{
	u32 mltr;

	i915->display.wm.num_levels = 3;

	mltr = intel_uncore_read(&i915->uncore, MLTR_ILK);

	/* ILK primary LP0 latency is 700 ns */
	wm[0] = 7;
	wm[1] = REG_FIELD_GET(MLTR_WM1_MASK, mltr);
	wm[2] = REG_FIELD_GET(MLTR_WM2_MASK, mltr);
}

static void intel_fixup_spr_wm_latency(struct drm_i915_private *dev_priv,
				       u16 wm[5])
{
	/* ILK sprite LP0 latency is 1300 ns */
	if (DISPLAY_VER(dev_priv) == 5)
		wm[0] = 13;
}

static void intel_fixup_cur_wm_latency(struct drm_i915_private *dev_priv,
				       u16 wm[5])
{
	/* ILK cursor LP0 latency is 1300 ns */
	if (DISPLAY_VER(dev_priv) == 5)
		wm[0] = 13;
}

static bool ilk_increase_wm_latency(struct drm_i915_private *dev_priv,
				    u16 wm[5], u16 min)
{
	int level;

	if (wm[0] >= min)
		return false;

	wm[0] = max(wm[0], min);
	for (level = 1; level < dev_priv->display.wm.num_levels; level++)
		wm[level] = max_t(u16, wm[level], DIV_ROUND_UP(min, 5));

	return true;
}

static void snb_wm_latency_quirk(struct drm_i915_private *dev_priv)
{
	bool changed;

	/*
	 * The BIOS provided WM memory latency values are often
	 * inadequate for high resolution displays. Adjust them.
	 */
	changed = ilk_increase_wm_latency(dev_priv, dev_priv->display.wm.pri_latency, 12);
	changed |= ilk_increase_wm_latency(dev_priv, dev_priv->display.wm.spr_latency, 12);
	changed |= ilk_increase_wm_latency(dev_priv, dev_priv->display.wm.cur_latency, 12);

	if (!changed)
		return;

	drm_dbg_kms(&dev_priv->drm,
		    "WM latency values increased to avoid potential underruns\n");
	intel_print_wm_latency(dev_priv, "Primary", dev_priv->display.wm.pri_latency);
	intel_print_wm_latency(dev_priv, "Sprite", dev_priv->display.wm.spr_latency);
	intel_print_wm_latency(dev_priv, "Cursor", dev_priv->display.wm.cur_latency);
}

static void snb_wm_lp3_irq_quirk(struct drm_i915_private *dev_priv)
{
	/*
	 * On some SNB machines (Thinkpad X220 Tablet at least)
	 * LP3 usage can cause vblank interrupts to be lost.
	 * The DEIIR bit will go high but it looks like the CPU
	 * never gets interrupted.
	 *
	 * It's not clear whether other interrupt source could
	 * be affected or if this is somehow limited to vblank
	 * interrupts only. To play it safe we disable LP3
	 * watermarks entirely.
	 */
	if (dev_priv->display.wm.pri_latency[3] == 0 &&
	    dev_priv->display.wm.spr_latency[3] == 0 &&
	    dev_priv->display.wm.cur_latency[3] == 0)
		return;

	dev_priv->display.wm.pri_latency[3] = 0;
	dev_priv->display.wm.spr_latency[3] = 0;
	dev_priv->display.wm.cur_latency[3] = 0;

	drm_dbg_kms(&dev_priv->drm,
		    "LP3 watermarks disabled due to potential for lost interrupts\n");
	intel_print_wm_latency(dev_priv, "Primary", dev_priv->display.wm.pri_latency);
	intel_print_wm_latency(dev_priv, "Sprite", dev_priv->display.wm.spr_latency);
	intel_print_wm_latency(dev_priv, "Cursor", dev_priv->display.wm.cur_latency);
}

static void ilk_setup_wm_latency(struct drm_i915_private *dev_priv)
{
	if (IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv))
		hsw_read_wm_latency(dev_priv, dev_priv->display.wm.pri_latency);
	else if (DISPLAY_VER(dev_priv) >= 6)
		snb_read_wm_latency(dev_priv, dev_priv->display.wm.pri_latency);
	else
		ilk_read_wm_latency(dev_priv, dev_priv->display.wm.pri_latency);

	memcpy(dev_priv->display.wm.spr_latency, dev_priv->display.wm.pri_latency,
	       sizeof(dev_priv->display.wm.pri_latency));
	memcpy(dev_priv->display.wm.cur_latency, dev_priv->display.wm.pri_latency,
	       sizeof(dev_priv->display.wm.pri_latency));

	intel_fixup_spr_wm_latency(dev_priv, dev_priv->display.wm.spr_latency);
	intel_fixup_cur_wm_latency(dev_priv, dev_priv->display.wm.cur_latency);

	intel_print_wm_latency(dev_priv, "Primary", dev_priv->display.wm.pri_latency);
	intel_print_wm_latency(dev_priv, "Sprite", dev_priv->display.wm.spr_latency);
	intel_print_wm_latency(dev_priv, "Cursor", dev_priv->display.wm.cur_latency);

	if (DISPLAY_VER(dev_priv) == 6) {
		snb_wm_latency_quirk(dev_priv);
		snb_wm_lp3_irq_quirk(dev_priv);
	}
}

static bool ilk_validate_pipe_wm(struct drm_i915_private *dev_priv,
				 struct intel_pipe_wm *pipe_wm)
{
	/* LP0 watermark maximums depend on this pipe alone */
	const struct intel_wm_config config = {
		.num_pipes_active = 1,
		.sprites_enabled = pipe_wm->sprites_enabled,
		.sprites_scaled = pipe_wm->sprites_scaled,
	};
	struct ilk_wm_maximums max;

	/* LP0 watermarks always use 1/2 DDB partitioning */
	ilk_compute_wm_maximums(dev_priv, 0, &config, INTEL_DDB_PART_1_2, &max);

	/* At least LP0 must be valid */
	if (!ilk_validate_wm_level(dev_priv, 0, &max, &pipe_wm->wm[0])) {
		drm_dbg_kms(&dev_priv->drm, "LP0 watermark invalid\n");
		return false;
	}

	return true;
}

/* Compute new watermarks for the pipe */
static int ilk_compute_pipe_wm(struct intel_atomic_state *state,
			       struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_pipe_wm *pipe_wm;
	struct intel_plane *plane;
	const struct intel_plane_state *plane_state;
	const struct intel_plane_state *pristate = NULL;
	const struct intel_plane_state *sprstate = NULL;
	const struct intel_plane_state *curstate = NULL;
	struct ilk_wm_maximums max;
	int level, usable_level;

	pipe_wm = &crtc_state->wm.ilk.optimal;

	intel_atomic_crtc_state_for_each_plane_state(plane, plane_state, crtc_state) {
		if (plane->base.type == DRM_PLANE_TYPE_PRIMARY)
			pristate = plane_state;
		else if (plane->base.type == DRM_PLANE_TYPE_OVERLAY)
			sprstate = plane_state;
		else if (plane->base.type == DRM_PLANE_TYPE_CURSOR)
			curstate = plane_state;
	}

	pipe_wm->pipe_enabled = crtc_state->hw.active;
	pipe_wm->sprites_enabled = crtc_state->active_planes & BIT(PLANE_SPRITE0);
	pipe_wm->sprites_scaled = crtc_state->scaled_planes & BIT(PLANE_SPRITE0);

	usable_level = dev_priv->display.wm.num_levels - 1;

	/* ILK/SNB: LP2+ watermarks only w/o sprites */
	if (DISPLAY_VER(dev_priv) < 7 && pipe_wm->sprites_enabled)
		usable_level = 1;

	/* ILK/SNB/IVB: LP1+ watermarks only w/o scaling */
	if (pipe_wm->sprites_scaled)
		usable_level = 0;

	memset(&pipe_wm->wm, 0, sizeof(pipe_wm->wm));
	ilk_compute_wm_level(dev_priv, crtc, 0, crtc_state,
			     pristate, sprstate, curstate, &pipe_wm->wm[0]);

	if (!ilk_validate_pipe_wm(dev_priv, pipe_wm))
		return -EINVAL;

	ilk_compute_wm_reg_maximums(dev_priv, 1, &max);

	for (level = 1; level <= usable_level; level++) {
		struct intel_wm_level *wm = &pipe_wm->wm[level];

		ilk_compute_wm_level(dev_priv, crtc, level, crtc_state,
				     pristate, sprstate, curstate, wm);

		/*
		 * Disable any watermark level that exceeds the
		 * register maximums since such watermarks are
		 * always invalid.
		 */
		if (!ilk_validate_wm_level(dev_priv, level, &max, wm)) {
			memset(wm, 0, sizeof(*wm));
			break;
		}
	}

	return 0;
}

/*
 * Build a set of 'intermediate' watermark values that satisfy both the old
 * state and the new state.  These can be programmed to the hardware
 * immediately.
 */
static int ilk_compute_intermediate_wm(struct intel_atomic_state *state,
				       struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_pipe_wm *a = &new_crtc_state->wm.ilk.intermediate;
	const struct intel_pipe_wm *b = &old_crtc_state->wm.ilk.optimal;
	int level;

	/*
	 * Start with the final, target watermarks, then combine with the
	 * currently active watermarks to get values that are safe both before
	 * and after the vblank.
	 */
	*a = new_crtc_state->wm.ilk.optimal;
	if (!new_crtc_state->hw.active ||
	    intel_crtc_needs_modeset(new_crtc_state) ||
	    state->skip_intermediate_wm)
		return 0;

	a->pipe_enabled |= b->pipe_enabled;
	a->sprites_enabled |= b->sprites_enabled;
	a->sprites_scaled |= b->sprites_scaled;

	for (level = 0; level < dev_priv->display.wm.num_levels; level++) {
		struct intel_wm_level *a_wm = &a->wm[level];
		const struct intel_wm_level *b_wm = &b->wm[level];

		a_wm->enable &= b_wm->enable;
		a_wm->pri_val = max(a_wm->pri_val, b_wm->pri_val);
		a_wm->spr_val = max(a_wm->spr_val, b_wm->spr_val);
		a_wm->cur_val = max(a_wm->cur_val, b_wm->cur_val);
		a_wm->fbc_val = max(a_wm->fbc_val, b_wm->fbc_val);
	}

	/*
	 * We need to make sure that these merged watermark values are
	 * actually a valid configuration themselves.  If they're not,
	 * there's no safe way to transition from the old state to
	 * the new state, so we need to fail the atomic transaction.
	 */
	if (!ilk_validate_pipe_wm(dev_priv, a))
		return -EINVAL;

	/*
	 * If our intermediate WM are identical to the final WM, then we can
	 * omit the post-vblank programming; only update if it's different.
	 */
	if (memcmp(a, &new_crtc_state->wm.ilk.optimal, sizeof(*a)) != 0)
		new_crtc_state->wm.need_postvbl_update = true;

	return 0;
}

/*
 * Merge the watermarks from all active pipes for a specific level.
 */
static void ilk_merge_wm_level(struct drm_i915_private *dev_priv,
			       int level,
			       struct intel_wm_level *ret_wm)
{
	const struct intel_crtc *crtc;

	ret_wm->enable = true;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct intel_pipe_wm *active = &crtc->wm.active.ilk;
		const struct intel_wm_level *wm = &active->wm[level];

		if (!active->pipe_enabled)
			continue;

		/*
		 * The watermark values may have been used in the past,
		 * so we must maintain them in the registers for some
		 * time even if the level is now disabled.
		 */
		if (!wm->enable)
			ret_wm->enable = false;

		ret_wm->pri_val = max(ret_wm->pri_val, wm->pri_val);
		ret_wm->spr_val = max(ret_wm->spr_val, wm->spr_val);
		ret_wm->cur_val = max(ret_wm->cur_val, wm->cur_val);
		ret_wm->fbc_val = max(ret_wm->fbc_val, wm->fbc_val);
	}
}

/*
 * Merge all low power watermarks for all active pipes.
 */
static void ilk_wm_merge(struct drm_i915_private *dev_priv,
			 const struct intel_wm_config *config,
			 const struct ilk_wm_maximums *max,
			 struct intel_pipe_wm *merged)
{
	int level, num_levels = dev_priv->display.wm.num_levels;
	int last_enabled_level = num_levels - 1;

	/* ILK/SNB/IVB: LP1+ watermarks only w/ single pipe */
	if ((DISPLAY_VER(dev_priv) < 7 || IS_IVYBRIDGE(dev_priv)) &&
	    config->num_pipes_active > 1)
		last_enabled_level = 0;

	/* ILK: FBC WM must be disabled always */
	merged->fbc_wm_enabled = DISPLAY_VER(dev_priv) >= 6;

	/* merge each WM1+ level */
	for (level = 1; level < num_levels; level++) {
		struct intel_wm_level *wm = &merged->wm[level];

		ilk_merge_wm_level(dev_priv, level, wm);

		if (level > last_enabled_level)
			wm->enable = false;
		else if (!ilk_validate_wm_level(dev_priv, level, max, wm))
			/* make sure all following levels get disabled */
			last_enabled_level = level - 1;

		/*
		 * The spec says it is preferred to disable
		 * FBC WMs instead of disabling a WM level.
		 */
		if (wm->fbc_val > max->fbc) {
			if (wm->enable)
				merged->fbc_wm_enabled = false;
			wm->fbc_val = 0;
		}
	}

	/* ILK: LP2+ must be disabled when FBC WM is disabled but FBC enabled */
	if (DISPLAY_VER(dev_priv) == 5 && HAS_FBC(dev_priv) &&
	    dev_priv->display.params.enable_fbc && !merged->fbc_wm_enabled) {
		for (level = 2; level < num_levels; level++) {
			struct intel_wm_level *wm = &merged->wm[level];

			wm->enable = false;
		}
	}
}

static int ilk_wm_lp_to_level(int wm_lp, const struct intel_pipe_wm *pipe_wm)
{
	/* LP1,LP2,LP3 levels are either 1,2,3 or 1,3,4 */
	return wm_lp + (wm_lp >= 2 && pipe_wm->wm[4].enable);
}

/* The value we need to program into the WM_LPx latency field */
static unsigned int ilk_wm_lp_latency(struct drm_i915_private *dev_priv,
				      int level)
{
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		return 2 * level;
	else
		return dev_priv->display.wm.pri_latency[level];
}

static void ilk_compute_wm_results(struct drm_i915_private *dev_priv,
				   const struct intel_pipe_wm *merged,
				   enum intel_ddb_partitioning partitioning,
				   struct ilk_wm_values *results)
{
	struct intel_crtc *crtc;
	int level, wm_lp;

	results->enable_fbc_wm = merged->fbc_wm_enabled;
	results->partitioning = partitioning;

	/* LP1+ register values */
	for (wm_lp = 1; wm_lp <= 3; wm_lp++) {
		const struct intel_wm_level *r;

		level = ilk_wm_lp_to_level(wm_lp, merged);

		r = &merged->wm[level];

		/*
		 * Maintain the watermark values even if the level is
		 * disabled. Doing otherwise could cause underruns.
		 */
		results->wm_lp[wm_lp - 1] =
			WM_LP_LATENCY(ilk_wm_lp_latency(dev_priv, level)) |
			WM_LP_PRIMARY(r->pri_val) |
			WM_LP_CURSOR(r->cur_val);

		if (r->enable)
			results->wm_lp[wm_lp - 1] |= WM_LP_ENABLE;

		if (DISPLAY_VER(dev_priv) >= 8)
			results->wm_lp[wm_lp - 1] |= WM_LP_FBC_BDW(r->fbc_val);
		else
			results->wm_lp[wm_lp - 1] |= WM_LP_FBC_ILK(r->fbc_val);

		results->wm_lp_spr[wm_lp - 1] = WM_LP_SPRITE(r->spr_val);

		/*
		 * Always set WM_LP_SPRITE_EN when spr_val != 0, even if the
		 * level is disabled. Doing otherwise could cause underruns.
		 */
		if (DISPLAY_VER(dev_priv) < 7 && r->spr_val) {
			drm_WARN_ON(&dev_priv->drm, wm_lp != 1);
			results->wm_lp_spr[wm_lp - 1] |= WM_LP_SPRITE_ENABLE;
		}
	}

	/* LP0 register values */
	for_each_intel_crtc(&dev_priv->drm, crtc) {
		enum pipe pipe = crtc->pipe;
		const struct intel_pipe_wm *pipe_wm = &crtc->wm.active.ilk;
		const struct intel_wm_level *r = &pipe_wm->wm[0];

		if (drm_WARN_ON(&dev_priv->drm, !r->enable))
			continue;

		results->wm_pipe[pipe] =
			WM0_PIPE_PRIMARY(r->pri_val) |
			WM0_PIPE_SPRITE(r->spr_val) |
			WM0_PIPE_CURSOR(r->cur_val);
	}
}

/*
 * Find the result with the highest level enabled. Check for enable_fbc_wm in
 * case both are at the same level. Prefer r1 in case they're the same.
 */
static struct intel_pipe_wm *
ilk_find_best_result(struct drm_i915_private *dev_priv,
		     struct intel_pipe_wm *r1,
		     struct intel_pipe_wm *r2)
{
	int level, level1 = 0, level2 = 0;

	for (level = 1; level < dev_priv->display.wm.num_levels; level++) {
		if (r1->wm[level].enable)
			level1 = level;
		if (r2->wm[level].enable)
			level2 = level;
	}

	if (level1 == level2) {
		if (r2->fbc_wm_enabled && !r1->fbc_wm_enabled)
			return r2;
		else
			return r1;
	} else if (level1 > level2) {
		return r1;
	} else {
		return r2;
	}
}

/* dirty bits used to track which watermarks need changes */
#define WM_DIRTY_PIPE(pipe) (1 << (pipe))
#define WM_DIRTY_LP(wm_lp) (1 << (15 + (wm_lp)))
#define WM_DIRTY_LP_ALL (WM_DIRTY_LP(1) | WM_DIRTY_LP(2) | WM_DIRTY_LP(3))
#define WM_DIRTY_FBC (1 << 24)
#define WM_DIRTY_DDB (1 << 25)

static unsigned int ilk_compute_wm_dirty(struct drm_i915_private *dev_priv,
					 const struct ilk_wm_values *old,
					 const struct ilk_wm_values *new)
{
	unsigned int dirty = 0;
	enum pipe pipe;
	int wm_lp;

	for_each_pipe(dev_priv, pipe) {
		if (old->wm_pipe[pipe] != new->wm_pipe[pipe]) {
			dirty |= WM_DIRTY_PIPE(pipe);
			/* Must disable LP1+ watermarks too */
			dirty |= WM_DIRTY_LP_ALL;
		}
	}

	if (old->enable_fbc_wm != new->enable_fbc_wm) {
		dirty |= WM_DIRTY_FBC;
		/* Must disable LP1+ watermarks too */
		dirty |= WM_DIRTY_LP_ALL;
	}

	if (old->partitioning != new->partitioning) {
		dirty |= WM_DIRTY_DDB;
		/* Must disable LP1+ watermarks too */
		dirty |= WM_DIRTY_LP_ALL;
	}

	/* LP1+ watermarks already deemed dirty, no need to continue */
	if (dirty & WM_DIRTY_LP_ALL)
		return dirty;

	/* Find the lowest numbered LP1+ watermark in need of an update... */
	for (wm_lp = 1; wm_lp <= 3; wm_lp++) {
		if (old->wm_lp[wm_lp - 1] != new->wm_lp[wm_lp - 1] ||
		    old->wm_lp_spr[wm_lp - 1] != new->wm_lp_spr[wm_lp - 1])
			break;
	}

	/* ...and mark it and all higher numbered LP1+ watermarks as dirty */
	for (; wm_lp <= 3; wm_lp++)
		dirty |= WM_DIRTY_LP(wm_lp);

	return dirty;
}

static bool _ilk_disable_lp_wm(struct drm_i915_private *dev_priv,
			       unsigned int dirty)
{
	struct ilk_wm_values *previous = &dev_priv->display.wm.hw;
	bool changed = false;

	if (dirty & WM_DIRTY_LP(3) && previous->wm_lp[2] & WM_LP_ENABLE) {
		previous->wm_lp[2] &= ~WM_LP_ENABLE;
		intel_uncore_write(&dev_priv->uncore, WM3_LP_ILK, previous->wm_lp[2]);
		changed = true;
	}
	if (dirty & WM_DIRTY_LP(2) && previous->wm_lp[1] & WM_LP_ENABLE) {
		previous->wm_lp[1] &= ~WM_LP_ENABLE;
		intel_uncore_write(&dev_priv->uncore, WM2_LP_ILK, previous->wm_lp[1]);
		changed = true;
	}
	if (dirty & WM_DIRTY_LP(1) && previous->wm_lp[0] & WM_LP_ENABLE) {
		previous->wm_lp[0] &= ~WM_LP_ENABLE;
		intel_uncore_write(&dev_priv->uncore, WM1_LP_ILK, previous->wm_lp[0]);
		changed = true;
	}

	/*
	 * Don't touch WM_LP_SPRITE_ENABLE here.
	 * Doing so could cause underruns.
	 */

	return changed;
}

/*
 * The spec says we shouldn't write when we don't need, because every write
 * causes WMs to be re-evaluated, expending some power.
 */
static void ilk_write_wm_values(struct drm_i915_private *dev_priv,
				struct ilk_wm_values *results)
{
	struct ilk_wm_values *previous = &dev_priv->display.wm.hw;
	unsigned int dirty;

	dirty = ilk_compute_wm_dirty(dev_priv, previous, results);
	if (!dirty)
		return;

	_ilk_disable_lp_wm(dev_priv, dirty);

	if (dirty & WM_DIRTY_PIPE(PIPE_A))
		intel_uncore_write(&dev_priv->uncore, WM0_PIPE_ILK(PIPE_A), results->wm_pipe[0]);
	if (dirty & WM_DIRTY_PIPE(PIPE_B))
		intel_uncore_write(&dev_priv->uncore, WM0_PIPE_ILK(PIPE_B), results->wm_pipe[1]);
	if (dirty & WM_DIRTY_PIPE(PIPE_C))
		intel_uncore_write(&dev_priv->uncore, WM0_PIPE_ILK(PIPE_C), results->wm_pipe[2]);

	if (dirty & WM_DIRTY_DDB) {
		if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
			intel_uncore_rmw(&dev_priv->uncore, WM_MISC, WM_MISC_DATA_PARTITION_5_6,
					 results->partitioning == INTEL_DDB_PART_1_2 ? 0 :
					 WM_MISC_DATA_PARTITION_5_6);
		else
			intel_uncore_rmw(&dev_priv->uncore, DISP_ARB_CTL2, DISP_DATA_PARTITION_5_6,
					 results->partitioning == INTEL_DDB_PART_1_2 ? 0 :
					 DISP_DATA_PARTITION_5_6);
	}

	if (dirty & WM_DIRTY_FBC)
		intel_uncore_rmw(&dev_priv->uncore, DISP_ARB_CTL, DISP_FBC_WM_DIS,
				 results->enable_fbc_wm ? 0 : DISP_FBC_WM_DIS);

	if (dirty & WM_DIRTY_LP(1) &&
	    previous->wm_lp_spr[0] != results->wm_lp_spr[0])
		intel_uncore_write(&dev_priv->uncore, WM1S_LP_ILK, results->wm_lp_spr[0]);

	if (DISPLAY_VER(dev_priv) >= 7) {
		if (dirty & WM_DIRTY_LP(2) && previous->wm_lp_spr[1] != results->wm_lp_spr[1])
			intel_uncore_write(&dev_priv->uncore, WM2S_LP_IVB, results->wm_lp_spr[1]);
		if (dirty & WM_DIRTY_LP(3) && previous->wm_lp_spr[2] != results->wm_lp_spr[2])
			intel_uncore_write(&dev_priv->uncore, WM3S_LP_IVB, results->wm_lp_spr[2]);
	}

	if (dirty & WM_DIRTY_LP(1) && previous->wm_lp[0] != results->wm_lp[0])
		intel_uncore_write(&dev_priv->uncore, WM1_LP_ILK, results->wm_lp[0]);
	if (dirty & WM_DIRTY_LP(2) && previous->wm_lp[1] != results->wm_lp[1])
		intel_uncore_write(&dev_priv->uncore, WM2_LP_ILK, results->wm_lp[1]);
	if (dirty & WM_DIRTY_LP(3) && previous->wm_lp[2] != results->wm_lp[2])
		intel_uncore_write(&dev_priv->uncore, WM3_LP_ILK, results->wm_lp[2]);

	dev_priv->display.wm.hw = *results;
}

bool ilk_disable_lp_wm(struct drm_i915_private *dev_priv)
{
	return _ilk_disable_lp_wm(dev_priv, WM_DIRTY_LP_ALL);
}

static void ilk_compute_wm_config(struct drm_i915_private *dev_priv,
				  struct intel_wm_config *config)
{
	struct intel_crtc *crtc;

	/* Compute the currently _active_ config */
	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct intel_pipe_wm *wm = &crtc->wm.active.ilk;

		if (!wm->pipe_enabled)
			continue;

		config->sprites_enabled |= wm->sprites_enabled;
		config->sprites_scaled |= wm->sprites_scaled;
		config->num_pipes_active++;
	}
}

static void ilk_program_watermarks(struct drm_i915_private *dev_priv)
{
	struct intel_pipe_wm lp_wm_1_2 = {}, lp_wm_5_6 = {}, *best_lp_wm;
	struct ilk_wm_maximums max;
	struct intel_wm_config config = {};
	struct ilk_wm_values results = {};
	enum intel_ddb_partitioning partitioning;

	ilk_compute_wm_config(dev_priv, &config);

	ilk_compute_wm_maximums(dev_priv, 1, &config, INTEL_DDB_PART_1_2, &max);
	ilk_wm_merge(dev_priv, &config, &max, &lp_wm_1_2);

	/* 5/6 split only in single pipe config on IVB+ */
	if (DISPLAY_VER(dev_priv) >= 7 &&
	    config.num_pipes_active == 1 && config.sprites_enabled) {
		ilk_compute_wm_maximums(dev_priv, 1, &config, INTEL_DDB_PART_5_6, &max);
		ilk_wm_merge(dev_priv, &config, &max, &lp_wm_5_6);

		best_lp_wm = ilk_find_best_result(dev_priv, &lp_wm_1_2, &lp_wm_5_6);
	} else {
		best_lp_wm = &lp_wm_1_2;
	}

	partitioning = (best_lp_wm == &lp_wm_1_2) ?
		       INTEL_DDB_PART_1_2 : INTEL_DDB_PART_5_6;

	ilk_compute_wm_results(dev_priv, best_lp_wm, partitioning, &results);

	ilk_write_wm_values(dev_priv, &results);
}

static void ilk_initial_watermarks(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	mutex_lock(&dev_priv->display.wm.wm_mutex);
	crtc->wm.active.ilk = crtc_state->wm.ilk.intermediate;
	ilk_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->display.wm.wm_mutex);
}

static void ilk_optimize_watermarks(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!crtc_state->wm.need_postvbl_update)
		return;

	mutex_lock(&dev_priv->display.wm.wm_mutex);
	crtc->wm.active.ilk = crtc_state->wm.ilk.optimal;
	ilk_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->display.wm.wm_mutex);
}

static void ilk_pipe_wm_get_hw_state(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct ilk_wm_values *hw = &dev_priv->display.wm.hw;
	struct intel_crtc_state *crtc_state = to_intel_crtc_state(crtc->base.state);
	struct intel_pipe_wm *active = &crtc_state->wm.ilk.optimal;
	enum pipe pipe = crtc->pipe;

	hw->wm_pipe[pipe] = intel_uncore_read(&dev_priv->uncore, WM0_PIPE_ILK(pipe));

	memset(active, 0, sizeof(*active));

	active->pipe_enabled = crtc->active;

	if (active->pipe_enabled) {
		u32 tmp = hw->wm_pipe[pipe];

		/*
		 * For active pipes LP0 watermark is marked as
		 * enabled, and LP1+ watermaks as disabled since
		 * we can't really reverse compute them in case
		 * multiple pipes are active.
		 */
		active->wm[0].enable = true;
		active->wm[0].pri_val = REG_FIELD_GET(WM0_PIPE_PRIMARY_MASK, tmp);
		active->wm[0].spr_val = REG_FIELD_GET(WM0_PIPE_SPRITE_MASK, tmp);
		active->wm[0].cur_val = REG_FIELD_GET(WM0_PIPE_CURSOR_MASK, tmp);
	} else {
		int level;

		/*
		 * For inactive pipes, all watermark levels
		 * should be marked as enabled but zeroed,
		 * which is what we'd compute them to.
		 */
		for (level = 0; level < dev_priv->display.wm.num_levels; level++)
			active->wm[level].enable = true;
	}

	crtc->wm.active.ilk = *active;
}

static int ilk_sanitize_watermarks_add_affected(struct drm_atomic_state *state)
{
	struct drm_plane *plane;
	struct intel_crtc *crtc;

	for_each_intel_crtc(state->dev, crtc) {
		struct intel_crtc_state *crtc_state;

		crtc_state = intel_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (crtc_state->hw.active) {
			/*
			 * Preserve the inherited flag to avoid
			 * taking the full modeset path.
			 */
			crtc_state->inherited = true;
		}
	}

	drm_for_each_plane(plane, state->dev) {
		struct drm_plane_state *plane_state;

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state))
			return PTR_ERR(plane_state);
	}

	return 0;
}

/*
 * Calculate what we think the watermarks should be for the state we've read
 * out of the hardware and then immediately program those watermarks so that
 * we ensure the hardware settings match our internal state.
 *
 * We can calculate what we think WM's should be by creating a duplicate of the
 * current state (which was constructed during hardware readout) and running it
 * through the atomic check code to calculate new watermark values in the
 * state object.
 */
void ilk_wm_sanitize(struct drm_i915_private *dev_priv)
{
	struct drm_atomic_state *state;
	struct intel_atomic_state *intel_state;
	struct intel_crtc *crtc;
	struct intel_crtc_state *crtc_state;
	struct drm_modeset_acquire_ctx ctx;
	int ret;
	int i;

	/* Only supported on platforms that use atomic watermark design */
	if (!dev_priv->display.funcs.wm->optimize_watermarks)
		return;

	if (drm_WARN_ON(&dev_priv->drm, DISPLAY_VER(dev_priv) >= 9))
		return;

	state = drm_atomic_state_alloc(&dev_priv->drm);
	if (drm_WARN_ON(&dev_priv->drm, !state))
		return;

	intel_state = to_intel_atomic_state(state);

	drm_modeset_acquire_init(&ctx, 0);

	state->acquire_ctx = &ctx;
	to_intel_atomic_state(state)->internal = true;

retry:
	/*
	 * Hardware readout is the only time we don't want to calculate
	 * intermediate watermarks (since we don't trust the current
	 * watermarks).
	 */
	if (!HAS_GMCH(dev_priv))
		intel_state->skip_intermediate_wm = true;

	ret = ilk_sanitize_watermarks_add_affected(state);
	if (ret)
		goto fail;

	ret = intel_atomic_check(&dev_priv->drm, state);
	if (ret)
		goto fail;

	/* Write calculated watermark values back */
	for_each_new_intel_crtc_in_state(intel_state, crtc, crtc_state, i) {
		crtc_state->wm.need_postvbl_update = true;
		intel_optimize_watermarks(intel_state, crtc);

		to_intel_crtc_state(crtc->base.state)->wm = crtc_state->wm;
	}

fail:
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	/*
	 * If we fail here, it means that the hardware appears to be
	 * programmed in a way that shouldn't be possible, given our
	 * understanding of watermark requirements.  This might mean a
	 * mistake in the hardware readout code or a mistake in the
	 * watermark calculations for a given platform.  Raise a WARN
	 * so that this is noticeable.
	 *
	 * If this actually happens, we'll have to just leave the
	 * BIOS-programmed watermarks untouched and hope for the best.
	 */
	drm_WARN(&dev_priv->drm, ret,
		 "Could not determine valid watermarks for inherited state\n");

	drm_atomic_state_put(state);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

#define _FW_WM(value, plane) \
	(((value) & DSPFW_ ## plane ## _MASK) >> DSPFW_ ## plane ## _SHIFT)
#define _FW_WM_VLV(value, plane) \
	(((value) & DSPFW_ ## plane ## _MASK_VLV) >> DSPFW_ ## plane ## _SHIFT)

static void g4x_read_wm_values(struct drm_i915_private *dev_priv,
			       struct g4x_wm_values *wm)
{
	u32 tmp;

	tmp = intel_uncore_read(&dev_priv->uncore, DSPFW1(dev_priv));
	wm->sr.plane = _FW_WM(tmp, SR);
	wm->pipe[PIPE_B].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORB);
	wm->pipe[PIPE_B].plane[PLANE_PRIMARY] = _FW_WM(tmp, PLANEB);
	wm->pipe[PIPE_A].plane[PLANE_PRIMARY] = _FW_WM(tmp, PLANEA);

	tmp = intel_uncore_read(&dev_priv->uncore, DSPFW2(dev_priv));
	wm->fbc_en = tmp & DSPFW_FBC_SR_EN;
	wm->sr.fbc = _FW_WM(tmp, FBC_SR);
	wm->hpll.fbc = _FW_WM(tmp, FBC_HPLL_SR);
	wm->pipe[PIPE_B].plane[PLANE_SPRITE0] = _FW_WM(tmp, SPRITEB);
	wm->pipe[PIPE_A].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORA);
	wm->pipe[PIPE_A].plane[PLANE_SPRITE0] = _FW_WM(tmp, SPRITEA);

	tmp = intel_uncore_read(&dev_priv->uncore, DSPFW3(dev_priv));
	wm->hpll_en = tmp & DSPFW_HPLL_SR_EN;
	wm->sr.cursor = _FW_WM(tmp, CURSOR_SR);
	wm->hpll.cursor = _FW_WM(tmp, HPLL_CURSOR);
	wm->hpll.plane = _FW_WM(tmp, HPLL_SR);
}

static void vlv_read_wm_values(struct drm_i915_private *dev_priv,
			       struct vlv_wm_values *wm)
{
	enum pipe pipe;
	u32 tmp;

	for_each_pipe(dev_priv, pipe) {
		tmp = intel_uncore_read(&dev_priv->uncore, VLV_DDL(pipe));

		wm->ddl[pipe].plane[PLANE_PRIMARY] =
			(tmp >> DDL_PLANE_SHIFT) & (DDL_PRECISION_HIGH | DRAIN_LATENCY_MASK);
		wm->ddl[pipe].plane[PLANE_CURSOR] =
			(tmp >> DDL_CURSOR_SHIFT) & (DDL_PRECISION_HIGH | DRAIN_LATENCY_MASK);
		wm->ddl[pipe].plane[PLANE_SPRITE0] =
			(tmp >> DDL_SPRITE_SHIFT(0)) & (DDL_PRECISION_HIGH | DRAIN_LATENCY_MASK);
		wm->ddl[pipe].plane[PLANE_SPRITE1] =
			(tmp >> DDL_SPRITE_SHIFT(1)) & (DDL_PRECISION_HIGH | DRAIN_LATENCY_MASK);
	}

	tmp = intel_uncore_read(&dev_priv->uncore, DSPFW1(dev_priv));
	wm->sr.plane = _FW_WM(tmp, SR);
	wm->pipe[PIPE_B].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORB);
	wm->pipe[PIPE_B].plane[PLANE_PRIMARY] = _FW_WM_VLV(tmp, PLANEB);
	wm->pipe[PIPE_A].plane[PLANE_PRIMARY] = _FW_WM_VLV(tmp, PLANEA);

	tmp = intel_uncore_read(&dev_priv->uncore, DSPFW2(dev_priv));
	wm->pipe[PIPE_A].plane[PLANE_SPRITE1] = _FW_WM_VLV(tmp, SPRITEB);
	wm->pipe[PIPE_A].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORA);
	wm->pipe[PIPE_A].plane[PLANE_SPRITE0] = _FW_WM_VLV(tmp, SPRITEA);

	tmp = intel_uncore_read(&dev_priv->uncore, DSPFW3(dev_priv));
	wm->sr.cursor = _FW_WM(tmp, CURSOR_SR);

	if (IS_CHERRYVIEW(dev_priv)) {
		tmp = intel_uncore_read(&dev_priv->uncore, DSPFW7_CHV);
		wm->pipe[PIPE_B].plane[PLANE_SPRITE1] = _FW_WM_VLV(tmp, SPRITED);
		wm->pipe[PIPE_B].plane[PLANE_SPRITE0] = _FW_WM_VLV(tmp, SPRITEC);

		tmp = intel_uncore_read(&dev_priv->uncore, DSPFW8_CHV);
		wm->pipe[PIPE_C].plane[PLANE_SPRITE1] = _FW_WM_VLV(tmp, SPRITEF);
		wm->pipe[PIPE_C].plane[PLANE_SPRITE0] = _FW_WM_VLV(tmp, SPRITEE);

		tmp = intel_uncore_read(&dev_priv->uncore, DSPFW9_CHV);
		wm->pipe[PIPE_C].plane[PLANE_PRIMARY] = _FW_WM_VLV(tmp, PLANEC);
		wm->pipe[PIPE_C].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORC);

		tmp = intel_uncore_read(&dev_priv->uncore, DSPHOWM);
		wm->sr.plane |= _FW_WM(tmp, SR_HI) << 9;
		wm->pipe[PIPE_C].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITEF_HI) << 8;
		wm->pipe[PIPE_C].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEE_HI) << 8;
		wm->pipe[PIPE_C].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEC_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITED_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEC_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEB_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITEB_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEA_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEA_HI) << 8;
	} else {
		tmp = intel_uncore_read(&dev_priv->uncore, DSPFW7);
		wm->pipe[PIPE_B].plane[PLANE_SPRITE1] = _FW_WM_VLV(tmp, SPRITED);
		wm->pipe[PIPE_B].plane[PLANE_SPRITE0] = _FW_WM_VLV(tmp, SPRITEC);

		tmp = intel_uncore_read(&dev_priv->uncore, DSPHOWM);
		wm->sr.plane |= _FW_WM(tmp, SR_HI) << 9;
		wm->pipe[PIPE_B].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITED_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEC_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEB_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITEB_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEA_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEA_HI) << 8;
	}
}

#undef _FW_WM
#undef _FW_WM_VLV

static void g4x_wm_get_hw_state(struct drm_i915_private *dev_priv)
{
	struct g4x_wm_values *wm = &dev_priv->display.wm.g4x;
	struct intel_crtc *crtc;

	g4x_read_wm_values(dev_priv, wm);

	wm->cxsr = intel_uncore_read(&dev_priv->uncore, FW_BLC_SELF) & FW_BLC_SELF_EN;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct g4x_wm_state *active = &crtc->wm.active.g4x;
		struct g4x_pipe_wm *raw;
		enum pipe pipe = crtc->pipe;
		enum plane_id plane_id;
		int level, max_level;

		active->cxsr = wm->cxsr;
		active->hpll_en = wm->hpll_en;
		active->fbc_en = wm->fbc_en;

		active->sr = wm->sr;
		active->hpll = wm->hpll;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			active->wm.plane[plane_id] =
				wm->pipe[pipe].plane[plane_id];
		}

		if (wm->cxsr && wm->hpll_en)
			max_level = G4X_WM_LEVEL_HPLL;
		else if (wm->cxsr)
			max_level = G4X_WM_LEVEL_SR;
		else
			max_level = G4X_WM_LEVEL_NORMAL;

		level = G4X_WM_LEVEL_NORMAL;
		raw = &crtc_state->wm.g4x.raw[level];
		for_each_plane_id_on_crtc(crtc, plane_id)
			raw->plane[plane_id] = active->wm.plane[plane_id];

		level = G4X_WM_LEVEL_SR;
		if (level > max_level)
			goto out;

		raw = &crtc_state->wm.g4x.raw[level];
		raw->plane[PLANE_PRIMARY] = active->sr.plane;
		raw->plane[PLANE_CURSOR] = active->sr.cursor;
		raw->plane[PLANE_SPRITE0] = 0;
		raw->fbc = active->sr.fbc;

		level = G4X_WM_LEVEL_HPLL;
		if (level > max_level)
			goto out;

		raw = &crtc_state->wm.g4x.raw[level];
		raw->plane[PLANE_PRIMARY] = active->hpll.plane;
		raw->plane[PLANE_CURSOR] = active->hpll.cursor;
		raw->plane[PLANE_SPRITE0] = 0;
		raw->fbc = active->hpll.fbc;

		level++;
	out:
		for_each_plane_id_on_crtc(crtc, plane_id)
			g4x_raw_plane_wm_set(crtc_state, level,
					     plane_id, USHRT_MAX);
		g4x_raw_fbc_wm_set(crtc_state, level, USHRT_MAX);

		g4x_invalidate_wms(crtc, active, level);

		crtc_state->wm.g4x.optimal = *active;
		crtc_state->wm.g4x.intermediate = *active;

		drm_dbg_kms(&dev_priv->drm,
			    "Initial watermarks: pipe %c, plane=%d, cursor=%d, sprite=%d\n",
			    pipe_name(pipe),
			    wm->pipe[pipe].plane[PLANE_PRIMARY],
			    wm->pipe[pipe].plane[PLANE_CURSOR],
			    wm->pipe[pipe].plane[PLANE_SPRITE0]);
	}

	drm_dbg_kms(&dev_priv->drm,
		    "Initial SR watermarks: plane=%d, cursor=%d fbc=%d\n",
		    wm->sr.plane, wm->sr.cursor, wm->sr.fbc);
	drm_dbg_kms(&dev_priv->drm,
		    "Initial HPLL watermarks: plane=%d, SR cursor=%d fbc=%d\n",
		    wm->hpll.plane, wm->hpll.cursor, wm->hpll.fbc);
	drm_dbg_kms(&dev_priv->drm, "Initial SR=%s HPLL=%s FBC=%s\n",
		    str_yes_no(wm->cxsr), str_yes_no(wm->hpll_en),
		    str_yes_no(wm->fbc_en));
}

static void g4x_wm_sanitize(struct drm_i915_private *dev_priv)
{
	struct intel_plane *plane;
	struct intel_crtc *crtc;

	mutex_lock(&dev_priv->display.wm.wm_mutex);

	for_each_intel_plane(&dev_priv->drm, plane) {
		struct intel_crtc *crtc =
			intel_crtc_for_pipe(dev_priv, plane->pipe);
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		enum plane_id plane_id = plane->id;
		int level;

		if (plane_state->uapi.visible)
			continue;

		for (level = 0; level < dev_priv->display.wm.num_levels; level++) {
			struct g4x_pipe_wm *raw =
				&crtc_state->wm.g4x.raw[level];

			raw->plane[plane_id] = 0;

			if (plane_id == PLANE_PRIMARY)
				raw->fbc = 0;
		}
	}

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		int ret;

		ret = _g4x_compute_pipe_wm(crtc_state);
		drm_WARN_ON(&dev_priv->drm, ret);

		crtc_state->wm.g4x.intermediate =
			crtc_state->wm.g4x.optimal;
		crtc->wm.active.g4x = crtc_state->wm.g4x.optimal;
	}

	g4x_program_watermarks(dev_priv);

	mutex_unlock(&dev_priv->display.wm.wm_mutex);
}

static void g4x_wm_get_hw_state_and_sanitize(struct drm_i915_private *i915)
{
	g4x_wm_get_hw_state(i915);
	g4x_wm_sanitize(i915);
}

static void vlv_wm_get_hw_state(struct drm_i915_private *dev_priv)
{
	struct vlv_wm_values *wm = &dev_priv->display.wm.vlv;
	struct intel_crtc *crtc;
	u32 val;

	vlv_read_wm_values(dev_priv, wm);

	wm->cxsr = intel_uncore_read(&dev_priv->uncore, FW_BLC_SELF_VLV) & FW_CSPWRDWNEN;
	wm->level = VLV_WM_LEVEL_PM2;

	if (IS_CHERRYVIEW(dev_priv)) {
		vlv_punit_get(dev_priv);

		val = vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM);
		if (val & DSP_MAXFIFO_PM5_ENABLE)
			wm->level = VLV_WM_LEVEL_PM5;

		/*
		 * If DDR DVFS is disabled in the BIOS, Punit
		 * will never ack the request. So if that happens
		 * assume we don't have to enable/disable DDR DVFS
		 * dynamically. To test that just set the REQ_ACK
		 * bit to poke the Punit, but don't change the
		 * HIGH/LOW bits so that we don't actually change
		 * the current state.
		 */
		val = vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2);
		val |= FORCE_DDR_FREQ_REQ_ACK;
		vlv_punit_write(dev_priv, PUNIT_REG_DDR_SETUP2, val);

		if (wait_for((vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2) &
			      FORCE_DDR_FREQ_REQ_ACK) == 0, 3)) {
			drm_dbg_kms(&dev_priv->drm,
				    "Punit not acking DDR DVFS request, "
				    "assuming DDR DVFS is disabled\n");
			dev_priv->display.wm.num_levels = VLV_WM_LEVEL_PM5 + 1;
		} else {
			val = vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2);
			if ((val & FORCE_DDR_HIGH_FREQ) == 0)
				wm->level = VLV_WM_LEVEL_DDR_DVFS;
		}

		vlv_punit_put(dev_priv);
	}

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct vlv_wm_state *active = &crtc->wm.active.vlv;
		const struct vlv_fifo_state *fifo_state =
			&crtc_state->wm.vlv.fifo_state;
		enum pipe pipe = crtc->pipe;
		enum plane_id plane_id;
		int level;

		vlv_get_fifo_size(crtc_state);

		active->num_levels = wm->level + 1;
		active->cxsr = wm->cxsr;

		for (level = 0; level < active->num_levels; level++) {
			struct g4x_pipe_wm *raw =
				&crtc_state->wm.vlv.raw[level];

			active->sr[level].plane = wm->sr.plane;
			active->sr[level].cursor = wm->sr.cursor;

			for_each_plane_id_on_crtc(crtc, plane_id) {
				active->wm[level].plane[plane_id] =
					wm->pipe[pipe].plane[plane_id];

				raw->plane[plane_id] =
					vlv_invert_wm_value(active->wm[level].plane[plane_id],
							    fifo_state->plane[plane_id]);
			}
		}

		for_each_plane_id_on_crtc(crtc, plane_id)
			vlv_raw_plane_wm_set(crtc_state, level,
					     plane_id, USHRT_MAX);
		vlv_invalidate_wms(crtc, active, level);

		crtc_state->wm.vlv.optimal = *active;
		crtc_state->wm.vlv.intermediate = *active;

		drm_dbg_kms(&dev_priv->drm,
			    "Initial watermarks: pipe %c, plane=%d, cursor=%d, sprite0=%d, sprite1=%d\n",
			    pipe_name(pipe),
			    wm->pipe[pipe].plane[PLANE_PRIMARY],
			    wm->pipe[pipe].plane[PLANE_CURSOR],
			    wm->pipe[pipe].plane[PLANE_SPRITE0],
			    wm->pipe[pipe].plane[PLANE_SPRITE1]);
	}

	drm_dbg_kms(&dev_priv->drm,
		    "Initial watermarks: SR plane=%d, SR cursor=%d level=%d cxsr=%d\n",
		    wm->sr.plane, wm->sr.cursor, wm->level, wm->cxsr);
}

static void vlv_wm_sanitize(struct drm_i915_private *dev_priv)
{
	struct intel_plane *plane;
	struct intel_crtc *crtc;

	mutex_lock(&dev_priv->display.wm.wm_mutex);

	for_each_intel_plane(&dev_priv->drm, plane) {
		struct intel_crtc *crtc =
			intel_crtc_for_pipe(dev_priv, plane->pipe);
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		enum plane_id plane_id = plane->id;
		int level;

		if (plane_state->uapi.visible)
			continue;

		for (level = 0; level < dev_priv->display.wm.num_levels; level++) {
			struct g4x_pipe_wm *raw =
				&crtc_state->wm.vlv.raw[level];

			raw->plane[plane_id] = 0;
		}
	}

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		int ret;

		ret = _vlv_compute_pipe_wm(crtc_state);
		drm_WARN_ON(&dev_priv->drm, ret);

		crtc_state->wm.vlv.intermediate =
			crtc_state->wm.vlv.optimal;
		crtc->wm.active.vlv = crtc_state->wm.vlv.optimal;
	}

	vlv_program_watermarks(dev_priv);

	mutex_unlock(&dev_priv->display.wm.wm_mutex);
}

static void vlv_wm_get_hw_state_and_sanitize(struct drm_i915_private *i915)
{
	vlv_wm_get_hw_state(i915);
	vlv_wm_sanitize(i915);
}

/*
 * FIXME should probably kill this and improve
 * the real watermark readout/sanitation instead
 */
static void ilk_init_lp_watermarks(struct drm_i915_private *dev_priv)
{
	intel_uncore_rmw(&dev_priv->uncore, WM3_LP_ILK, WM_LP_ENABLE, 0);
	intel_uncore_rmw(&dev_priv->uncore, WM2_LP_ILK, WM_LP_ENABLE, 0);
	intel_uncore_rmw(&dev_priv->uncore, WM1_LP_ILK, WM_LP_ENABLE, 0);

	/*
	 * Don't touch WM_LP_SPRITE_ENABLE here.
	 * Doing so could cause underruns.
	 */
}

static void ilk_wm_get_hw_state(struct drm_i915_private *dev_priv)
{
	struct ilk_wm_values *hw = &dev_priv->display.wm.hw;
	struct intel_crtc *crtc;

	ilk_init_lp_watermarks(dev_priv);

	for_each_intel_crtc(&dev_priv->drm, crtc)
		ilk_pipe_wm_get_hw_state(crtc);

	hw->wm_lp[0] = intel_uncore_read(&dev_priv->uncore, WM1_LP_ILK);
	hw->wm_lp[1] = intel_uncore_read(&dev_priv->uncore, WM2_LP_ILK);
	hw->wm_lp[2] = intel_uncore_read(&dev_priv->uncore, WM3_LP_ILK);

	hw->wm_lp_spr[0] = intel_uncore_read(&dev_priv->uncore, WM1S_LP_ILK);
	if (DISPLAY_VER(dev_priv) >= 7) {
		hw->wm_lp_spr[1] = intel_uncore_read(&dev_priv->uncore, WM2S_LP_IVB);
		hw->wm_lp_spr[2] = intel_uncore_read(&dev_priv->uncore, WM3S_LP_IVB);
	}

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		hw->partitioning = (intel_uncore_read(&dev_priv->uncore, WM_MISC) &
				    WM_MISC_DATA_PARTITION_5_6) ?
			INTEL_DDB_PART_5_6 : INTEL_DDB_PART_1_2;
	else if (IS_IVYBRIDGE(dev_priv))
		hw->partitioning = (intel_uncore_read(&dev_priv->uncore, DISP_ARB_CTL2) &
				    DISP_DATA_PARTITION_5_6) ?
			INTEL_DDB_PART_5_6 : INTEL_DDB_PART_1_2;

	hw->enable_fbc_wm =
		!(intel_uncore_read(&dev_priv->uncore, DISP_ARB_CTL) & DISP_FBC_WM_DIS);
}

static const struct intel_wm_funcs ilk_wm_funcs = {
	.compute_pipe_wm = ilk_compute_pipe_wm,
	.compute_intermediate_wm = ilk_compute_intermediate_wm,
	.initial_watermarks = ilk_initial_watermarks,
	.optimize_watermarks = ilk_optimize_watermarks,
	.get_hw_state = ilk_wm_get_hw_state,
};

static const struct intel_wm_funcs vlv_wm_funcs = {
	.compute_pipe_wm = vlv_compute_pipe_wm,
	.compute_intermediate_wm = vlv_compute_intermediate_wm,
	.initial_watermarks = vlv_initial_watermarks,
	.optimize_watermarks = vlv_optimize_watermarks,
	.atomic_update_watermarks = vlv_atomic_update_fifo,
	.get_hw_state = vlv_wm_get_hw_state_and_sanitize,
};

static const struct intel_wm_funcs g4x_wm_funcs = {
	.compute_pipe_wm = g4x_compute_pipe_wm,
	.compute_intermediate_wm = g4x_compute_intermediate_wm,
	.initial_watermarks = g4x_initial_watermarks,
	.optimize_watermarks = g4x_optimize_watermarks,
	.get_hw_state = g4x_wm_get_hw_state_and_sanitize,
};

static const struct intel_wm_funcs pnv_wm_funcs = {
	.update_wm = pnv_update_wm,
};

static const struct intel_wm_funcs i965_wm_funcs = {
	.update_wm = i965_update_wm,
};

static const struct intel_wm_funcs i9xx_wm_funcs = {
	.update_wm = i9xx_update_wm,
};

static const struct intel_wm_funcs i845_wm_funcs = {
	.update_wm = i845_update_wm,
};

static const struct intel_wm_funcs nop_funcs = {
};

void i9xx_wm_init(struct drm_i915_private *dev_priv)
{
	/* For FIFO watermark updates */
	if (HAS_PCH_SPLIT(dev_priv)) {
		ilk_setup_wm_latency(dev_priv);
		dev_priv->display.funcs.wm = &ilk_wm_funcs;
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		vlv_setup_wm_latency(dev_priv);
		dev_priv->display.funcs.wm = &vlv_wm_funcs;
	} else if (IS_G4X(dev_priv)) {
		g4x_setup_wm_latency(dev_priv);
		dev_priv->display.funcs.wm = &g4x_wm_funcs;
	} else if (IS_PINEVIEW(dev_priv)) {
		if (!pnv_get_cxsr_latency(dev_priv)) {
			drm_info(&dev_priv->drm, "Unknown FSB/MEM, disabling CxSR\n");
			/* Disable CxSR and never update its watermark again */
			intel_set_memory_cxsr(dev_priv, false);
			dev_priv->display.funcs.wm = &nop_funcs;
		} else {
			dev_priv->display.funcs.wm = &pnv_wm_funcs;
		}
	} else if (DISPLAY_VER(dev_priv) == 4) {
		dev_priv->display.funcs.wm = &i965_wm_funcs;
	} else if (DISPLAY_VER(dev_priv) == 3) {
		dev_priv->display.funcs.wm = &i9xx_wm_funcs;
	} else if (DISPLAY_VER(dev_priv) == 2) {
		if (INTEL_NUM_PIPES(dev_priv) == 1)
			dev_priv->display.funcs.wm = &i845_wm_funcs;
		else
			dev_priv->display.funcs.wm = &i9xx_wm_funcs;
	} else {
		drm_err(&dev_priv->drm,
			"unexpected fall-through in %s\n", __func__);
		dev_priv->display.funcs.wm = &nop_funcs;
	}
}
