/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "i9xx_plane_regs.h"
#include "intel_color.h"
#include "intel_color_regs.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dsb.h"

struct intel_color_funcs {
	int (*color_check)(struct intel_atomic_state *state,
			   struct intel_crtc *crtc);
	/*
	 * Program non-arming double buffered color management registers
	 * before vblank evasion. The registers should then latch after
	 * the arming register is written (by color_commit_arm()) during
	 * the next vblank start, alongside any other double buffered
	 * registers involved with the same commit. This hook is optional.
	 */
	void (*color_commit_noarm)(const struct intel_crtc_state *crtc_state);
	/*
	 * Program arming double buffered color management registers
	 * during vblank evasion. The registers (and whatever other registers
	 * they arm that were written by color_commit_noarm) should then latch
	 * during the next vblank start, alongside any other double buffered
	 * registers involved with the same commit.
	 */
	void (*color_commit_arm)(const struct intel_crtc_state *crtc_state);
	/*
	 * Perform any extra tasks needed after all the
	 * double buffered registers have been latched.
	 */
	void (*color_post_update)(const struct intel_crtc_state *crtc_state);
	/*
	 * Load LUTs (and other single buffered color management
	 * registers). Will (hopefully) be called during the vblank
	 * following the latching of any double buffered registers
	 * involved with the same commit.
	 */
	void (*load_luts)(const struct intel_crtc_state *crtc_state);
	/*
	 * Read out the LUTs from the hardware into the software state.
	 * Used by eg. the hardware state checker.
	 */
	void (*read_luts)(struct intel_crtc_state *crtc_state);
	/*
	 * Compare the LUTs
	 */
	bool (*lut_equal)(const struct intel_crtc_state *crtc_state,
			  const struct drm_property_blob *blob1,
			  const struct drm_property_blob *blob2,
			  bool is_pre_csc_lut);
	/*
	 * Read out the CSCs (if any) from the hardware into the
	 * software state. Used by eg. the hardware state checker.
	 */
	void (*read_csc)(struct intel_crtc_state *crtc_state);
	/*
	 * Read config other than LUTs and CSCs, before them. Optional.
	 */
	void (*get_config)(struct intel_crtc_state *crtc_state);
};

#define CTM_COEFF_SIGN	(1ULL << 63)

#define CTM_COEFF_1_0	(1ULL << 32)
#define CTM_COEFF_2_0	(CTM_COEFF_1_0 << 1)
#define CTM_COEFF_4_0	(CTM_COEFF_2_0 << 1)
#define CTM_COEFF_8_0	(CTM_COEFF_4_0 << 1)
#define CTM_COEFF_0_5	(CTM_COEFF_1_0 >> 1)
#define CTM_COEFF_0_25	(CTM_COEFF_0_5 >> 1)
#define CTM_COEFF_0_125	(CTM_COEFF_0_25 >> 1)

#define CTM_COEFF_LIMITED_RANGE ((235ULL - 16ULL) * CTM_COEFF_1_0 / 255)

#define CTM_COEFF_NEGATIVE(coeff)	(((coeff) & CTM_COEFF_SIGN) != 0)
#define CTM_COEFF_ABS(coeff)		((coeff) & (CTM_COEFF_SIGN - 1))

#define LEGACY_LUT_LENGTH		256

/*
 * ILK+ csc matrix:
 *
 * |R/Cr|   | c0 c1 c2 |   ( |R/Cr|   |preoff0| )   |postoff0|
 * |G/Y | = | c3 c4 c5 | x ( |G/Y | + |preoff1| ) + |postoff1|
 * |B/Cb|   | c6 c7 c8 |   ( |B/Cb|   |preoff2| )   |postoff2|
 *
 * ILK/SNB don't have explicit post offsets, and instead
 * CSC_MODE_YUV_TO_RGB and CSC_BLACK_SCREEN_OFFSET are used:
 *  CSC_MODE_YUV_TO_RGB=0 + CSC_BLACK_SCREEN_OFFSET=0 -> 1/2, 0, 1/2
 *  CSC_MODE_YUV_TO_RGB=0 + CSC_BLACK_SCREEN_OFFSET=1 -> 1/2, 1/16, 1/2
 *  CSC_MODE_YUV_TO_RGB=1 + CSC_BLACK_SCREEN_OFFSET=0 -> 0, 0, 0
 *  CSC_MODE_YUV_TO_RGB=1 + CSC_BLACK_SCREEN_OFFSET=1 -> 1/16, 1/16, 1/16
 */

/*
 * Extract the CSC coefficient from a CTM coefficient (in U32.32 fixed point
 * format). This macro takes the coefficient we want transformed and the
 * number of fractional bits.
 *
 * We only have a 9 bits precision window which slides depending on the value
 * of the CTM coefficient and we write the value from bit 3. We also round the
 * value.
 */
#define ILK_CSC_COEFF_FP(coeff, fbits)	\
	(clamp_val(((coeff) >> (32 - (fbits) - 3)) + 4, 0, 0xfff) & 0xff8)

#define ILK_CSC_COEFF_1_0 0x7800
#define ILK_CSC_COEFF_LIMITED_RANGE ((235 - 16) << (12 - 8)) /* exponent 0 */
#define ILK_CSC_POSTOFF_LIMITED_RANGE (16 << (12 - 8))

static const struct intel_csc_matrix ilk_csc_matrix_identity = {
	.preoff = {},
	.coeff = {
		ILK_CSC_COEFF_1_0, 0, 0,
		0, ILK_CSC_COEFF_1_0, 0,
		0, 0, ILK_CSC_COEFF_1_0,
	},
	.postoff = {},
};

/* Full range RGB -> limited range RGB matrix */
static const struct intel_csc_matrix ilk_csc_matrix_limited_range = {
	.preoff = {},
	.coeff = {
		ILK_CSC_COEFF_LIMITED_RANGE, 0, 0,
		0, ILK_CSC_COEFF_LIMITED_RANGE, 0,
		0, 0, ILK_CSC_COEFF_LIMITED_RANGE,
	},
	.postoff = {
		ILK_CSC_POSTOFF_LIMITED_RANGE,
		ILK_CSC_POSTOFF_LIMITED_RANGE,
		ILK_CSC_POSTOFF_LIMITED_RANGE,
	},
};

/* BT.709 full range RGB -> limited range YCbCr matrix */
static const struct intel_csc_matrix ilk_csc_matrix_rgb_to_ycbcr = {
	.preoff = {},
	.coeff = {
		0x1e08, 0x9cc0, 0xb528,
		0x2ba8, 0x09d8, 0x37e8,
		0xbce8, 0x9ad8, 0x1e08,
	},
	.postoff = {
		0x0800, 0x0100, 0x0800,
	},
};

static void intel_csc_clear(struct intel_csc_matrix *csc)
{
	memset(csc, 0, sizeof(*csc));
}

static bool lut_is_legacy(const struct drm_property_blob *lut)
{
	return lut && drm_color_lut_size(lut) == LEGACY_LUT_LENGTH;
}

/*
 * When using limited range, multiply the matrix given by userspace by
 * the matrix that we would use for the limited range.
 */
static u64 *ctm_mult_by_limited(u64 *result, const u64 *input)
{
	int i;

	for (i = 0; i < 9; i++) {
		u64 user_coeff = input[i];
		u32 limited_coeff = CTM_COEFF_LIMITED_RANGE;
		u32 abs_coeff = clamp_val(CTM_COEFF_ABS(user_coeff), 0,
					  CTM_COEFF_4_0 - 1) >> 2;

		/*
		 * By scaling every co-efficient with limited range (16-235)
		 * vs full range (0-255) the final o/p will be scaled down to
		 * fit in the limited range supported by the panel.
		 */
		result[i] = mul_u32_u32(limited_coeff, abs_coeff) >> 30;
		result[i] |= user_coeff & CTM_COEFF_SIGN;
	}

	return result;
}

static void ilk_update_pipe_csc(struct intel_crtc *crtc,
				const struct intel_csc_matrix *csc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	intel_de_write_fw(i915, PIPE_CSC_PREOFF_HI(pipe), csc->preoff[0]);
	intel_de_write_fw(i915, PIPE_CSC_PREOFF_ME(pipe), csc->preoff[1]);
	intel_de_write_fw(i915, PIPE_CSC_PREOFF_LO(pipe), csc->preoff[2]);

	intel_de_write_fw(i915, PIPE_CSC_COEFF_RY_GY(pipe),
			  csc->coeff[0] << 16 | csc->coeff[1]);
	intel_de_write_fw(i915, PIPE_CSC_COEFF_BY(pipe),
			  csc->coeff[2] << 16);

	intel_de_write_fw(i915, PIPE_CSC_COEFF_RU_GU(pipe),
			  csc->coeff[3] << 16 | csc->coeff[4]);
	intel_de_write_fw(i915, PIPE_CSC_COEFF_BU(pipe),
			  csc->coeff[5] << 16);

	intel_de_write_fw(i915, PIPE_CSC_COEFF_RV_GV(pipe),
			  csc->coeff[6] << 16 | csc->coeff[7]);
	intel_de_write_fw(i915, PIPE_CSC_COEFF_BV(pipe),
			  csc->coeff[8] << 16);

	if (DISPLAY_VER(i915) < 7)
		return;

	intel_de_write_fw(i915, PIPE_CSC_POSTOFF_HI(pipe), csc->postoff[0]);
	intel_de_write_fw(i915, PIPE_CSC_POSTOFF_ME(pipe), csc->postoff[1]);
	intel_de_write_fw(i915, PIPE_CSC_POSTOFF_LO(pipe), csc->postoff[2]);
}

static void ilk_read_pipe_csc(struct intel_crtc *crtc,
			      struct intel_csc_matrix *csc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 tmp;

	csc->preoff[0] = intel_de_read_fw(i915, PIPE_CSC_PREOFF_HI(pipe));
	csc->preoff[1] = intel_de_read_fw(i915, PIPE_CSC_PREOFF_ME(pipe));
	csc->preoff[2] = intel_de_read_fw(i915, PIPE_CSC_PREOFF_LO(pipe));

	tmp = intel_de_read_fw(i915, PIPE_CSC_COEFF_RY_GY(pipe));
	csc->coeff[0] = tmp >> 16;
	csc->coeff[1] = tmp & 0xffff;
	tmp = intel_de_read_fw(i915, PIPE_CSC_COEFF_BY(pipe));
	csc->coeff[2] = tmp >> 16;

	tmp = intel_de_read_fw(i915, PIPE_CSC_COEFF_RU_GU(pipe));
	csc->coeff[3] = tmp >> 16;
	csc->coeff[4] = tmp & 0xffff;
	tmp = intel_de_read_fw(i915, PIPE_CSC_COEFF_BU(pipe));
	csc->coeff[5] = tmp >> 16;

	tmp = intel_de_read_fw(i915, PIPE_CSC_COEFF_RV_GV(pipe));
	csc->coeff[6] = tmp >> 16;
	csc->coeff[7] = tmp & 0xffff;
	tmp = intel_de_read_fw(i915, PIPE_CSC_COEFF_BV(pipe));
	csc->coeff[8] = tmp >> 16;

	if (DISPLAY_VER(i915) < 7)
		return;

	csc->postoff[0] = intel_de_read_fw(i915, PIPE_CSC_POSTOFF_HI(pipe));
	csc->postoff[1] = intel_de_read_fw(i915, PIPE_CSC_POSTOFF_ME(pipe));
	csc->postoff[2] = intel_de_read_fw(i915, PIPE_CSC_POSTOFF_LO(pipe));
}

static void ilk_read_csc(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->csc_enable)
		ilk_read_pipe_csc(crtc, &crtc_state->csc);
}

static void skl_read_csc(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	/*
	 * Display WA #1184: skl,glk
	 * Wa_1406463849: icl
	 *
	 * Danger! On SKL-ICL *reads* from the CSC coeff/offset registers
	 * will disarm an already armed CSC double buffer update.
	 * So this must not be called while armed. Fortunately the state checker
	 * readout happens only after the update has been already been latched.
	 *
	 * On earlier and later platforms only writes to said registers will
	 * disarm the update. This is considered normal behavior and also
	 * happens with various other hardware units.
	 */
	if (crtc_state->csc_enable)
		ilk_read_pipe_csc(crtc, &crtc_state->csc);
}

static void icl_update_output_csc(struct intel_crtc *crtc,
				  const struct intel_csc_matrix *csc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_PREOFF_HI(pipe), csc->preoff[0]);
	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_PREOFF_ME(pipe), csc->preoff[1]);
	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_PREOFF_LO(pipe), csc->preoff[2]);

	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_COEFF_RY_GY(pipe),
			  csc->coeff[0] << 16 | csc->coeff[1]);
	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_COEFF_BY(pipe),
			  csc->coeff[2] << 16);

	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_COEFF_RU_GU(pipe),
			  csc->coeff[3] << 16 | csc->coeff[4]);
	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_COEFF_BU(pipe),
			  csc->coeff[5] << 16);

	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_COEFF_RV_GV(pipe),
			  csc->coeff[6] << 16 | csc->coeff[7]);
	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_COEFF_BV(pipe),
			  csc->coeff[8] << 16);

	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_POSTOFF_HI(pipe), csc->postoff[0]);
	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_POSTOFF_ME(pipe), csc->postoff[1]);
	intel_de_write_fw(i915, PIPE_CSC_OUTPUT_POSTOFF_LO(pipe), csc->postoff[2]);
}

static void icl_read_output_csc(struct intel_crtc *crtc,
				struct intel_csc_matrix *csc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 tmp;

	csc->preoff[0] = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_PREOFF_HI(pipe));
	csc->preoff[1] = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_PREOFF_ME(pipe));
	csc->preoff[2] = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_PREOFF_LO(pipe));

	tmp = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_COEFF_RY_GY(pipe));
	csc->coeff[0] = tmp >> 16;
	csc->coeff[1] = tmp & 0xffff;
	tmp = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_COEFF_BY(pipe));
	csc->coeff[2] = tmp >> 16;

	tmp = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_COEFF_RU_GU(pipe));
	csc->coeff[3] = tmp >> 16;
	csc->coeff[4] = tmp & 0xffff;
	tmp = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_COEFF_BU(pipe));
	csc->coeff[5] = tmp >> 16;

	tmp = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_COEFF_RV_GV(pipe));
	csc->coeff[6] = tmp >> 16;
	csc->coeff[7] = tmp & 0xffff;
	tmp = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_COEFF_BV(pipe));
	csc->coeff[8] = tmp >> 16;

	csc->postoff[0] = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_POSTOFF_HI(pipe));
	csc->postoff[1] = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_POSTOFF_ME(pipe));
	csc->postoff[2] = intel_de_read_fw(i915, PIPE_CSC_OUTPUT_POSTOFF_LO(pipe));
}

static void icl_read_csc(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	/*
	 * Wa_1406463849: icl
	 *
	 * See skl_read_csc()
	 */
	if (crtc_state->csc_mode & ICL_CSC_ENABLE)
		ilk_read_pipe_csc(crtc, &crtc_state->csc);

	if (crtc_state->csc_mode & ICL_OUTPUT_CSC_ENABLE)
		icl_read_output_csc(crtc, &crtc_state->output_csc);
}

static bool ilk_limited_range(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	/* icl+ have dedicated output CSC */
	if (DISPLAY_VER(i915) >= 11)
		return false;

	/* pre-hsw have TRANSCONF_COLOR_RANGE_SELECT */
	if (DISPLAY_VER(i915) < 7 || IS_IVYBRIDGE(i915))
		return false;

	return crtc_state->limited_color_range;
}

static bool ilk_lut_limited_range(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (!ilk_limited_range(crtc_state))
		return false;

	if (crtc_state->c8_planes)
		return false;

	if (DISPLAY_VER(i915) == 10)
		return crtc_state->hw.gamma_lut;
	else
		return crtc_state->hw.gamma_lut &&
			(crtc_state->hw.degamma_lut || crtc_state->hw.ctm);
}

static bool ilk_csc_limited_range(const struct intel_crtc_state *crtc_state)
{
	if (!ilk_limited_range(crtc_state))
		return false;

	return !ilk_lut_limited_range(crtc_state);
}

static void ilk_csc_copy(struct drm_i915_private *i915,
			 struct intel_csc_matrix *dst,
			 const struct intel_csc_matrix *src)
{
	*dst = *src;

	if (DISPLAY_VER(i915) < 7)
		memset(dst->postoff, 0, sizeof(dst->postoff));
}

static void ilk_csc_convert_ctm(const struct intel_crtc_state *crtc_state,
				struct intel_csc_matrix *csc,
				bool limited_color_range)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct drm_color_ctm *ctm = crtc_state->hw.ctm->data;
	const u64 *input;
	u64 temp[9];
	int i;

	/* for preoff/postoff */
	if (limited_color_range)
		ilk_csc_copy(i915, csc, &ilk_csc_matrix_limited_range);
	else
		ilk_csc_copy(i915, csc, &ilk_csc_matrix_identity);

	if (limited_color_range)
		input = ctm_mult_by_limited(temp, ctm->matrix);
	else
		input = ctm->matrix;

	/*
	 * Convert fixed point S31.32 input to format supported by the
	 * hardware.
	 */
	for (i = 0; i < 9; i++) {
		u64 abs_coeff = ((1ULL << 63) - 1) & input[i];

		/*
		 * Clamp input value to min/max supported by
		 * hardware.
		 */
		abs_coeff = clamp_val(abs_coeff, 0, CTM_COEFF_4_0 - 1);

		csc->coeff[i] = 0;

		/* sign bit */
		if (CTM_COEFF_NEGATIVE(input[i]))
			csc->coeff[i] |= 1 << 15;

		if (abs_coeff < CTM_COEFF_0_125)
			csc->coeff[i] |= (3 << 12) |
				ILK_CSC_COEFF_FP(abs_coeff, 12);
		else if (abs_coeff < CTM_COEFF_0_25)
			csc->coeff[i] |= (2 << 12) |
				ILK_CSC_COEFF_FP(abs_coeff, 11);
		else if (abs_coeff < CTM_COEFF_0_5)
			csc->coeff[i] |= (1 << 12) |
				ILK_CSC_COEFF_FP(abs_coeff, 10);
		else if (abs_coeff < CTM_COEFF_1_0)
			csc->coeff[i] |= ILK_CSC_COEFF_FP(abs_coeff, 9);
		else if (abs_coeff < CTM_COEFF_2_0)
			csc->coeff[i] |= (7 << 12) |
				ILK_CSC_COEFF_FP(abs_coeff, 8);
		else
			csc->coeff[i] |= (6 << 12) |
				ILK_CSC_COEFF_FP(abs_coeff, 7);
	}
}

static void ilk_assign_csc(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	bool limited_color_range = ilk_csc_limited_range(crtc_state);

	if (crtc_state->hw.ctm) {
		drm_WARN_ON(&i915->drm, !crtc_state->csc_enable);

		ilk_csc_convert_ctm(crtc_state, &crtc_state->csc, limited_color_range);
	} else if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB) {
		drm_WARN_ON(&i915->drm, !crtc_state->csc_enable);

		ilk_csc_copy(i915, &crtc_state->csc, &ilk_csc_matrix_rgb_to_ycbcr);
	} else if (limited_color_range) {
		drm_WARN_ON(&i915->drm, !crtc_state->csc_enable);

		ilk_csc_copy(i915, &crtc_state->csc, &ilk_csc_matrix_limited_range);
	} else if (crtc_state->csc_enable) {
		/*
		 * On GLK both pipe CSC and degamma LUT are controlled
		 * by csc_enable. Hence for the cases where the degama
		 * LUT is needed but CSC is not we need to load an
		 * identity matrix.
		 */
		drm_WARN_ON(&i915->drm, !IS_GEMINILAKE(i915));

		ilk_csc_copy(i915, &crtc_state->csc, &ilk_csc_matrix_identity);
	} else {
		intel_csc_clear(&crtc_state->csc);
	}
}

static void ilk_load_csc_matrix(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->csc_enable)
		ilk_update_pipe_csc(crtc, &crtc_state->csc);
}

static void icl_assign_csc(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (crtc_state->hw.ctm) {
		drm_WARN_ON(&i915->drm, (crtc_state->csc_mode & ICL_CSC_ENABLE) == 0);

		ilk_csc_convert_ctm(crtc_state, &crtc_state->csc, false);
	} else {
		drm_WARN_ON(&i915->drm, (crtc_state->csc_mode & ICL_CSC_ENABLE) != 0);

		intel_csc_clear(&crtc_state->csc);
	}

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB) {
		drm_WARN_ON(&i915->drm, (crtc_state->csc_mode & ICL_OUTPUT_CSC_ENABLE) == 0);

		ilk_csc_copy(i915, &crtc_state->output_csc, &ilk_csc_matrix_rgb_to_ycbcr);
	} else if (crtc_state->limited_color_range) {
		drm_WARN_ON(&i915->drm, (crtc_state->csc_mode & ICL_OUTPUT_CSC_ENABLE) == 0);

		ilk_csc_copy(i915, &crtc_state->output_csc, &ilk_csc_matrix_limited_range);
	} else {
		drm_WARN_ON(&i915->drm, (crtc_state->csc_mode & ICL_OUTPUT_CSC_ENABLE) != 0);

		intel_csc_clear(&crtc_state->output_csc);
	}
}

static void icl_load_csc_matrix(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->csc_mode & ICL_CSC_ENABLE)
		ilk_update_pipe_csc(crtc, &crtc_state->csc);

	if (crtc_state->csc_mode & ICL_OUTPUT_CSC_ENABLE)
		icl_update_output_csc(crtc, &crtc_state->output_csc);
}

static u16 ctm_to_twos_complement(u64 coeff, int int_bits, int frac_bits)
{
	s64 c = CTM_COEFF_ABS(coeff);

	/* leave an extra bit for rounding */
	c >>= 32 - frac_bits - 1;

	/* round and drop the extra bit */
	c = (c + 1) >> 1;

	if (CTM_COEFF_NEGATIVE(coeff))
		c = -c;

	c = clamp(c, -(s64)BIT(int_bits + frac_bits - 1),
		  (s64)(BIT(int_bits + frac_bits - 1) - 1));

	return c & (BIT(int_bits + frac_bits) - 1);
}

/*
 * VLV/CHV Wide Gamut Color Correction (WGC) CSC
 * |r|   | c0 c1 c2 |   |r|
 * |g| = | c3 c4 c5 | x |g|
 * |b|   | c6 c7 c8 |   |b|
 *
 * Coefficients are two's complement s2.10.
 */
static void vlv_wgc_csc_convert_ctm(const struct intel_crtc_state *crtc_state,
				    struct intel_csc_matrix *csc)
{
	const struct drm_color_ctm *ctm = crtc_state->hw.ctm->data;
	int i;

	for (i = 0; i < 9; i++)
		csc->coeff[i] = ctm_to_twos_complement(ctm->matrix[i], 2, 10);
}

static void vlv_load_wgc_csc(struct intel_crtc *crtc,
			     const struct intel_csc_matrix *csc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	intel_de_write_fw(dev_priv, PIPE_WGC_C01_C00(dev_priv, pipe),
			  csc->coeff[1] << 16 | csc->coeff[0]);
	intel_de_write_fw(dev_priv, PIPE_WGC_C02(dev_priv, pipe),
			  csc->coeff[2]);

	intel_de_write_fw(dev_priv, PIPE_WGC_C11_C10(dev_priv, pipe),
			  csc->coeff[4] << 16 | csc->coeff[3]);
	intel_de_write_fw(dev_priv, PIPE_WGC_C12(dev_priv, pipe),
			  csc->coeff[5]);

	intel_de_write_fw(dev_priv, PIPE_WGC_C21_C20(dev_priv, pipe),
			  csc->coeff[7] << 16 | csc->coeff[6]);
	intel_de_write_fw(dev_priv, PIPE_WGC_C22(dev_priv, pipe),
			  csc->coeff[8]);
}

static void vlv_read_wgc_csc(struct intel_crtc *crtc,
			     struct intel_csc_matrix *csc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 tmp;

	tmp = intel_de_read_fw(dev_priv, PIPE_WGC_C01_C00(dev_priv, pipe));
	csc->coeff[0] = tmp & 0xffff;
	csc->coeff[1] = tmp >> 16;

	tmp = intel_de_read_fw(dev_priv, PIPE_WGC_C02(dev_priv, pipe));
	csc->coeff[2] = tmp & 0xffff;

	tmp = intel_de_read_fw(dev_priv, PIPE_WGC_C11_C10(dev_priv, pipe));
	csc->coeff[3] = tmp & 0xffff;
	csc->coeff[4] = tmp >> 16;

	tmp = intel_de_read_fw(dev_priv, PIPE_WGC_C12(dev_priv, pipe));
	csc->coeff[5] = tmp & 0xffff;

	tmp = intel_de_read_fw(dev_priv, PIPE_WGC_C21_C20(dev_priv, pipe));
	csc->coeff[6] = tmp & 0xffff;
	csc->coeff[7] = tmp >> 16;

	tmp = intel_de_read_fw(dev_priv, PIPE_WGC_C22(dev_priv, pipe));
	csc->coeff[8] = tmp & 0xffff;
}

static void vlv_read_csc(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->wgc_enable)
		vlv_read_wgc_csc(crtc, &crtc_state->csc);
}

static void vlv_assign_csc(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (crtc_state->hw.ctm) {
		drm_WARN_ON(&i915->drm, !crtc_state->wgc_enable);

		vlv_wgc_csc_convert_ctm(crtc_state, &crtc_state->csc);
	} else {
		drm_WARN_ON(&i915->drm, crtc_state->wgc_enable);

		intel_csc_clear(&crtc_state->csc);
	}
}

/*
 * CHV Color Gamut Mapping (CGM) CSC
 * |r|   | c0 c1 c2 |   |r|
 * |g| = | c3 c4 c5 | x |g|
 * |b|   | c6 c7 c8 |   |b|
 *
 * Coefficients are two's complement s4.12.
 */
static void chv_cgm_csc_convert_ctm(const struct intel_crtc_state *crtc_state,
				    struct intel_csc_matrix *csc)
{
	const struct drm_color_ctm *ctm = crtc_state->hw.ctm->data;
	int i;

	for (i = 0; i < 9; i++)
		csc->coeff[i] = ctm_to_twos_complement(ctm->matrix[i], 4, 12);
}

#define CHV_CGM_CSC_COEFF_1_0 (1 << 12)

static const struct intel_csc_matrix chv_cgm_csc_matrix_identity = {
	.coeff = {
		CHV_CGM_CSC_COEFF_1_0, 0, 0,
		0, CHV_CGM_CSC_COEFF_1_0, 0,
		0, 0, CHV_CGM_CSC_COEFF_1_0,
	},
};

static void chv_load_cgm_csc(struct intel_crtc *crtc,
			     const struct intel_csc_matrix *csc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	intel_de_write_fw(i915, CGM_PIPE_CSC_COEFF01(pipe),
			  csc->coeff[1] << 16 | csc->coeff[0]);
	intel_de_write_fw(i915, CGM_PIPE_CSC_COEFF23(pipe),
			  csc->coeff[3] << 16 | csc->coeff[2]);
	intel_de_write_fw(i915, CGM_PIPE_CSC_COEFF45(pipe),
			  csc->coeff[5] << 16 | csc->coeff[4]);
	intel_de_write_fw(i915, CGM_PIPE_CSC_COEFF67(pipe),
			  csc->coeff[7] << 16 | csc->coeff[6]);
	intel_de_write_fw(i915, CGM_PIPE_CSC_COEFF8(pipe),
			  csc->coeff[8]);
}

static void chv_read_cgm_csc(struct intel_crtc *crtc,
			     struct intel_csc_matrix *csc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 tmp;

	tmp = intel_de_read_fw(i915, CGM_PIPE_CSC_COEFF01(pipe));
	csc->coeff[0] = tmp & 0xffff;
	csc->coeff[1] = tmp >> 16;

	tmp = intel_de_read_fw(i915, CGM_PIPE_CSC_COEFF23(pipe));
	csc->coeff[2] = tmp & 0xffff;
	csc->coeff[3] = tmp >> 16;

	tmp = intel_de_read_fw(i915, CGM_PIPE_CSC_COEFF45(pipe));
	csc->coeff[4] = tmp & 0xffff;
	csc->coeff[5] = tmp >> 16;

	tmp = intel_de_read_fw(i915, CGM_PIPE_CSC_COEFF67(pipe));
	csc->coeff[6] = tmp & 0xffff;
	csc->coeff[7] = tmp >> 16;

	tmp = intel_de_read_fw(i915, CGM_PIPE_CSC_COEFF8(pipe));
	csc->coeff[8] = tmp & 0xffff;
}

static void chv_read_csc(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->cgm_mode & CGM_PIPE_MODE_CSC)
		chv_read_cgm_csc(crtc, &crtc_state->csc);
}

static void chv_assign_csc(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	drm_WARN_ON(&i915->drm, crtc_state->wgc_enable);

	if (crtc_state->hw.ctm) {
		drm_WARN_ON(&i915->drm, (crtc_state->cgm_mode & CGM_PIPE_MODE_CSC) == 0);

		chv_cgm_csc_convert_ctm(crtc_state, &crtc_state->csc);
	} else {
		drm_WARN_ON(&i915->drm, (crtc_state->cgm_mode & CGM_PIPE_MODE_CSC) == 0);

		crtc_state->csc = chv_cgm_csc_matrix_identity;
	}
}

/* convert hw value with given bit_precision to lut property val */
static u32 intel_color_lut_pack(u32 val, int bit_precision)
{
	if (bit_precision > 16)
		return DIV_ROUND_CLOSEST_ULL(mul_u32_u32(val, (1 << 16) - 1),
					     (1 << bit_precision) - 1);
	else
		return DIV_ROUND_CLOSEST(val * ((1 << 16) - 1),
					 (1 << bit_precision) - 1);
}

static u32 i9xx_lut_8(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(PALETTE_RED_MASK, drm_color_lut_extract(color->red, 8)) |
		REG_FIELD_PREP(PALETTE_GREEN_MASK, drm_color_lut_extract(color->green, 8)) |
		REG_FIELD_PREP(PALETTE_BLUE_MASK, drm_color_lut_extract(color->blue, 8));
}

static void i9xx_lut_8_pack(struct drm_color_lut *entry, u32 val)
{
	entry->red = intel_color_lut_pack(REG_FIELD_GET(PALETTE_RED_MASK, val), 8);
	entry->green = intel_color_lut_pack(REG_FIELD_GET(PALETTE_GREEN_MASK, val), 8);
	entry->blue = intel_color_lut_pack(REG_FIELD_GET(PALETTE_BLUE_MASK, val), 8);
}

/* i8xx/i9xx+ 10bit slope format "even DW" (low 8 bits) */
static u32 _i9xx_lut_10_ldw(u16 a)
{
	return drm_color_lut_extract(a, 10) & 0xff;
}

static u32 i9xx_lut_10_ldw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(PALETTE_RED_MASK, _i9xx_lut_10_ldw(color[0].red)) |
		REG_FIELD_PREP(PALETTE_GREEN_MASK, _i9xx_lut_10_ldw(color[0].green)) |
		REG_FIELD_PREP(PALETTE_BLUE_MASK, _i9xx_lut_10_ldw(color[0].blue));
}

/* i8xx/i9xx+ 10bit slope format "odd DW" (high 2 bits + slope) */
static u32 _i9xx_lut_10_udw(u16 a, u16 b)
{
	unsigned int mantissa, exponent;

	a = drm_color_lut_extract(a, 10);
	b = drm_color_lut_extract(b, 10);

	/* b = a + 8 * m * 2 ^ -e */
	mantissa = clamp(b - a, 0, 0x7f);
	exponent = 3;
	while (mantissa > 0xf) {
		mantissa >>= 1;
		exponent--;
	}

	return (exponent << 6) |
		(mantissa << 2) |
		(a >> 8);
}

static u32 i9xx_lut_10_udw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(PALETTE_RED_MASK, _i9xx_lut_10_udw(color[0].red, color[1].red)) |
		REG_FIELD_PREP(PALETTE_GREEN_MASK, _i9xx_lut_10_udw(color[0].green, color[1].green)) |
		REG_FIELD_PREP(PALETTE_BLUE_MASK, _i9xx_lut_10_udw(color[0].blue, color[1].blue));
}

static void i9xx_lut_10_pack(struct drm_color_lut *color,
			     u32 ldw, u32 udw)
{
	u16 red = REG_FIELD_GET(PALETTE_10BIT_RED_LDW_MASK, ldw) |
		REG_FIELD_GET(PALETTE_10BIT_RED_UDW_MASK, udw) << 8;
	u16 green = REG_FIELD_GET(PALETTE_10BIT_GREEN_LDW_MASK, ldw) |
		REG_FIELD_GET(PALETTE_10BIT_GREEN_UDW_MASK, udw) << 8;
	u16 blue = REG_FIELD_GET(PALETTE_10BIT_BLUE_LDW_MASK, ldw) |
		REG_FIELD_GET(PALETTE_10BIT_BLUE_UDW_MASK, udw) << 8;

	color->red = intel_color_lut_pack(red, 10);
	color->green = intel_color_lut_pack(green, 10);
	color->blue = intel_color_lut_pack(blue, 10);
}

static void i9xx_lut_10_pack_slope(struct drm_color_lut *color,
				   u32 ldw, u32 udw)
{
	int r_exp = REG_FIELD_GET(PALETTE_10BIT_RED_EXP_MASK, udw);
	int r_mant = REG_FIELD_GET(PALETTE_10BIT_RED_MANT_MASK, udw);
	int g_exp = REG_FIELD_GET(PALETTE_10BIT_GREEN_EXP_MASK, udw);
	int g_mant = REG_FIELD_GET(PALETTE_10BIT_GREEN_MANT_MASK, udw);
	int b_exp = REG_FIELD_GET(PALETTE_10BIT_BLUE_EXP_MASK, udw);
	int b_mant = REG_FIELD_GET(PALETTE_10BIT_BLUE_MANT_MASK, udw);

	i9xx_lut_10_pack(color, ldw, udw);

	color->red += r_mant << (3 - r_exp);
	color->green += g_mant << (3 - g_exp);
	color->blue += b_mant << (3 - b_exp);
}

/* i965+ "10.6" bit interpolated format "even DW" (low 8 bits) */
static u32 i965_lut_10p6_ldw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(PALETTE_RED_MASK, color->red & 0xff) |
		REG_FIELD_PREP(PALETTE_GREEN_MASK, color->green & 0xff) |
		REG_FIELD_PREP(PALETTE_BLUE_MASK, color->blue & 0xff);
}

/* i965+ "10.6" interpolated format "odd DW" (high 8 bits) */
static u32 i965_lut_10p6_udw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(PALETTE_RED_MASK, color->red >> 8) |
		REG_FIELD_PREP(PALETTE_GREEN_MASK, color->green >> 8) |
		REG_FIELD_PREP(PALETTE_BLUE_MASK, color->blue >> 8);
}

static void i965_lut_10p6_pack(struct drm_color_lut *entry, u32 ldw, u32 udw)
{
	entry->red = REG_FIELD_GET(PALETTE_RED_MASK, udw) << 8 |
		REG_FIELD_GET(PALETTE_RED_MASK, ldw);
	entry->green = REG_FIELD_GET(PALETTE_GREEN_MASK, udw) << 8 |
		REG_FIELD_GET(PALETTE_GREEN_MASK, ldw);
	entry->blue = REG_FIELD_GET(PALETTE_BLUE_MASK, udw) << 8 |
		REG_FIELD_GET(PALETTE_BLUE_MASK, ldw);
}

static u16 i965_lut_11p6_max_pack(u32 val)
{
	/* PIPEGCMAX is 11.6, clamp to 10.6 */
	return min(val, 0xffffu);
}

static u32 ilk_lut_10(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(PREC_PALETTE_10_RED_MASK, drm_color_lut_extract(color->red, 10)) |
		REG_FIELD_PREP(PREC_PALETTE_10_GREEN_MASK, drm_color_lut_extract(color->green, 10)) |
		REG_FIELD_PREP(PREC_PALETTE_10_BLUE_MASK, drm_color_lut_extract(color->blue, 10));
}

static void ilk_lut_10_pack(struct drm_color_lut *entry, u32 val)
{
	entry->red = intel_color_lut_pack(REG_FIELD_GET(PREC_PALETTE_10_RED_MASK, val), 10);
	entry->green = intel_color_lut_pack(REG_FIELD_GET(PREC_PALETTE_10_GREEN_MASK, val), 10);
	entry->blue = intel_color_lut_pack(REG_FIELD_GET(PREC_PALETTE_10_BLUE_MASK, val), 10);
}

/* ilk+ "12.4" interpolated format (low 6 bits) */
static u32 ilk_lut_12p4_ldw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(PREC_PALETTE_12P4_RED_LDW_MASK, color->red & 0x3f) |
		REG_FIELD_PREP(PREC_PALETTE_12P4_GREEN_LDW_MASK, color->green & 0x3f) |
		REG_FIELD_PREP(PREC_PALETTE_12P4_BLUE_LDW_MASK, color->blue & 0x3f);
}

/* ilk+ "12.4" interpolated format (high 10 bits) */
static u32 ilk_lut_12p4_udw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(PREC_PALETTE_12P4_RED_UDW_MASK, color->red >> 6) |
		REG_FIELD_PREP(PREC_PALETTE_12P4_GREEN_UDW_MASK, color->green >> 6) |
		REG_FIELD_PREP(PREC_PALETTE_12P4_BLUE_UDW_MASK, color->blue >> 6);
}

static void ilk_lut_12p4_pack(struct drm_color_lut *entry, u32 ldw, u32 udw)
{
	entry->red = REG_FIELD_GET(PREC_PALETTE_12P4_RED_UDW_MASK, udw) << 6 |
		REG_FIELD_GET(PREC_PALETTE_12P4_RED_LDW_MASK, ldw);
	entry->green = REG_FIELD_GET(PREC_PALETTE_12P4_GREEN_UDW_MASK, udw) << 6 |
		REG_FIELD_GET(PREC_PALETTE_12P4_GREEN_LDW_MASK, ldw);
	entry->blue = REG_FIELD_GET(PREC_PALETTE_12P4_BLUE_UDW_MASK, udw) << 6 |
		REG_FIELD_GET(PREC_PALETTE_12P4_BLUE_LDW_MASK, ldw);
}

static void icl_color_commit_noarm(const struct intel_crtc_state *crtc_state)
{
	/*
	 * Despite Wa_1406463849, ICL no longer suffers from the SKL
	 * DC5/PSR CSC black screen issue (see skl_color_commit_noarm()).
	 * Possibly due to the extra sticky CSC arming
	 * (see icl_color_post_update()).
	 *
	 * On TGL+ all CSC arming issues have been properly fixed.
	 */
	icl_load_csc_matrix(crtc_state);
}

static void skl_color_commit_noarm(const struct intel_crtc_state *crtc_state)
{
	/*
	 * Possibly related to display WA #1184, SKL CSC loses the latched
	 * CSC coeff/offset register values if the CSC registers are disarmed
	 * between DC5 exit and PSR exit. This will cause the plane(s) to
	 * output all black (until CSC_MODE is rearmed and properly latched).
	 * Once PSR exit (and proper register latching) has occurred the
	 * danger is over. Thus when PSR is enabled the CSC coeff/offset
	 * register programming will be peformed from skl_color_commit_arm()
	 * which is called after PSR exit.
	 */
	if (!crtc_state->has_psr)
		ilk_load_csc_matrix(crtc_state);
}

static void ilk_color_commit_noarm(const struct intel_crtc_state *crtc_state)
{
	ilk_load_csc_matrix(crtc_state);
}

static void i9xx_color_commit_arm(const struct intel_crtc_state *crtc_state)
{
	/* update TRANSCONF GAMMA_MODE */
	i9xx_set_pipeconf(crtc_state);
}

static void ilk_color_commit_arm(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	/* update TRANSCONF GAMMA_MODE */
	ilk_set_pipeconf(crtc_state);

	intel_de_write_fw(i915, PIPE_CSC_MODE(crtc->pipe),
			  crtc_state->csc_mode);
}

static void hsw_color_commit_arm(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	intel_de_write(i915, GAMMA_MODE(crtc->pipe),
		       crtc_state->gamma_mode);

	intel_de_write_fw(i915, PIPE_CSC_MODE(crtc->pipe),
			  crtc_state->csc_mode);
}

static u32 hsw_read_gamma_mode(struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	return intel_de_read(i915, GAMMA_MODE(crtc->pipe));
}

static u32 ilk_read_csc_mode(struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	return intel_de_read(i915, PIPE_CSC_MODE(crtc->pipe));
}

static void i9xx_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_plane *plane = to_intel_plane(crtc->base.primary);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum i9xx_plane_id i9xx_plane = plane->i9xx_plane;
	u32 tmp;

	tmp = intel_de_read(dev_priv, DSPCNTR(dev_priv, i9xx_plane));

	if (tmp & DISP_PIPE_GAMMA_ENABLE)
		crtc_state->gamma_enable = true;

	if (!HAS_GMCH(dev_priv) && tmp & DISP_PIPE_CSC_ENABLE)
		crtc_state->csc_enable = true;
}

static void hsw_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	crtc_state->gamma_mode = hsw_read_gamma_mode(crtc);
	crtc_state->csc_mode = ilk_read_csc_mode(crtc);

	i9xx_get_config(crtc_state);
}

static void skl_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	u32 tmp;

	crtc_state->gamma_mode = hsw_read_gamma_mode(crtc);
	crtc_state->csc_mode = ilk_read_csc_mode(crtc);

	tmp = intel_de_read(i915, SKL_BOTTOM_COLOR(crtc->pipe));

	if (tmp & SKL_BOTTOM_COLOR_GAMMA_ENABLE)
		crtc_state->gamma_enable = true;

	if (tmp & SKL_BOTTOM_COLOR_CSC_ENABLE)
		crtc_state->csc_enable = true;
}

static void skl_color_commit_arm(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 val = 0;

	if (crtc_state->has_psr)
		ilk_load_csc_matrix(crtc_state);

	/*
	 * We don't (yet) allow userspace to control the pipe background color,
	 * so force it to black, but apply pipe gamma and CSC appropriately
	 * so that its handling will match how we program our planes.
	 */
	if (crtc_state->gamma_enable)
		val |= SKL_BOTTOM_COLOR_GAMMA_ENABLE;
	if (crtc_state->csc_enable)
		val |= SKL_BOTTOM_COLOR_CSC_ENABLE;
	intel_de_write(i915, SKL_BOTTOM_COLOR(pipe), val);

	intel_de_write(i915, GAMMA_MODE(crtc->pipe),
		       crtc_state->gamma_mode);

	intel_de_write_fw(i915, PIPE_CSC_MODE(crtc->pipe),
			  crtc_state->csc_mode);
}

static void icl_color_commit_arm(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	/*
	 * We don't (yet) allow userspace to control the pipe background color,
	 * so force it to black.
	 */
	intel_de_write(i915, SKL_BOTTOM_COLOR(pipe), 0);

	intel_de_write(i915, GAMMA_MODE(crtc->pipe),
		       crtc_state->gamma_mode);

	intel_de_write_fw(i915, PIPE_CSC_MODE(crtc->pipe),
			  crtc_state->csc_mode);
}

static void icl_color_post_update(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	/*
	 * Despite Wa_1406463849, ICL CSC is no longer disarmed by
	 * coeff/offset register *writes*. Instead, once CSC_MODE
	 * is armed it stays armed, even after it has been latched.
	 * Afterwards the coeff/offset registers become effectively
	 * self-arming. That self-arming must be disabled before the
	 * next icl_color_commit_noarm() tries to write the next set
	 * of coeff/offset registers. Fortunately register *reads*
	 * do still disarm the CSC. Naturally this must not be done
	 * until the previously written CSC registers have actually
	 * been latched.
	 *
	 * TGL+ no longer need this workaround.
	 */
	intel_de_read_fw(i915, PIPE_CSC_PREOFF_HI(crtc->pipe));
}

static struct drm_property_blob *
create_linear_lut(struct drm_i915_private *i915, int lut_size)
{
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;
	int i;

	blob = drm_property_create_blob(&i915->drm,
					sizeof(lut[0]) * lut_size,
					NULL);
	if (IS_ERR(blob))
		return blob;

	lut = blob->data;

	for (i = 0; i < lut_size; i++) {
		u16 val = 0xffff * i / (lut_size - 1);

		lut[i].red = val;
		lut[i].green = val;
		lut[i].blue = val;
	}

	return blob;
}

static u16 lut_limited_range(unsigned int value)
{
	unsigned int min = 16 << 8;
	unsigned int max = 235 << 8;

	return value * (max - min) / 0xffff + min;
}

static struct drm_property_blob *
create_resized_lut(struct drm_i915_private *i915,
		   const struct drm_property_blob *blob_in, int lut_out_size,
		   bool limited_color_range)
{
	int i, lut_in_size = drm_color_lut_size(blob_in);
	struct drm_property_blob *blob_out;
	const struct drm_color_lut *lut_in;
	struct drm_color_lut *lut_out;

	blob_out = drm_property_create_blob(&i915->drm,
					    sizeof(lut_out[0]) * lut_out_size,
					    NULL);
	if (IS_ERR(blob_out))
		return blob_out;

	lut_in = blob_in->data;
	lut_out = blob_out->data;

	for (i = 0; i < lut_out_size; i++) {
		const struct drm_color_lut *entry =
			&lut_in[i * (lut_in_size - 1) / (lut_out_size - 1)];

		if (limited_color_range) {
			lut_out[i].red = lut_limited_range(entry->red);
			lut_out[i].green = lut_limited_range(entry->green);
			lut_out[i].blue = lut_limited_range(entry->blue);
		} else {
			lut_out[i] = *entry;
		}
	}

	return blob_out;
}

static void i9xx_load_lut_8(struct intel_crtc *crtc,
			    const struct drm_property_blob *blob)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_color_lut *lut;
	enum pipe pipe = crtc->pipe;
	int i;

	if (!blob)
		return;

	lut = blob->data;

	for (i = 0; i < 256; i++)
		intel_de_write_fw(dev_priv, PALETTE(dev_priv, pipe, i),
				  i9xx_lut_8(&lut[i]));
}

static void i9xx_load_lut_10(struct intel_crtc *crtc,
			     const struct drm_property_blob *blob)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_color_lut *lut = blob->data;
	int i, lut_size = drm_color_lut_size(blob);
	enum pipe pipe = crtc->pipe;

	for (i = 0; i < lut_size - 1; i++) {
		intel_de_write_fw(dev_priv,
				  PALETTE(dev_priv, pipe, 2 * i + 0),
				  i9xx_lut_10_ldw(&lut[i]));
		intel_de_write_fw(dev_priv,
				  PALETTE(dev_priv, pipe, 2 * i + 1),
				  i9xx_lut_10_udw(&lut[i]));
	}
}

static void i9xx_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_property_blob *post_csc_lut = crtc_state->post_csc_lut;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		i9xx_load_lut_8(crtc, post_csc_lut);
		break;
	case GAMMA_MODE_MODE_10BIT:
		i9xx_load_lut_10(crtc, post_csc_lut);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static void i965_load_lut_10p6(struct intel_crtc *crtc,
			       const struct drm_property_blob *blob)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_color_lut *lut = blob->data;
	int i, lut_size = drm_color_lut_size(blob);
	enum pipe pipe = crtc->pipe;

	for (i = 0; i < lut_size - 1; i++) {
		intel_de_write_fw(dev_priv,
				  PALETTE(dev_priv, pipe, 2 * i + 0),
				  i965_lut_10p6_ldw(&lut[i]));
		intel_de_write_fw(dev_priv,
				  PALETTE(dev_priv, pipe, 2 * i + 1),
				  i965_lut_10p6_udw(&lut[i]));
	}

	intel_de_write_fw(dev_priv, PIPEGCMAX(dev_priv, pipe, 0), lut[i].red);
	intel_de_write_fw(dev_priv, PIPEGCMAX(dev_priv, pipe, 1), lut[i].green);
	intel_de_write_fw(dev_priv, PIPEGCMAX(dev_priv, pipe, 2), lut[i].blue);
}

static void i965_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_property_blob *post_csc_lut = crtc_state->post_csc_lut;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		i9xx_load_lut_8(crtc, post_csc_lut);
		break;
	case GAMMA_MODE_MODE_10BIT:
		i965_load_lut_10p6(crtc, post_csc_lut);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static void ilk_lut_write(const struct intel_crtc_state *crtc_state,
			  i915_reg_t reg, u32 val)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (crtc_state->dsb_color_vblank)
		intel_dsb_reg_write(crtc_state->dsb_color_vblank, reg, val);
	else
		intel_de_write_fw(i915, reg, val);
}

static void ilk_load_lut_8(const struct intel_crtc_state *crtc_state,
			   const struct drm_property_blob *blob)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_color_lut *lut;
	enum pipe pipe = crtc->pipe;
	int i;

	if (!blob)
		return;

	lut = blob->data;

	/*
	 * DSB fails to correctly load the legacy LUT unless
	 * we either write each entry twice when using posted
	 * writes, or we use non-posted writes.
	 *
	 * If palette anti-collision is active during LUT
	 * register writes:
	 * - posted writes simply get dropped and thus the LUT
	 *   contents may not be correctly updated
	 * - non-posted writes are blocked and thus the LUT
	 *   contents are always correct, but simultaneous CPU
	 *   MMIO access will start to fail
	 *
	 * Choose the lesser of two evils and use posted writes.
	 * Using posted writes is also faster, even when having
	 * to write each register twice.
	 */
	for (i = 0; i < 256; i++) {
		ilk_lut_write(crtc_state, LGC_PALETTE(pipe, i),
			      i9xx_lut_8(&lut[i]));
		if (crtc_state->dsb_color_vblank)
			ilk_lut_write(crtc_state, LGC_PALETTE(pipe, i),
				      i9xx_lut_8(&lut[i]));
	}
}

static void ilk_load_lut_10(const struct intel_crtc_state *crtc_state,
			    const struct drm_property_blob *blob)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_color_lut *lut = blob->data;
	int i, lut_size = drm_color_lut_size(blob);
	enum pipe pipe = crtc->pipe;

	for (i = 0; i < lut_size; i++)
		ilk_lut_write(crtc_state, PREC_PALETTE(pipe, i),
			      ilk_lut_10(&lut[i]));
}

static void ilk_load_luts(const struct intel_crtc_state *crtc_state)
{
	const struct drm_property_blob *post_csc_lut = crtc_state->post_csc_lut;
	const struct drm_property_blob *pre_csc_lut = crtc_state->pre_csc_lut;
	const struct drm_property_blob *blob = post_csc_lut ?: pre_csc_lut;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		ilk_load_lut_8(crtc_state, blob);
		break;
	case GAMMA_MODE_MODE_10BIT:
		ilk_load_lut_10(crtc_state, blob);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static int ivb_lut_10_size(u32 prec_index)
{
	if (prec_index & PAL_PREC_SPLIT_MODE)
		return 512;
	else
		return 1024;
}

/*
 * IVB/HSW Bspec / PAL_PREC_INDEX:
 * "Restriction : Index auto increment mode is not
 *  supported and must not be enabled."
 */
static void ivb_load_lut_10(const struct intel_crtc_state *crtc_state,
			    const struct drm_property_blob *blob,
			    u32 prec_index)
{
	const struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_color_lut *lut = blob->data;
	int i, lut_size = drm_color_lut_size(blob);
	enum pipe pipe = crtc->pipe;

	for (i = 0; i < lut_size; i++) {
		ilk_lut_write(crtc_state, PREC_PAL_INDEX(pipe),
			      prec_index + i);
		ilk_lut_write(crtc_state, PREC_PAL_DATA(pipe),
			      ilk_lut_10(&lut[i]));
	}

	/*
	 * Reset the index, otherwise it prevents the legacy palette to be
	 * written properly.
	 */
	ilk_lut_write(crtc_state, PREC_PAL_INDEX(pipe),
		      PAL_PREC_INDEX_VALUE(0));
}

/* On BDW+ the index auto increment mode actually works */
static void bdw_load_lut_10(const struct intel_crtc_state *crtc_state,
			    const struct drm_property_blob *blob,
			    u32 prec_index)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_color_lut *lut = blob->data;
	int i, lut_size = drm_color_lut_size(blob);
	enum pipe pipe = crtc->pipe;

	ilk_lut_write(crtc_state, PREC_PAL_INDEX(pipe),
		      prec_index);
	ilk_lut_write(crtc_state, PREC_PAL_INDEX(pipe),
		      PAL_PREC_AUTO_INCREMENT |
		      prec_index);

	for (i = 0; i < lut_size; i++)
		ilk_lut_write(crtc_state, PREC_PAL_DATA(pipe),
			      ilk_lut_10(&lut[i]));

	/*
	 * Reset the index, otherwise it prevents the legacy palette to be
	 * written properly.
	 */
	ilk_lut_write(crtc_state, PREC_PAL_INDEX(pipe),
		      PAL_PREC_INDEX_VALUE(0));
}

static void ivb_load_lut_ext_max(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;

	/* Program the max register to clamp values > 1.0. */
	ilk_lut_write(crtc_state, PREC_PAL_EXT_GC_MAX(pipe, 0), 1 << 16);
	ilk_lut_write(crtc_state, PREC_PAL_EXT_GC_MAX(pipe, 1), 1 << 16);
	ilk_lut_write(crtc_state, PREC_PAL_EXT_GC_MAX(pipe, 2), 1 << 16);
}

static void glk_load_lut_ext2_max(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;

	/* Program the max register to clamp values > 1.0. */
	ilk_lut_write(crtc_state, PREC_PAL_EXT2_GC_MAX(pipe, 0), 1 << 16);
	ilk_lut_write(crtc_state, PREC_PAL_EXT2_GC_MAX(pipe, 1), 1 << 16);
	ilk_lut_write(crtc_state, PREC_PAL_EXT2_GC_MAX(pipe, 2), 1 << 16);
}

static void ivb_load_luts(const struct intel_crtc_state *crtc_state)
{
	const struct drm_property_blob *post_csc_lut = crtc_state->post_csc_lut;
	const struct drm_property_blob *pre_csc_lut = crtc_state->pre_csc_lut;
	const struct drm_property_blob *blob = post_csc_lut ?: pre_csc_lut;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		ilk_load_lut_8(crtc_state, blob);
		break;
	case GAMMA_MODE_MODE_SPLIT:
		ivb_load_lut_10(crtc_state, pre_csc_lut, PAL_PREC_SPLIT_MODE |
				PAL_PREC_INDEX_VALUE(0));
		ivb_load_lut_ext_max(crtc_state);
		ivb_load_lut_10(crtc_state, post_csc_lut, PAL_PREC_SPLIT_MODE |
				PAL_PREC_INDEX_VALUE(512));
		break;
	case GAMMA_MODE_MODE_10BIT:
		ivb_load_lut_10(crtc_state, blob,
				PAL_PREC_INDEX_VALUE(0));
		ivb_load_lut_ext_max(crtc_state);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static void bdw_load_luts(const struct intel_crtc_state *crtc_state)
{
	const struct drm_property_blob *post_csc_lut = crtc_state->post_csc_lut;
	const struct drm_property_blob *pre_csc_lut = crtc_state->pre_csc_lut;
	const struct drm_property_blob *blob = post_csc_lut ?: pre_csc_lut;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		ilk_load_lut_8(crtc_state, blob);
		break;
	case GAMMA_MODE_MODE_SPLIT:
		bdw_load_lut_10(crtc_state, pre_csc_lut, PAL_PREC_SPLIT_MODE |
				PAL_PREC_INDEX_VALUE(0));
		ivb_load_lut_ext_max(crtc_state);
		bdw_load_lut_10(crtc_state, post_csc_lut, PAL_PREC_SPLIT_MODE |
				PAL_PREC_INDEX_VALUE(512));
		break;
	case GAMMA_MODE_MODE_10BIT:
		bdw_load_lut_10(crtc_state, blob,
				PAL_PREC_INDEX_VALUE(0));
		ivb_load_lut_ext_max(crtc_state);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static int glk_degamma_lut_size(struct drm_i915_private *i915)
{
	if (DISPLAY_VER(i915) >= 13)
		return 131;
	else
		return 35;
}

static u32 glk_degamma_lut(const struct drm_color_lut *color)
{
	return color->green;
}

static void glk_degamma_lut_pack(struct drm_color_lut *entry, u32 val)
{
	/* PRE_CSC_GAMC_DATA is 3.16, clamp to 0.16 */
	entry->red = entry->green = entry->blue = min(val, 0xffffu);
}

static u32 mtl_degamma_lut(const struct drm_color_lut *color)
{
	return drm_color_lut_extract(color->green, 24);
}

static void mtl_degamma_lut_pack(struct drm_color_lut *entry, u32 val)
{
	/* PRE_CSC_GAMC_DATA is 3.24, clamp to 0.16 */
	entry->red = entry->green = entry->blue =
		intel_color_lut_pack(min(val, 0xffffffu), 24);
}

static void glk_load_degamma_lut(const struct intel_crtc_state *crtc_state,
				 const struct drm_property_blob *blob)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	const struct drm_color_lut *lut = blob->data;
	int i, lut_size = drm_color_lut_size(blob);
	enum pipe pipe = crtc->pipe;

	/*
	 * When setting the auto-increment bit, the hardware seems to
	 * ignore the index bits, so we need to reset it to index 0
	 * separately.
	 */
	ilk_lut_write(crtc_state, PRE_CSC_GAMC_INDEX(pipe),
		      PRE_CSC_GAMC_INDEX_VALUE(0));
	ilk_lut_write(crtc_state, PRE_CSC_GAMC_INDEX(pipe),
		      PRE_CSC_GAMC_AUTO_INCREMENT |
		      PRE_CSC_GAMC_INDEX_VALUE(0));

	for (i = 0; i < lut_size; i++) {
		/*
		 * First lut_size entries represent range from 0 to 1.0
		 * 3 additional lut entries will represent extended range
		 * inputs 3.0 and 7.0 respectively, currently clamped
		 * at 1.0. Since the precision is 16bit, the user
		 * value can be directly filled to register.
		 * The pipe degamma table in GLK+ onwards doesn't
		 * support different values per channel, so this just
		 * programs green value which will be equal to Red and
		 * Blue into the lut registers.
		 * ToDo: Extend to max 7.0. Enable 32 bit input value
		 * as compared to just 16 to achieve this.
		 */
		ilk_lut_write(crtc_state, PRE_CSC_GAMC_DATA(pipe),
			      DISPLAY_VER(i915) >= 14 ?
			      mtl_degamma_lut(&lut[i]) : glk_degamma_lut(&lut[i]));
	}

	/* Clamp values > 1.0. */
	while (i++ < glk_degamma_lut_size(i915))
		ilk_lut_write(crtc_state, PRE_CSC_GAMC_DATA(pipe),
			      DISPLAY_VER(i915) >= 14 ?
			      1 << 24 : 1 << 16);

	ilk_lut_write(crtc_state, PRE_CSC_GAMC_INDEX(pipe), 0);
}

static void glk_load_luts(const struct intel_crtc_state *crtc_state)
{
	const struct drm_property_blob *pre_csc_lut = crtc_state->pre_csc_lut;
	const struct drm_property_blob *post_csc_lut = crtc_state->post_csc_lut;

	if (pre_csc_lut)
		glk_load_degamma_lut(crtc_state, pre_csc_lut);

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		ilk_load_lut_8(crtc_state, post_csc_lut);
		break;
	case GAMMA_MODE_MODE_10BIT:
		bdw_load_lut_10(crtc_state, post_csc_lut, PAL_PREC_INDEX_VALUE(0));
		ivb_load_lut_ext_max(crtc_state);
		glk_load_lut_ext2_max(crtc_state);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static void
ivb_load_lut_max(const struct intel_crtc_state *crtc_state,
		 const struct drm_color_lut *color)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;

	/* FIXME LUT entries are 16 bit only, so we can prog 0xFFFF max */
	ilk_lut_write(crtc_state, PREC_PAL_GC_MAX(pipe, 0), color->red);
	ilk_lut_write(crtc_state, PREC_PAL_GC_MAX(pipe, 1), color->green);
	ilk_lut_write(crtc_state, PREC_PAL_GC_MAX(pipe, 2), color->blue);
}

static void
icl_program_gamma_superfine_segment(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_property_blob *blob = crtc_state->post_csc_lut;
	const struct drm_color_lut *lut = blob->data;
	enum pipe pipe = crtc->pipe;
	int i;

	/*
	 * Program Super Fine segment (let's call it seg1)...
	 *
	 * Super Fine segment's step is 1/(8 * 128 * 256) and it has
	 * 9 entries, corresponding to values 0, 1/(8 * 128 * 256),
	 * 2/(8 * 128 * 256) ... 8/(8 * 128 * 256).
	 */
	ilk_lut_write(crtc_state, PREC_PAL_MULTI_SEG_INDEX(pipe),
		      PAL_PREC_MULTI_SEG_INDEX_VALUE(0));
	ilk_lut_write(crtc_state, PREC_PAL_MULTI_SEG_INDEX(pipe),
		      PAL_PREC_AUTO_INCREMENT |
		      PAL_PREC_MULTI_SEG_INDEX_VALUE(0));

	for (i = 0; i < 9; i++) {
		const struct drm_color_lut *entry = &lut[i];

		ilk_lut_write(crtc_state, PREC_PAL_MULTI_SEG_DATA(pipe),
			      ilk_lut_12p4_ldw(entry));
		ilk_lut_write(crtc_state, PREC_PAL_MULTI_SEG_DATA(pipe),
			      ilk_lut_12p4_udw(entry));
	}

	ilk_lut_write(crtc_state, PREC_PAL_MULTI_SEG_INDEX(pipe),
		      PAL_PREC_MULTI_SEG_INDEX_VALUE(0));
}

static void
icl_program_gamma_multi_segment(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct drm_property_blob *blob = crtc_state->post_csc_lut;
	const struct drm_color_lut *lut = blob->data;
	const struct drm_color_lut *entry;
	enum pipe pipe = crtc->pipe;
	int i;

	/*
	 * Program Fine segment (let's call it seg2)...
	 *
	 * Fine segment's step is 1/(128 * 256) i.e. 1/(128 * 256), 2/(128 * 256)
	 * ... 256/(128 * 256). So in order to program fine segment of LUT we
	 * need to pick every 8th entry in the LUT, and program 256 indexes.
	 *
	 * PAL_PREC_INDEX[0] and PAL_PREC_INDEX[1] map to seg2[1],
	 * seg2[0] being unused by the hardware.
	 */
	ilk_lut_write(crtc_state, PREC_PAL_INDEX(pipe),
		      PAL_PREC_INDEX_VALUE(0));
	ilk_lut_write(crtc_state, PREC_PAL_INDEX(pipe),
		      PAL_PREC_AUTO_INCREMENT |
		      PAL_PREC_INDEX_VALUE(0));

	for (i = 1; i < 257; i++) {
		entry = &lut[i * 8];

		ilk_lut_write(crtc_state, PREC_PAL_DATA(pipe),
			      ilk_lut_12p4_ldw(entry));
		ilk_lut_write(crtc_state, PREC_PAL_DATA(pipe),
			      ilk_lut_12p4_udw(entry));
	}

	/*
	 * Program Coarse segment (let's call it seg3)...
	 *
	 * Coarse segment starts from index 0 and it's step is 1/256 ie 0,
	 * 1/256, 2/256 ... 256/256. As per the description of each entry in LUT
	 * above, we need to pick every (8 * 128)th entry in LUT, and
	 * program 256 of those.
	 *
	 * Spec is not very clear about if entries seg3[0] and seg3[1] are
	 * being used or not, but we still need to program these to advance
	 * the index.
	 */
	for (i = 0; i < 256; i++) {
		entry = &lut[i * 8 * 128];

		ilk_lut_write(crtc_state, PREC_PAL_DATA(pipe),
			      ilk_lut_12p4_ldw(entry));
		ilk_lut_write(crtc_state, PREC_PAL_DATA(pipe),
			      ilk_lut_12p4_udw(entry));
	}

	ilk_lut_write(crtc_state, PREC_PAL_INDEX(pipe),
		      PAL_PREC_INDEX_VALUE(0));

	/* The last entry in the LUT is to be programmed in GCMAX */
	entry = &lut[256 * 8 * 128];
	ivb_load_lut_max(crtc_state, entry);
}

static void icl_load_luts(const struct intel_crtc_state *crtc_state)
{
	const struct drm_property_blob *pre_csc_lut = crtc_state->pre_csc_lut;
	const struct drm_property_blob *post_csc_lut = crtc_state->post_csc_lut;

	if (pre_csc_lut)
		glk_load_degamma_lut(crtc_state, pre_csc_lut);

	switch (crtc_state->gamma_mode & GAMMA_MODE_MODE_MASK) {
	case GAMMA_MODE_MODE_8BIT:
		ilk_load_lut_8(crtc_state, post_csc_lut);
		break;
	case GAMMA_MODE_MODE_12BIT_MULTI_SEG:
		icl_program_gamma_superfine_segment(crtc_state);
		icl_program_gamma_multi_segment(crtc_state);
		ivb_load_lut_ext_max(crtc_state);
		glk_load_lut_ext2_max(crtc_state);
		break;
	case GAMMA_MODE_MODE_10BIT:
		bdw_load_lut_10(crtc_state, post_csc_lut, PAL_PREC_INDEX_VALUE(0));
		ivb_load_lut_ext_max(crtc_state);
		glk_load_lut_ext2_max(crtc_state);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static void vlv_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->wgc_enable)
		vlv_load_wgc_csc(crtc, &crtc_state->csc);

	i965_load_luts(crtc_state);
}

static u32 chv_cgm_degamma_ldw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(CGM_PIPE_DEGAMMA_GREEN_LDW_MASK, drm_color_lut_extract(color->green, 14)) |
		REG_FIELD_PREP(CGM_PIPE_DEGAMMA_BLUE_LDW_MASK, drm_color_lut_extract(color->blue, 14));
}

static u32 chv_cgm_degamma_udw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(CGM_PIPE_DEGAMMA_RED_UDW_MASK, drm_color_lut_extract(color->red, 14));
}

static void chv_cgm_degamma_pack(struct drm_color_lut *entry, u32 ldw, u32 udw)
{
	entry->green = intel_color_lut_pack(REG_FIELD_GET(CGM_PIPE_DEGAMMA_GREEN_LDW_MASK, ldw), 14);
	entry->blue = intel_color_lut_pack(REG_FIELD_GET(CGM_PIPE_DEGAMMA_BLUE_LDW_MASK, ldw), 14);
	entry->red = intel_color_lut_pack(REG_FIELD_GET(CGM_PIPE_DEGAMMA_RED_UDW_MASK, udw), 14);
}

static void chv_load_cgm_degamma(struct intel_crtc *crtc,
				 const struct drm_property_blob *blob)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	const struct drm_color_lut *lut = blob->data;
	int i, lut_size = drm_color_lut_size(blob);
	enum pipe pipe = crtc->pipe;

	for (i = 0; i < lut_size; i++) {
		intel_de_write_fw(i915, CGM_PIPE_DEGAMMA(pipe, i, 0),
				  chv_cgm_degamma_ldw(&lut[i]));
		intel_de_write_fw(i915, CGM_PIPE_DEGAMMA(pipe, i, 1),
				  chv_cgm_degamma_udw(&lut[i]));
	}
}

static u32 chv_cgm_gamma_ldw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(CGM_PIPE_GAMMA_GREEN_LDW_MASK, drm_color_lut_extract(color->green, 10)) |
		REG_FIELD_PREP(CGM_PIPE_GAMMA_BLUE_LDW_MASK, drm_color_lut_extract(color->blue, 10));
}

static u32 chv_cgm_gamma_udw(const struct drm_color_lut *color)
{
	return REG_FIELD_PREP(CGM_PIPE_GAMMA_RED_UDW_MASK, drm_color_lut_extract(color->red, 10));
}

static void chv_cgm_gamma_pack(struct drm_color_lut *entry, u32 ldw, u32 udw)
{
	entry->green = intel_color_lut_pack(REG_FIELD_GET(CGM_PIPE_GAMMA_GREEN_LDW_MASK, ldw), 10);
	entry->blue = intel_color_lut_pack(REG_FIELD_GET(CGM_PIPE_GAMMA_BLUE_LDW_MASK, ldw), 10);
	entry->red = intel_color_lut_pack(REG_FIELD_GET(CGM_PIPE_GAMMA_RED_UDW_MASK, udw), 10);
}

static void chv_load_cgm_gamma(struct intel_crtc *crtc,
			       const struct drm_property_blob *blob)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	const struct drm_color_lut *lut = blob->data;
	int i, lut_size = drm_color_lut_size(blob);
	enum pipe pipe = crtc->pipe;

	for (i = 0; i < lut_size; i++) {
		intel_de_write_fw(i915, CGM_PIPE_GAMMA(pipe, i, 0),
				  chv_cgm_gamma_ldw(&lut[i]));
		intel_de_write_fw(i915, CGM_PIPE_GAMMA(pipe, i, 1),
				  chv_cgm_gamma_udw(&lut[i]));
	}
}

static void chv_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	const struct drm_property_blob *pre_csc_lut = crtc_state->pre_csc_lut;
	const struct drm_property_blob *post_csc_lut = crtc_state->post_csc_lut;

	if (crtc_state->cgm_mode & CGM_PIPE_MODE_CSC)
		chv_load_cgm_csc(crtc, &crtc_state->csc);

	if (crtc_state->cgm_mode & CGM_PIPE_MODE_DEGAMMA)
		chv_load_cgm_degamma(crtc, pre_csc_lut);

	if (crtc_state->cgm_mode & CGM_PIPE_MODE_GAMMA)
		chv_load_cgm_gamma(crtc, post_csc_lut);
	else
		i965_load_luts(crtc_state);

	intel_de_write_fw(i915, CGM_PIPE_MODE(crtc->pipe),
			  crtc_state->cgm_mode);
}

void intel_color_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (crtc_state->dsb_color_vblank)
		return;

	i915->display.funcs.color->load_luts(crtc_state);
}

void intel_color_commit_noarm(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (i915->display.funcs.color->color_commit_noarm)
		i915->display.funcs.color->color_commit_noarm(crtc_state);
}

void intel_color_commit_arm(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	i915->display.funcs.color->color_commit_arm(crtc_state);

	if (crtc_state->dsb_color_commit)
		intel_dsb_commit(crtc_state->dsb_color_commit, false);
}

void intel_color_post_update(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (i915->display.funcs.color->color_post_update)
		i915->display.funcs.color->color_post_update(crtc_state);
}

void intel_color_modeset(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	intel_color_load_luts(crtc_state);
	intel_color_commit_noarm(crtc_state);
	intel_color_commit_arm(crtc_state);

	if (DISPLAY_VER(display) < 9) {
		struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
		struct intel_plane *plane = to_intel_plane(crtc->base.primary);

		/* update DSPCNTR to configure gamma/csc for pipe bottom color */
		plane->disable_arm(NULL, plane, crtc_state);
	}
}

void intel_color_prepare_commit(struct intel_atomic_state *state,
				struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!crtc_state->hw.active ||
	    intel_crtc_needs_modeset(crtc_state))
		return;

	if (!intel_crtc_needs_color_update(crtc_state))
		return;

	if (!crtc_state->pre_csc_lut && !crtc_state->post_csc_lut)
		return;

	crtc_state->dsb_color_vblank = intel_dsb_prepare(state, crtc, INTEL_DSB_1, 1024);
	if (!crtc_state->dsb_color_vblank)
		return;

	i915->display.funcs.color->load_luts(crtc_state);

	intel_dsb_finish(crtc_state->dsb_color_vblank);

	crtc_state->dsb_color_commit = intel_dsb_prepare(state, crtc, INTEL_DSB_0, 16);
	if (!crtc_state->dsb_color_commit) {
		intel_dsb_cleanup(crtc_state->dsb_color_vblank);
		crtc_state->dsb_color_vblank = NULL;
		return;
	}

	intel_dsb_chain(state, crtc_state->dsb_color_commit,
			crtc_state->dsb_color_vblank, true);

	intel_dsb_finish(crtc_state->dsb_color_commit);
}

void intel_color_cleanup_commit(struct intel_crtc_state *crtc_state)
{
	if (crtc_state->dsb_color_commit) {
		intel_dsb_cleanup(crtc_state->dsb_color_commit);
		crtc_state->dsb_color_commit = NULL;
	}

	if (crtc_state->dsb_color_vblank) {
		intel_dsb_cleanup(crtc_state->dsb_color_vblank);
		crtc_state->dsb_color_vblank = NULL;
	}
}

void intel_color_wait_commit(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->dsb_color_commit)
		intel_dsb_wait(crtc_state->dsb_color_commit);
	if (crtc_state->dsb_color_vblank)
		intel_dsb_wait(crtc_state->dsb_color_vblank);
}

bool intel_color_uses_dsb(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->dsb_color_vblank;
}

static bool intel_can_preload_luts(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);

	return !old_crtc_state->post_csc_lut &&
		!old_crtc_state->pre_csc_lut;
}

static bool vlv_can_preload_luts(struct intel_atomic_state *state,
				 struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);

	return !old_crtc_state->wgc_enable &&
		!old_crtc_state->post_csc_lut;
}

static bool chv_can_preload_luts(struct intel_atomic_state *state,
				 struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	/*
	 * CGM_PIPE_MODE is itself single buffered. We'd have to
	 * somehow split it out from chv_load_luts() if we wanted
	 * the ability to preload the CGM LUTs/CSC without tearing.
	 */
	if (old_crtc_state->cgm_mode || new_crtc_state->cgm_mode)
		return false;

	return vlv_can_preload_luts(state, crtc);
}

int intel_color_check(struct intel_atomic_state *state,
		      struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	/*
	 * May need to update pipe gamma enable bits
	 * when C8 planes are getting enabled/disabled.
	 */
	if (!old_crtc_state->c8_planes != !new_crtc_state->c8_planes)
		new_crtc_state->uapi.color_mgmt_changed = true;

	if (!intel_crtc_needs_color_update(new_crtc_state))
		return 0;

	return i915->display.funcs.color->color_check(state, crtc);
}

void intel_color_get_config(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (i915->display.funcs.color->get_config)
		i915->display.funcs.color->get_config(crtc_state);

	i915->display.funcs.color->read_luts(crtc_state);

	if (i915->display.funcs.color->read_csc)
		i915->display.funcs.color->read_csc(crtc_state);
}

bool intel_color_lut_equal(const struct intel_crtc_state *crtc_state,
			   const struct drm_property_blob *blob1,
			   const struct drm_property_blob *blob2,
			   bool is_pre_csc_lut)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	/*
	 * FIXME c8_planes readout missing thus
	 * .read_luts() doesn't read out post_csc_lut.
	 */
	if (!is_pre_csc_lut && crtc_state->c8_planes)
		return true;

	return i915->display.funcs.color->lut_equal(crtc_state, blob1, blob2,
						    is_pre_csc_lut);
}

static bool need_plane_update(struct intel_plane *plane,
			      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);

	/*
	 * On pre-SKL the pipe gamma enable and pipe csc enable for
	 * the pipe bottom color are configured via the primary plane.
	 * We have to reconfigure that even if the plane is inactive.
	 */
	return crtc_state->active_planes & BIT(plane->id) ||
		(DISPLAY_VER(i915) < 9 &&
		 plane->id == PLANE_PRIMARY);
}

static int
intel_color_add_affected_planes(struct intel_atomic_state *state,
				struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_plane *plane;

	if (!new_crtc_state->hw.active ||
	    intel_crtc_needs_modeset(new_crtc_state))
		return 0;

	if (new_crtc_state->gamma_enable == old_crtc_state->gamma_enable &&
	    new_crtc_state->csc_enable == old_crtc_state->csc_enable)
		return 0;

	for_each_intel_plane_on_crtc(&i915->drm, crtc, plane) {
		struct intel_plane_state *plane_state;

		if (!need_plane_update(plane, new_crtc_state))
			continue;

		plane_state = intel_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state))
			return PTR_ERR(plane_state);

		new_crtc_state->update_planes |= BIT(plane->id);
		new_crtc_state->async_flip_planes = 0;
		new_crtc_state->do_async_flip = false;

		/* plane control register changes blocked by CxSR */
		if (HAS_GMCH(i915))
			new_crtc_state->disable_cxsr = true;
	}

	return 0;
}

static u32 intel_gamma_lut_tests(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct drm_property_blob *gamma_lut = crtc_state->hw.gamma_lut;

	if (lut_is_legacy(gamma_lut))
		return 0;

	return DISPLAY_INFO(i915)->color.gamma_lut_tests;
}

static u32 intel_degamma_lut_tests(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	return DISPLAY_INFO(i915)->color.degamma_lut_tests;
}

static int intel_gamma_lut_size(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct drm_property_blob *gamma_lut = crtc_state->hw.gamma_lut;

	if (lut_is_legacy(gamma_lut))
		return LEGACY_LUT_LENGTH;

	return DISPLAY_INFO(i915)->color.gamma_lut_size;
}

static u32 intel_degamma_lut_size(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	return DISPLAY_INFO(i915)->color.degamma_lut_size;
}

static int check_lut_size(struct drm_i915_private *i915,
			  const struct drm_property_blob *lut, int expected)
{
	int len;

	if (!lut)
		return 0;

	len = drm_color_lut_size(lut);
	if (len != expected) {
		drm_dbg_kms(&i915->drm, "Invalid LUT size; got %d, expected %d\n",
			    len, expected);
		return -EINVAL;
	}

	return 0;
}

static int _check_luts(const struct intel_crtc_state *crtc_state,
		       u32 degamma_tests, u32 gamma_tests)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct drm_property_blob *gamma_lut = crtc_state->hw.gamma_lut;
	const struct drm_property_blob *degamma_lut = crtc_state->hw.degamma_lut;
	int gamma_length, degamma_length;

	/* C8 relies on its palette being stored in the legacy LUT */
	if (crtc_state->c8_planes && !lut_is_legacy(crtc_state->hw.gamma_lut)) {
		drm_dbg_kms(&i915->drm,
			    "C8 pixelformat requires the legacy LUT\n");
		return -EINVAL;
	}

	degamma_length = intel_degamma_lut_size(crtc_state);
	gamma_length = intel_gamma_lut_size(crtc_state);

	if (check_lut_size(i915, degamma_lut, degamma_length) ||
	    check_lut_size(i915, gamma_lut, gamma_length))
		return -EINVAL;

	if (drm_color_lut_check(degamma_lut, degamma_tests) ||
	    drm_color_lut_check(gamma_lut, gamma_tests))
		return -EINVAL;

	return 0;
}

static int check_luts(const struct intel_crtc_state *crtc_state)
{
	return _check_luts(crtc_state,
			   intel_degamma_lut_tests(crtc_state),
			   intel_gamma_lut_tests(crtc_state));
}

static u32 i9xx_gamma_mode(struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->gamma_enable ||
	    lut_is_legacy(crtc_state->hw.gamma_lut))
		return GAMMA_MODE_MODE_8BIT;
	else
		return GAMMA_MODE_MODE_10BIT;
}

static int i9xx_lut_10_diff(u16 a, u16 b)
{
	return drm_color_lut_extract(a, 10) -
		drm_color_lut_extract(b, 10);
}

static int i9xx_check_lut_10(struct drm_i915_private *dev_priv,
			     const struct drm_property_blob *blob)
{
	const struct drm_color_lut *lut = blob->data;
	int lut_size = drm_color_lut_size(blob);
	const struct drm_color_lut *a = &lut[lut_size - 2];
	const struct drm_color_lut *b = &lut[lut_size - 1];

	if (i9xx_lut_10_diff(b->red, a->red) > 0x7f ||
	    i9xx_lut_10_diff(b->green, a->green) > 0x7f ||
	    i9xx_lut_10_diff(b->blue, a->blue) > 0x7f) {
		drm_dbg_kms(&dev_priv->drm, "Last gamma LUT entry exceeds max slope\n");
		return -EINVAL;
	}

	return 0;
}

void intel_color_assert_luts(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	/* make sure {pre,post}_csc_lut were correctly assigned */
	if (DISPLAY_VER(i915) >= 11 || HAS_GMCH(i915)) {
		drm_WARN_ON(&i915->drm,
			    crtc_state->pre_csc_lut != crtc_state->hw.degamma_lut);
		drm_WARN_ON(&i915->drm,
			    crtc_state->post_csc_lut != crtc_state->hw.gamma_lut);
	} else if (DISPLAY_VER(i915) == 10) {
		drm_WARN_ON(&i915->drm,
			    crtc_state->post_csc_lut == crtc_state->hw.gamma_lut &&
			    crtc_state->pre_csc_lut != crtc_state->hw.degamma_lut &&
			    crtc_state->pre_csc_lut != i915->display.color.glk_linear_degamma_lut);
		drm_WARN_ON(&i915->drm,
			    !ilk_lut_limited_range(crtc_state) &&
			    crtc_state->post_csc_lut != NULL &&
			    crtc_state->post_csc_lut != crtc_state->hw.gamma_lut);
	} else if (crtc_state->gamma_mode != GAMMA_MODE_MODE_SPLIT) {
		drm_WARN_ON(&i915->drm,
			    crtc_state->pre_csc_lut != crtc_state->hw.degamma_lut &&
			    crtc_state->pre_csc_lut != crtc_state->hw.gamma_lut);
		drm_WARN_ON(&i915->drm,
			    !ilk_lut_limited_range(crtc_state) &&
			    crtc_state->post_csc_lut != crtc_state->hw.degamma_lut &&
			    crtc_state->post_csc_lut != crtc_state->hw.gamma_lut);
	}
}

static void intel_assign_luts(struct intel_crtc_state *crtc_state)
{
	drm_property_replace_blob(&crtc_state->pre_csc_lut,
				  crtc_state->hw.degamma_lut);
	drm_property_replace_blob(&crtc_state->post_csc_lut,
				  crtc_state->hw.gamma_lut);
}

static int i9xx_color_check(struct intel_atomic_state *state,
			    struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	ret = check_luts(crtc_state);
	if (ret)
		return ret;

	crtc_state->gamma_enable =
		crtc_state->hw.gamma_lut &&
		!crtc_state->c8_planes;

	crtc_state->gamma_mode = i9xx_gamma_mode(crtc_state);

	if (DISPLAY_VER(i915) < 4 &&
	    crtc_state->gamma_mode == GAMMA_MODE_MODE_10BIT) {
		ret = i9xx_check_lut_10(i915, crtc_state->hw.gamma_lut);
		if (ret)
			return ret;
	}

	ret = intel_color_add_affected_planes(state, crtc);
	if (ret)
		return ret;

	intel_assign_luts(crtc_state);

	crtc_state->preload_luts = intel_can_preload_luts(state, crtc);

	return 0;
}

/*
 * VLV color pipeline:
 * u0.10 -> WGC csc -> u0.10 -> pipe gamma -> u0.10
 */
static int vlv_color_check(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	ret = check_luts(crtc_state);
	if (ret)
		return ret;

	crtc_state->gamma_enable =
		crtc_state->hw.gamma_lut &&
		!crtc_state->c8_planes;

	crtc_state->gamma_mode = i9xx_gamma_mode(crtc_state);

	crtc_state->wgc_enable = crtc_state->hw.ctm;

	ret = intel_color_add_affected_planes(state, crtc);
	if (ret)
		return ret;

	intel_assign_luts(crtc_state);

	vlv_assign_csc(crtc_state);

	crtc_state->preload_luts = vlv_can_preload_luts(state, crtc);

	return 0;
}

static u32 chv_cgm_mode(const struct intel_crtc_state *crtc_state)
{
	u32 cgm_mode = 0;

	if (crtc_state->hw.degamma_lut)
		cgm_mode |= CGM_PIPE_MODE_DEGAMMA;
	if (crtc_state->hw.ctm)
		cgm_mode |= CGM_PIPE_MODE_CSC;
	if (crtc_state->hw.gamma_lut &&
	    !lut_is_legacy(crtc_state->hw.gamma_lut))
		cgm_mode |= CGM_PIPE_MODE_GAMMA;

	/*
	 * Toggling the CGM CSC on/off outside of the tiny window
	 * between start of vblank and frame start causes underruns.
	 * Always enable the CGM CSC as a workaround.
	 */
	cgm_mode |= CGM_PIPE_MODE_CSC;

	return cgm_mode;
}

/*
 * CHV color pipeline:
 * u0.10 -> CGM degamma -> u0.14 -> CGM csc -> u0.14 -> CGM gamma ->
 * u0.10 -> WGC csc -> u0.10 -> pipe gamma -> u0.10
 *
 * We always bypass the WGC csc and use the CGM csc
 * instead since it has degamma and better precision.
 */
static int chv_color_check(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	ret = check_luts(crtc_state);
	if (ret)
		return ret;

	/*
	 * Pipe gamma will be used only for the legacy LUT.
	 * Otherwise we bypass it and use the CGM gamma instead.
	 */
	crtc_state->gamma_enable =
		lut_is_legacy(crtc_state->hw.gamma_lut) &&
		!crtc_state->c8_planes;

	crtc_state->gamma_mode = GAMMA_MODE_MODE_8BIT;

	crtc_state->cgm_mode = chv_cgm_mode(crtc_state);

	/*
	 * We always bypass the WGC CSC and use the CGM CSC
	 * instead since it has degamma and better precision.
	 */
	crtc_state->wgc_enable = false;

	ret = intel_color_add_affected_planes(state, crtc);
	if (ret)
		return ret;

	intel_assign_luts(crtc_state);

	chv_assign_csc(crtc_state);

	crtc_state->preload_luts = chv_can_preload_luts(state, crtc);

	return 0;
}

static bool ilk_gamma_enable(const struct intel_crtc_state *crtc_state)
{
	return (crtc_state->hw.gamma_lut ||
		crtc_state->hw.degamma_lut) &&
		!crtc_state->c8_planes;
}

static bool ilk_csc_enable(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB ||
		ilk_csc_limited_range(crtc_state) ||
		crtc_state->hw.ctm;
}

static u32 ilk_gamma_mode(const struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->gamma_enable ||
	    lut_is_legacy(crtc_state->hw.gamma_lut))
		return GAMMA_MODE_MODE_8BIT;
	else
		return GAMMA_MODE_MODE_10BIT;
}

static u32 ilk_csc_mode(const struct intel_crtc_state *crtc_state)
{
	/*
	 * CSC comes after the LUT in RGB->YCbCr mode.
	 * RGB->YCbCr needs the limited range offsets added to
	 * the output. RGB limited range output is handled by
	 * the hw automagically elsewhere.
	 */
	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB)
		return CSC_BLACK_SCREEN_OFFSET;

	if (crtc_state->hw.degamma_lut)
		return CSC_MODE_YUV_TO_RGB;

	return CSC_MODE_YUV_TO_RGB |
		CSC_POSITION_BEFORE_GAMMA;
}

static int ilk_assign_luts(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (ilk_lut_limited_range(crtc_state)) {
		struct drm_property_blob *gamma_lut;

		gamma_lut = create_resized_lut(i915, crtc_state->hw.gamma_lut,
					       drm_color_lut_size(crtc_state->hw.gamma_lut),
					       true);
		if (IS_ERR(gamma_lut))
			return PTR_ERR(gamma_lut);

		drm_property_replace_blob(&crtc_state->post_csc_lut, gamma_lut);

		drm_property_blob_put(gamma_lut);

		drm_property_replace_blob(&crtc_state->pre_csc_lut, crtc_state->hw.degamma_lut);

		return 0;
	}

	if (crtc_state->hw.degamma_lut ||
	    crtc_state->csc_mode & CSC_POSITION_BEFORE_GAMMA) {
		drm_property_replace_blob(&crtc_state->pre_csc_lut,
					  crtc_state->hw.degamma_lut);
		drm_property_replace_blob(&crtc_state->post_csc_lut,
					  crtc_state->hw.gamma_lut);
	} else {
		drm_property_replace_blob(&crtc_state->pre_csc_lut,
					  crtc_state->hw.gamma_lut);
		drm_property_replace_blob(&crtc_state->post_csc_lut,
					  NULL);
	}

	return 0;
}

static int ilk_color_check(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	ret = check_luts(crtc_state);
	if (ret)
		return ret;

	if (crtc_state->hw.degamma_lut && crtc_state->hw.gamma_lut) {
		drm_dbg_kms(&i915->drm,
			    "Degamma and gamma together are not possible\n");
		return -EINVAL;
	}

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB &&
	    crtc_state->hw.ctm) {
		drm_dbg_kms(&i915->drm,
			    "YCbCr and CTM together are not possible\n");
		return -EINVAL;
	}

	crtc_state->gamma_enable = ilk_gamma_enable(crtc_state);

	crtc_state->csc_enable = ilk_csc_enable(crtc_state);

	crtc_state->gamma_mode = ilk_gamma_mode(crtc_state);

	crtc_state->csc_mode = ilk_csc_mode(crtc_state);

	ret = intel_color_add_affected_planes(state, crtc);
	if (ret)
		return ret;

	ret = ilk_assign_luts(crtc_state);
	if (ret)
		return ret;

	ilk_assign_csc(crtc_state);

	crtc_state->preload_luts = intel_can_preload_luts(state, crtc);

	return 0;
}

static u32 ivb_gamma_mode(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->hw.degamma_lut && crtc_state->hw.gamma_lut)
		return GAMMA_MODE_MODE_SPLIT;

	return ilk_gamma_mode(crtc_state);
}

static u32 ivb_csc_mode(const struct intel_crtc_state *crtc_state)
{
	bool limited_color_range = ilk_csc_limited_range(crtc_state);

	/*
	 * CSC comes after the LUT in degamma, RGB->YCbCr,
	 * and RGB full->limited range mode.
	 */
	if (crtc_state->hw.degamma_lut ||
	    crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB ||
	    limited_color_range)
		return 0;

	return CSC_POSITION_BEFORE_GAMMA;
}

static int ivb_assign_luts(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	struct drm_property_blob *degamma_lut, *gamma_lut;

	if (crtc_state->gamma_mode != GAMMA_MODE_MODE_SPLIT)
		return ilk_assign_luts(crtc_state);

	drm_WARN_ON(&i915->drm, drm_color_lut_size(crtc_state->hw.degamma_lut) != 1024);
	drm_WARN_ON(&i915->drm, drm_color_lut_size(crtc_state->hw.gamma_lut) != 1024);

	degamma_lut = create_resized_lut(i915, crtc_state->hw.degamma_lut, 512,
					 false);
	if (IS_ERR(degamma_lut))
		return PTR_ERR(degamma_lut);

	gamma_lut = create_resized_lut(i915, crtc_state->hw.gamma_lut, 512,
				       ilk_lut_limited_range(crtc_state));
	if (IS_ERR(gamma_lut)) {
		drm_property_blob_put(degamma_lut);
		return PTR_ERR(gamma_lut);
	}

	drm_property_replace_blob(&crtc_state->pre_csc_lut, degamma_lut);
	drm_property_replace_blob(&crtc_state->post_csc_lut, gamma_lut);

	drm_property_blob_put(degamma_lut);
	drm_property_blob_put(gamma_lut);

	return 0;
}

static int ivb_color_check(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	ret = check_luts(crtc_state);
	if (ret)
		return ret;

	if (crtc_state->c8_planes && crtc_state->hw.degamma_lut) {
		drm_dbg_kms(&i915->drm,
			    "C8 pixelformat and degamma together are not possible\n");
		return -EINVAL;
	}

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB &&
	    crtc_state->hw.ctm) {
		drm_dbg_kms(&i915->drm,
			    "YCbCr and CTM together are not possible\n");
		return -EINVAL;
	}

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB &&
	    crtc_state->hw.degamma_lut && crtc_state->hw.gamma_lut) {
		drm_dbg_kms(&i915->drm,
			    "YCbCr and degamma+gamma together are not possible\n");
		return -EINVAL;
	}

	crtc_state->gamma_enable = ilk_gamma_enable(crtc_state);

	crtc_state->csc_enable = ilk_csc_enable(crtc_state);

	crtc_state->gamma_mode = ivb_gamma_mode(crtc_state);

	crtc_state->csc_mode = ivb_csc_mode(crtc_state);

	ret = intel_color_add_affected_planes(state, crtc);
	if (ret)
		return ret;

	ret = ivb_assign_luts(crtc_state);
	if (ret)
		return ret;

	ilk_assign_csc(crtc_state);

	crtc_state->preload_luts = intel_can_preload_luts(state, crtc);

	return 0;
}

static u32 glk_gamma_mode(const struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->gamma_enable ||
	    lut_is_legacy(crtc_state->hw.gamma_lut))
		return GAMMA_MODE_MODE_8BIT;
	else
		return GAMMA_MODE_MODE_10BIT;
}

static bool glk_use_pre_csc_lut_for_gamma(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->hw.gamma_lut &&
		!crtc_state->c8_planes &&
		crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB;
}

static int glk_assign_luts(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (glk_use_pre_csc_lut_for_gamma(crtc_state)) {
		struct drm_property_blob *gamma_lut;

		gamma_lut = create_resized_lut(i915, crtc_state->hw.gamma_lut,
					       DISPLAY_INFO(i915)->color.degamma_lut_size,
					       false);
		if (IS_ERR(gamma_lut))
			return PTR_ERR(gamma_lut);

		drm_property_replace_blob(&crtc_state->pre_csc_lut, gamma_lut);
		drm_property_replace_blob(&crtc_state->post_csc_lut, NULL);

		drm_property_blob_put(gamma_lut);

		return 0;
	}

	if (ilk_lut_limited_range(crtc_state)) {
		struct drm_property_blob *gamma_lut;

		gamma_lut = create_resized_lut(i915, crtc_state->hw.gamma_lut,
					       drm_color_lut_size(crtc_state->hw.gamma_lut),
					       true);
		if (IS_ERR(gamma_lut))
			return PTR_ERR(gamma_lut);

		drm_property_replace_blob(&crtc_state->post_csc_lut, gamma_lut);

		drm_property_blob_put(gamma_lut);
	} else {
		drm_property_replace_blob(&crtc_state->post_csc_lut, crtc_state->hw.gamma_lut);
	}

	drm_property_replace_blob(&crtc_state->pre_csc_lut, crtc_state->hw.degamma_lut);

	/*
	 * On GLK+ both pipe CSC and degamma LUT are controlled
	 * by csc_enable. Hence for the cases where the CSC is
	 * needed but degamma LUT is not we need to load a
	 * linear degamma LUT.
	 */
	if (crtc_state->csc_enable && !crtc_state->pre_csc_lut)
		drm_property_replace_blob(&crtc_state->pre_csc_lut,
					  i915->display.color.glk_linear_degamma_lut);

	return 0;
}

static int glk_check_luts(const struct intel_crtc_state *crtc_state)
{
	u32 degamma_tests = intel_degamma_lut_tests(crtc_state);
	u32 gamma_tests = intel_gamma_lut_tests(crtc_state);

	if (glk_use_pre_csc_lut_for_gamma(crtc_state))
		gamma_tests |= degamma_tests;

	return _check_luts(crtc_state, degamma_tests, gamma_tests);
}

static int glk_color_check(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	ret = glk_check_luts(crtc_state);
	if (ret)
		return ret;

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB &&
	    crtc_state->hw.ctm) {
		drm_dbg_kms(&i915->drm,
			    "YCbCr and CTM together are not possible\n");
		return -EINVAL;
	}

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB &&
	    crtc_state->hw.degamma_lut && crtc_state->hw.gamma_lut) {
		drm_dbg_kms(&i915->drm,
			    "YCbCr and degamma+gamma together are not possible\n");
		return -EINVAL;
	}

	crtc_state->gamma_enable =
		!glk_use_pre_csc_lut_for_gamma(crtc_state) &&
		crtc_state->hw.gamma_lut &&
		!crtc_state->c8_planes;

	/* On GLK+ degamma LUT is controlled by csc_enable */
	crtc_state->csc_enable =
		glk_use_pre_csc_lut_for_gamma(crtc_state) ||
		crtc_state->hw.degamma_lut ||
		crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB ||
		crtc_state->hw.ctm || ilk_csc_limited_range(crtc_state);

	crtc_state->gamma_mode = glk_gamma_mode(crtc_state);

	crtc_state->csc_mode = 0;

	ret = intel_color_add_affected_planes(state, crtc);
	if (ret)
		return ret;

	ret = glk_assign_luts(crtc_state);
	if (ret)
		return ret;

	ilk_assign_csc(crtc_state);

	crtc_state->preload_luts = intel_can_preload_luts(state, crtc);

	return 0;
}

static u32 icl_gamma_mode(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	u32 gamma_mode = 0;

	if (crtc_state->hw.degamma_lut)
		gamma_mode |= PRE_CSC_GAMMA_ENABLE;

	if (crtc_state->hw.gamma_lut &&
	    !crtc_state->c8_planes)
		gamma_mode |= POST_CSC_GAMMA_ENABLE;

	if (!crtc_state->hw.gamma_lut ||
	    lut_is_legacy(crtc_state->hw.gamma_lut))
		gamma_mode |= GAMMA_MODE_MODE_8BIT;
	/*
	 * Enable 10bit gamma for D13
	 * ToDo: Extend to Logarithmic Gamma once the new UAPI
	 * is accepted and implemented by a userspace consumer
	 */
	else if (DISPLAY_VER(i915) >= 13)
		gamma_mode |= GAMMA_MODE_MODE_10BIT;
	else
		gamma_mode |= GAMMA_MODE_MODE_12BIT_MULTI_SEG;

	return gamma_mode;
}

static u32 icl_csc_mode(const struct intel_crtc_state *crtc_state)
{
	u32 csc_mode = 0;

	if (crtc_state->hw.ctm)
		csc_mode |= ICL_CSC_ENABLE;

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB ||
	    crtc_state->limited_color_range)
		csc_mode |= ICL_OUTPUT_CSC_ENABLE;

	return csc_mode;
}

static int icl_color_check(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	int ret;

	ret = check_luts(crtc_state);
	if (ret)
		return ret;

	crtc_state->gamma_mode = icl_gamma_mode(crtc_state);

	crtc_state->csc_mode = icl_csc_mode(crtc_state);

	intel_assign_luts(crtc_state);

	icl_assign_csc(crtc_state);

	crtc_state->preload_luts = intel_can_preload_luts(state, crtc);

	return 0;
}

static int i9xx_post_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->gamma_enable && !crtc_state->c8_planes)
		return 0;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		return 8;
	case GAMMA_MODE_MODE_10BIT:
		return 10;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		return 0;
	}
}

static int i9xx_pre_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	return 0;
}

static int i965_post_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->gamma_enable && !crtc_state->c8_planes)
		return 0;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		return 8;
	case GAMMA_MODE_MODE_10BIT:
		return 16;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		return 0;
	}
}

static int ilk_gamma_mode_precision(u32 gamma_mode)
{
	switch (gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		return 8;
	case GAMMA_MODE_MODE_10BIT:
		return 10;
	default:
		MISSING_CASE(gamma_mode);
		return 0;
	}
}

static bool ilk_has_post_csc_lut(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->c8_planes)
		return true;

	return crtc_state->gamma_enable &&
		(crtc_state->csc_mode & CSC_POSITION_BEFORE_GAMMA) != 0;
}

static bool ilk_has_pre_csc_lut(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->gamma_enable &&
		(crtc_state->csc_mode & CSC_POSITION_BEFORE_GAMMA) == 0;
}

static int ilk_post_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (!ilk_has_post_csc_lut(crtc_state))
		return 0;

	return ilk_gamma_mode_precision(crtc_state->gamma_mode);
}

static int ilk_pre_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (!ilk_has_pre_csc_lut(crtc_state))
		return 0;

	return ilk_gamma_mode_precision(crtc_state->gamma_mode);
}

static int ivb_post_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->gamma_enable &&
	    crtc_state->gamma_mode == GAMMA_MODE_MODE_SPLIT)
		return 10;

	return ilk_post_csc_lut_precision(crtc_state);
}

static int ivb_pre_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->gamma_enable &&
	    crtc_state->gamma_mode == GAMMA_MODE_MODE_SPLIT)
		return 10;

	return ilk_pre_csc_lut_precision(crtc_state);
}

static int chv_post_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->cgm_mode & CGM_PIPE_MODE_GAMMA)
		return 10;

	return i965_post_csc_lut_precision(crtc_state);
}

static int chv_pre_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->cgm_mode & CGM_PIPE_MODE_DEGAMMA)
		return 14;

	return 0;
}

static int glk_post_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->gamma_enable && !crtc_state->c8_planes)
		return 0;

	return ilk_gamma_mode_precision(crtc_state->gamma_mode);
}

static int glk_pre_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->csc_enable)
		return 0;

	return 16;
}

static bool icl_has_post_csc_lut(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->c8_planes)
		return true;

	return crtc_state->gamma_mode & POST_CSC_GAMMA_ENABLE;
}

static bool icl_has_pre_csc_lut(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->gamma_mode & PRE_CSC_GAMMA_ENABLE;
}

static int icl_post_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (!icl_has_post_csc_lut(crtc_state))
		return 0;

	switch (crtc_state->gamma_mode & GAMMA_MODE_MODE_MASK) {
	case GAMMA_MODE_MODE_8BIT:
		return 8;
	case GAMMA_MODE_MODE_10BIT:
		return 10;
	case GAMMA_MODE_MODE_12BIT_MULTI_SEG:
		return 16;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		return 0;
	}
}

static int icl_pre_csc_lut_precision(const struct intel_crtc_state *crtc_state)
{
	if (!icl_has_pre_csc_lut(crtc_state))
		return 0;

	return 16;
}

static bool err_check(const struct drm_color_lut *lut1,
		      const struct drm_color_lut *lut2, u32 err)
{
	return ((abs((long)lut2->red - lut1->red)) <= err) &&
		((abs((long)lut2->blue - lut1->blue)) <= err) &&
		((abs((long)lut2->green - lut1->green)) <= err);
}

static bool intel_lut_entries_equal(const struct drm_color_lut *lut1,
				    const struct drm_color_lut *lut2,
				    int lut_size, u32 err)
{
	int i;

	for (i = 0; i < lut_size; i++) {
		if (!err_check(&lut1[i], &lut2[i], err))
			return false;
	}

	return true;
}

static bool intel_lut_equal(const struct drm_property_blob *blob1,
			    const struct drm_property_blob *blob2,
			    int check_size, int precision)
{
	const struct drm_color_lut *lut1, *lut2;
	int lut_size1, lut_size2;
	u32 err;

	if (!blob1 != !blob2)
		return false;

	if (!blob1 != !precision)
		return false;

	if (!blob1)
		return true;

	lut_size1 = drm_color_lut_size(blob1);
	lut_size2 = drm_color_lut_size(blob2);

	if (lut_size1 != lut_size2)
		return false;

	if (check_size > lut_size1)
		return false;

	lut1 = blob1->data;
	lut2 = blob2->data;

	err = 0xffff >> precision;

	if (!check_size)
		check_size = lut_size1;

	return intel_lut_entries_equal(lut1, lut2, check_size, err);
}

static bool i9xx_lut_equal(const struct intel_crtc_state *crtc_state,
			   const struct drm_property_blob *blob1,
			   const struct drm_property_blob *blob2,
			   bool is_pre_csc_lut)
{
	int check_size = 0;

	if (is_pre_csc_lut)
		return intel_lut_equal(blob1, blob2, 0,
				       i9xx_pre_csc_lut_precision(crtc_state));

	/* 10bit mode last entry is implicit, just skip it */
	if (crtc_state->gamma_mode == GAMMA_MODE_MODE_10BIT)
		check_size = 128;

	return intel_lut_equal(blob1, blob2, check_size,
			       i9xx_post_csc_lut_precision(crtc_state));
}

static bool i965_lut_equal(const struct intel_crtc_state *crtc_state,
			   const struct drm_property_blob *blob1,
			   const struct drm_property_blob *blob2,
			   bool is_pre_csc_lut)
{
	if (is_pre_csc_lut)
		return intel_lut_equal(blob1, blob2, 0,
				       i9xx_pre_csc_lut_precision(crtc_state));
	else
		return intel_lut_equal(blob1, blob2, 0,
				       i965_post_csc_lut_precision(crtc_state));
}

static bool chv_lut_equal(const struct intel_crtc_state *crtc_state,
			  const struct drm_property_blob *blob1,
			  const struct drm_property_blob *blob2,
			  bool is_pre_csc_lut)
{
	if (is_pre_csc_lut)
		return intel_lut_equal(blob1, blob2, 0,
				       chv_pre_csc_lut_precision(crtc_state));
	else
		return intel_lut_equal(blob1, blob2, 0,
				       chv_post_csc_lut_precision(crtc_state));
}

static bool ilk_lut_equal(const struct intel_crtc_state *crtc_state,
			  const struct drm_property_blob *blob1,
			  const struct drm_property_blob *blob2,
			  bool is_pre_csc_lut)
{
	if (is_pre_csc_lut)
		return intel_lut_equal(blob1, blob2, 0,
				       ilk_pre_csc_lut_precision(crtc_state));
	else
		return intel_lut_equal(blob1, blob2, 0,
				       ilk_post_csc_lut_precision(crtc_state));
}

static bool ivb_lut_equal(const struct intel_crtc_state *crtc_state,
			  const struct drm_property_blob *blob1,
			  const struct drm_property_blob *blob2,
			  bool is_pre_csc_lut)
{
	if (is_pre_csc_lut)
		return intel_lut_equal(blob1, blob2, 0,
				       ivb_pre_csc_lut_precision(crtc_state));
	else
		return intel_lut_equal(blob1, blob2, 0,
				       ivb_post_csc_lut_precision(crtc_state));
}

static bool glk_lut_equal(const struct intel_crtc_state *crtc_state,
			  const struct drm_property_blob *blob1,
			  const struct drm_property_blob *blob2,
			  bool is_pre_csc_lut)
{
	if (is_pre_csc_lut)
		return intel_lut_equal(blob1, blob2, 0,
				       glk_pre_csc_lut_precision(crtc_state));
	else
		return intel_lut_equal(blob1, blob2, 0,
				       glk_post_csc_lut_precision(crtc_state));
}

static bool icl_lut_equal(const struct intel_crtc_state *crtc_state,
			  const struct drm_property_blob *blob1,
			  const struct drm_property_blob *blob2,
			  bool is_pre_csc_lut)
{
	int check_size = 0;

	if (is_pre_csc_lut)
		return intel_lut_equal(blob1, blob2, 0,
				       icl_pre_csc_lut_precision(crtc_state));

	/* hw readout broken except for the super fine segment :( */
	if ((crtc_state->gamma_mode & GAMMA_MODE_MODE_MASK) ==
	    GAMMA_MODE_MODE_12BIT_MULTI_SEG)
		check_size = 9;

	return intel_lut_equal(blob1, blob2, check_size,
			       icl_post_csc_lut_precision(crtc_state));
}

static struct drm_property_blob *i9xx_read_lut_8(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;
	int i;

	blob = drm_property_create_blob(&dev_priv->drm,
					sizeof(lut[0]) * LEGACY_LUT_LENGTH,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	for (i = 0; i < LEGACY_LUT_LENGTH; i++) {
		u32 val = intel_de_read_fw(dev_priv,
					   PALETTE(dev_priv, pipe, i));

		i9xx_lut_8_pack(&lut[i], val);
	}

	return blob;
}

static struct drm_property_blob *i9xx_read_lut_10(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 lut_size = DISPLAY_INFO(dev_priv)->color.gamma_lut_size;
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;
	u32 ldw, udw;
	int i;

	blob = drm_property_create_blob(&dev_priv->drm,
					lut_size * sizeof(lut[0]), NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	for (i = 0; i < lut_size - 1; i++) {
		ldw = intel_de_read_fw(dev_priv,
				       PALETTE(dev_priv, pipe, 2 * i + 0));
		udw = intel_de_read_fw(dev_priv,
				       PALETTE(dev_priv, pipe, 2 * i + 1));

		i9xx_lut_10_pack(&lut[i], ldw, udw);
	}

	i9xx_lut_10_pack_slope(&lut[i], ldw, udw);

	return blob;
}

static void i9xx_read_luts(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (!crtc_state->gamma_enable && !crtc_state->c8_planes)
		return;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		crtc_state->post_csc_lut = i9xx_read_lut_8(crtc);
		break;
	case GAMMA_MODE_MODE_10BIT:
		crtc_state->post_csc_lut = i9xx_read_lut_10(crtc);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static struct drm_property_blob *i965_read_lut_10p6(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	int i, lut_size = DISPLAY_INFO(dev_priv)->color.gamma_lut_size;
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	blob = drm_property_create_blob(&dev_priv->drm,
					sizeof(lut[0]) * lut_size,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	for (i = 0; i < lut_size - 1; i++) {
		u32 ldw = intel_de_read_fw(dev_priv,
					   PALETTE(dev_priv, pipe, 2 * i + 0));
		u32 udw = intel_de_read_fw(dev_priv,
					   PALETTE(dev_priv, pipe, 2 * i + 1));

		i965_lut_10p6_pack(&lut[i], ldw, udw);
	}

	lut[i].red = i965_lut_11p6_max_pack(intel_de_read_fw(dev_priv, PIPEGCMAX(dev_priv, pipe, 0)));
	lut[i].green = i965_lut_11p6_max_pack(intel_de_read_fw(dev_priv, PIPEGCMAX(dev_priv, pipe, 1)));
	lut[i].blue = i965_lut_11p6_max_pack(intel_de_read_fw(dev_priv, PIPEGCMAX(dev_priv, pipe, 2)));

	return blob;
}

static void i965_read_luts(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (!crtc_state->gamma_enable && !crtc_state->c8_planes)
		return;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		crtc_state->post_csc_lut = i9xx_read_lut_8(crtc);
		break;
	case GAMMA_MODE_MODE_10BIT:
		crtc_state->post_csc_lut = i965_read_lut_10p6(crtc);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static struct drm_property_blob *chv_read_cgm_degamma(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	int i, lut_size = DISPLAY_INFO(dev_priv)->color.degamma_lut_size;
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	blob = drm_property_create_blob(&dev_priv->drm,
					sizeof(lut[0]) * lut_size,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	for (i = 0; i < lut_size; i++) {
		u32 ldw = intel_de_read_fw(dev_priv, CGM_PIPE_DEGAMMA(pipe, i, 0));
		u32 udw = intel_de_read_fw(dev_priv, CGM_PIPE_DEGAMMA(pipe, i, 1));

		chv_cgm_degamma_pack(&lut[i], ldw, udw);
	}

	return blob;
}

static struct drm_property_blob *chv_read_cgm_gamma(struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	int i, lut_size = DISPLAY_INFO(i915)->color.gamma_lut_size;
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	blob = drm_property_create_blob(&i915->drm,
					sizeof(lut[0]) * lut_size,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	for (i = 0; i < lut_size; i++) {
		u32 ldw = intel_de_read_fw(i915, CGM_PIPE_GAMMA(pipe, i, 0));
		u32 udw = intel_de_read_fw(i915, CGM_PIPE_GAMMA(pipe, i, 1));

		chv_cgm_gamma_pack(&lut[i], ldw, udw);
	}

	return blob;
}

static void chv_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	crtc_state->cgm_mode = intel_de_read(i915, CGM_PIPE_MODE(crtc->pipe));

	i9xx_get_config(crtc_state);
}

static void chv_read_luts(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->cgm_mode & CGM_PIPE_MODE_DEGAMMA)
		crtc_state->pre_csc_lut = chv_read_cgm_degamma(crtc);

	if (crtc_state->cgm_mode & CGM_PIPE_MODE_GAMMA)
		crtc_state->post_csc_lut = chv_read_cgm_gamma(crtc);
	else
		i965_read_luts(crtc_state);
}

static struct drm_property_blob *ilk_read_lut_8(struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;
	int i;

	blob = drm_property_create_blob(&i915->drm,
					sizeof(lut[0]) * LEGACY_LUT_LENGTH,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	for (i = 0; i < LEGACY_LUT_LENGTH; i++) {
		u32 val = intel_de_read_fw(i915, LGC_PALETTE(pipe, i));

		i9xx_lut_8_pack(&lut[i], val);
	}

	return blob;
}

static struct drm_property_blob *ilk_read_lut_10(struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	int i, lut_size = DISPLAY_INFO(i915)->color.gamma_lut_size;
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	blob = drm_property_create_blob(&i915->drm,
					sizeof(lut[0]) * lut_size,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	for (i = 0; i < lut_size; i++) {
		u32 val = intel_de_read_fw(i915, PREC_PALETTE(pipe, i));

		ilk_lut_10_pack(&lut[i], val);
	}

	return blob;
}

static void ilk_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	crtc_state->csc_mode = ilk_read_csc_mode(crtc);

	i9xx_get_config(crtc_state);
}

static void ilk_read_luts(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_property_blob **blob =
		ilk_has_post_csc_lut(crtc_state) ?
		&crtc_state->post_csc_lut : &crtc_state->pre_csc_lut;

	if (!crtc_state->gamma_enable && !crtc_state->c8_planes)
		return;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		*blob = ilk_read_lut_8(crtc);
		break;
	case GAMMA_MODE_MODE_10BIT:
		*blob = ilk_read_lut_10(crtc);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

/*
 * IVB/HSW Bspec / PAL_PREC_INDEX:
 * "Restriction : Index auto increment mode is not
 *  supported and must not be enabled."
 */
static struct drm_property_blob *ivb_read_lut_10(struct intel_crtc *crtc,
						 u32 prec_index)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	int i, lut_size = ivb_lut_10_size(prec_index);
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	blob = drm_property_create_blob(&dev_priv->drm,
					sizeof(lut[0]) * lut_size,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	for (i = 0; i < lut_size; i++) {
		u32 val;

		intel_de_write_fw(dev_priv, PREC_PAL_INDEX(pipe),
				  prec_index + i);
		val = intel_de_read_fw(dev_priv, PREC_PAL_DATA(pipe));

		ilk_lut_10_pack(&lut[i], val);
	}

	intel_de_write_fw(dev_priv, PREC_PAL_INDEX(pipe),
			  PAL_PREC_INDEX_VALUE(0));

	return blob;
}

static void ivb_read_luts(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_property_blob **blob =
		ilk_has_post_csc_lut(crtc_state) ?
		&crtc_state->post_csc_lut : &crtc_state->pre_csc_lut;

	if (!crtc_state->gamma_enable && !crtc_state->c8_planes)
		return;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		*blob = ilk_read_lut_8(crtc);
		break;
	case GAMMA_MODE_MODE_SPLIT:
		crtc_state->pre_csc_lut =
			ivb_read_lut_10(crtc, PAL_PREC_SPLIT_MODE |
					PAL_PREC_INDEX_VALUE(0));
		crtc_state->post_csc_lut =
			ivb_read_lut_10(crtc, PAL_PREC_SPLIT_MODE |
					PAL_PREC_INDEX_VALUE(512));
		break;
	case GAMMA_MODE_MODE_10BIT:
		*blob = ivb_read_lut_10(crtc, PAL_PREC_INDEX_VALUE(0));
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

/* On BDW+ the index auto increment mode actually works */
static struct drm_property_blob *bdw_read_lut_10(struct intel_crtc *crtc,
						 u32 prec_index)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	int i, lut_size = ivb_lut_10_size(prec_index);
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	blob = drm_property_create_blob(&i915->drm,
					sizeof(lut[0]) * lut_size,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	intel_de_write_fw(i915, PREC_PAL_INDEX(pipe),
			  prec_index);
	intel_de_write_fw(i915, PREC_PAL_INDEX(pipe),
			  PAL_PREC_AUTO_INCREMENT |
			  prec_index);

	for (i = 0; i < lut_size; i++) {
		u32 val = intel_de_read_fw(i915, PREC_PAL_DATA(pipe));

		ilk_lut_10_pack(&lut[i], val);
	}

	intel_de_write_fw(i915, PREC_PAL_INDEX(pipe),
			  PAL_PREC_INDEX_VALUE(0));

	return blob;
}

static void bdw_read_luts(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_property_blob **blob =
		ilk_has_post_csc_lut(crtc_state) ?
		&crtc_state->post_csc_lut : &crtc_state->pre_csc_lut;

	if (!crtc_state->gamma_enable && !crtc_state->c8_planes)
		return;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		*blob = ilk_read_lut_8(crtc);
		break;
	case GAMMA_MODE_MODE_SPLIT:
		crtc_state->pre_csc_lut =
			bdw_read_lut_10(crtc, PAL_PREC_SPLIT_MODE |
					PAL_PREC_INDEX_VALUE(0));
		crtc_state->post_csc_lut =
			bdw_read_lut_10(crtc, PAL_PREC_SPLIT_MODE |
					PAL_PREC_INDEX_VALUE(512));
		break;
	case GAMMA_MODE_MODE_10BIT:
		*blob = bdw_read_lut_10(crtc, PAL_PREC_INDEX_VALUE(0));
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static struct drm_property_blob *glk_read_degamma_lut(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	int i, lut_size = DISPLAY_INFO(dev_priv)->color.degamma_lut_size;
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	blob = drm_property_create_blob(&dev_priv->drm,
					sizeof(lut[0]) * lut_size,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	/*
	 * When setting the auto-increment bit, the hardware seems to
	 * ignore the index bits, so we need to reset it to index 0
	 * separately.
	 */
	intel_de_write_fw(dev_priv, PRE_CSC_GAMC_INDEX(pipe),
			  PRE_CSC_GAMC_INDEX_VALUE(0));
	intel_de_write_fw(dev_priv, PRE_CSC_GAMC_INDEX(pipe),
			  PRE_CSC_GAMC_AUTO_INCREMENT |
			  PRE_CSC_GAMC_INDEX_VALUE(0));

	for (i = 0; i < lut_size; i++) {
		u32 val = intel_de_read_fw(dev_priv, PRE_CSC_GAMC_DATA(pipe));

		if (DISPLAY_VER(dev_priv) >= 14)
			mtl_degamma_lut_pack(&lut[i], val);
		else
			glk_degamma_lut_pack(&lut[i], val);
	}

	intel_de_write_fw(dev_priv, PRE_CSC_GAMC_INDEX(pipe),
			  PRE_CSC_GAMC_INDEX_VALUE(0));

	return blob;
}

static void glk_read_luts(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->csc_enable)
		crtc_state->pre_csc_lut = glk_read_degamma_lut(crtc);

	if (!crtc_state->gamma_enable && !crtc_state->c8_planes)
		return;

	switch (crtc_state->gamma_mode) {
	case GAMMA_MODE_MODE_8BIT:
		crtc_state->post_csc_lut = ilk_read_lut_8(crtc);
		break;
	case GAMMA_MODE_MODE_10BIT:
		crtc_state->post_csc_lut = bdw_read_lut_10(crtc, PAL_PREC_INDEX_VALUE(0));
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static struct drm_property_blob *
icl_read_lut_multi_segment(struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	int i, lut_size = DISPLAY_INFO(i915)->color.gamma_lut_size;
	enum pipe pipe = crtc->pipe;
	struct drm_property_blob *blob;
	struct drm_color_lut *lut;

	blob = drm_property_create_blob(&i915->drm,
					sizeof(lut[0]) * lut_size,
					NULL);
	if (IS_ERR(blob))
		return NULL;

	lut = blob->data;

	intel_de_write_fw(i915, PREC_PAL_MULTI_SEG_INDEX(pipe),
			  PAL_PREC_MULTI_SEG_INDEX_VALUE(0));
	intel_de_write_fw(i915, PREC_PAL_MULTI_SEG_INDEX(pipe),
			  PAL_PREC_MULTI_SEG_AUTO_INCREMENT |
			  PAL_PREC_MULTI_SEG_INDEX_VALUE(0));

	for (i = 0; i < 9; i++) {
		u32 ldw = intel_de_read_fw(i915, PREC_PAL_MULTI_SEG_DATA(pipe));
		u32 udw = intel_de_read_fw(i915, PREC_PAL_MULTI_SEG_DATA(pipe));

		ilk_lut_12p4_pack(&lut[i], ldw, udw);
	}

	intel_de_write_fw(i915, PREC_PAL_MULTI_SEG_INDEX(pipe),
			  PAL_PREC_MULTI_SEG_INDEX_VALUE(0));

	/*
	 * FIXME readouts from PAL_PREC_DATA register aren't giving
	 * correct values in the case of fine and coarse segments.
	 * Restricting readouts only for super fine segment as of now.
	 */

	return blob;
}

static void icl_read_luts(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (icl_has_pre_csc_lut(crtc_state))
		crtc_state->pre_csc_lut = glk_read_degamma_lut(crtc);

	if (!icl_has_post_csc_lut(crtc_state))
		return;

	switch (crtc_state->gamma_mode & GAMMA_MODE_MODE_MASK) {
	case GAMMA_MODE_MODE_8BIT:
		crtc_state->post_csc_lut = ilk_read_lut_8(crtc);
		break;
	case GAMMA_MODE_MODE_10BIT:
		crtc_state->post_csc_lut = bdw_read_lut_10(crtc, PAL_PREC_INDEX_VALUE(0));
		break;
	case GAMMA_MODE_MODE_12BIT_MULTI_SEG:
		crtc_state->post_csc_lut = icl_read_lut_multi_segment(crtc);
		break;
	default:
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static const struct intel_color_funcs chv_color_funcs = {
	.color_check = chv_color_check,
	.color_commit_arm = i9xx_color_commit_arm,
	.load_luts = chv_load_luts,
	.read_luts = chv_read_luts,
	.lut_equal = chv_lut_equal,
	.read_csc = chv_read_csc,
	.get_config = chv_get_config,
};

static const struct intel_color_funcs vlv_color_funcs = {
	.color_check = vlv_color_check,
	.color_commit_arm = i9xx_color_commit_arm,
	.load_luts = vlv_load_luts,
	.read_luts = i965_read_luts,
	.lut_equal = i965_lut_equal,
	.read_csc = vlv_read_csc,
	.get_config = i9xx_get_config,
};

static const struct intel_color_funcs i965_color_funcs = {
	.color_check = i9xx_color_check,
	.color_commit_arm = i9xx_color_commit_arm,
	.load_luts = i965_load_luts,
	.read_luts = i965_read_luts,
	.lut_equal = i965_lut_equal,
	.get_config = i9xx_get_config,
};

static const struct intel_color_funcs i9xx_color_funcs = {
	.color_check = i9xx_color_check,
	.color_commit_arm = i9xx_color_commit_arm,
	.load_luts = i9xx_load_luts,
	.read_luts = i9xx_read_luts,
	.lut_equal = i9xx_lut_equal,
	.get_config = i9xx_get_config,
};

static const struct intel_color_funcs tgl_color_funcs = {
	.color_check = icl_color_check,
	.color_commit_noarm = icl_color_commit_noarm,
	.color_commit_arm = icl_color_commit_arm,
	.load_luts = icl_load_luts,
	.read_luts = icl_read_luts,
	.lut_equal = icl_lut_equal,
	.read_csc = icl_read_csc,
	.get_config = skl_get_config,
};

static const struct intel_color_funcs icl_color_funcs = {
	.color_check = icl_color_check,
	.color_commit_noarm = icl_color_commit_noarm,
	.color_commit_arm = icl_color_commit_arm,
	.color_post_update = icl_color_post_update,
	.load_luts = icl_load_luts,
	.read_luts = icl_read_luts,
	.lut_equal = icl_lut_equal,
	.read_csc = icl_read_csc,
	.get_config = skl_get_config,
};

static const struct intel_color_funcs glk_color_funcs = {
	.color_check = glk_color_check,
	.color_commit_noarm = skl_color_commit_noarm,
	.color_commit_arm = skl_color_commit_arm,
	.load_luts = glk_load_luts,
	.read_luts = glk_read_luts,
	.lut_equal = glk_lut_equal,
	.read_csc = skl_read_csc,
	.get_config = skl_get_config,
};

static const struct intel_color_funcs skl_color_funcs = {
	.color_check = ivb_color_check,
	.color_commit_noarm = skl_color_commit_noarm,
	.color_commit_arm = skl_color_commit_arm,
	.load_luts = bdw_load_luts,
	.read_luts = bdw_read_luts,
	.lut_equal = ivb_lut_equal,
	.read_csc = skl_read_csc,
	.get_config = skl_get_config,
};

static const struct intel_color_funcs bdw_color_funcs = {
	.color_check = ivb_color_check,
	.color_commit_noarm = ilk_color_commit_noarm,
	.color_commit_arm = hsw_color_commit_arm,
	.load_luts = bdw_load_luts,
	.read_luts = bdw_read_luts,
	.lut_equal = ivb_lut_equal,
	.read_csc = ilk_read_csc,
	.get_config = hsw_get_config,
};

static const struct intel_color_funcs hsw_color_funcs = {
	.color_check = ivb_color_check,
	.color_commit_noarm = ilk_color_commit_noarm,
	.color_commit_arm = hsw_color_commit_arm,
	.load_luts = ivb_load_luts,
	.read_luts = ivb_read_luts,
	.lut_equal = ivb_lut_equal,
	.read_csc = ilk_read_csc,
	.get_config = hsw_get_config,
};

static const struct intel_color_funcs ivb_color_funcs = {
	.color_check = ivb_color_check,
	.color_commit_noarm = ilk_color_commit_noarm,
	.color_commit_arm = ilk_color_commit_arm,
	.load_luts = ivb_load_luts,
	.read_luts = ivb_read_luts,
	.lut_equal = ivb_lut_equal,
	.read_csc = ilk_read_csc,
	.get_config = ilk_get_config,
};

static const struct intel_color_funcs ilk_color_funcs = {
	.color_check = ilk_color_check,
	.color_commit_noarm = ilk_color_commit_noarm,
	.color_commit_arm = ilk_color_commit_arm,
	.load_luts = ilk_load_luts,
	.read_luts = ilk_read_luts,
	.lut_equal = ilk_lut_equal,
	.read_csc = ilk_read_csc,
	.get_config = ilk_get_config,
};

void intel_color_crtc_init(struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	int degamma_lut_size, gamma_lut_size;
	bool has_ctm;

	drm_mode_crtc_set_gamma_size(&crtc->base, 256);

	gamma_lut_size = DISPLAY_INFO(i915)->color.gamma_lut_size;
	degamma_lut_size = DISPLAY_INFO(i915)->color.degamma_lut_size;
	has_ctm = DISPLAY_VER(i915) >= 5;

	/*
	 * "DPALETTE_A: NOTE: The 8-bit (non-10-bit) mode is the
	 *  only mode supported by Alviso and Grantsdale."
	 *
	 * Actually looks like this affects all of gen3.
	 * Confirmed on alv,cst,pnv. Mobile gen2 parts (alm,mgm)
	 * are confirmed not to suffer from this restriction.
	 */
	if (DISPLAY_VER(i915) == 3 && crtc->pipe == PIPE_A)
		gamma_lut_size = 256;

	drm_crtc_enable_color_mgmt(&crtc->base, degamma_lut_size,
				   has_ctm, gamma_lut_size);
}

int intel_color_init(struct drm_i915_private *i915)
{
	struct drm_property_blob *blob;

	if (DISPLAY_VER(i915) != 10)
		return 0;

	blob = create_linear_lut(i915,
				 DISPLAY_INFO(i915)->color.degamma_lut_size);
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	i915->display.color.glk_linear_degamma_lut = blob;

	return 0;
}

void intel_color_init_hooks(struct drm_i915_private *i915)
{
	if (HAS_GMCH(i915)) {
		if (IS_CHERRYVIEW(i915))
			i915->display.funcs.color = &chv_color_funcs;
		else if (IS_VALLEYVIEW(i915))
			i915->display.funcs.color = &vlv_color_funcs;
		else if (DISPLAY_VER(i915) >= 4)
			i915->display.funcs.color = &i965_color_funcs;
		else
			i915->display.funcs.color = &i9xx_color_funcs;
	} else {
		if (DISPLAY_VER(i915) >= 12)
			i915->display.funcs.color = &tgl_color_funcs;
		else if (DISPLAY_VER(i915) == 11)
			i915->display.funcs.color = &icl_color_funcs;
		else if (DISPLAY_VER(i915) == 10)
			i915->display.funcs.color = &glk_color_funcs;
		else if (DISPLAY_VER(i915) == 9)
			i915->display.funcs.color = &skl_color_funcs;
		else if (DISPLAY_VER(i915) == 8)
			i915->display.funcs.color = &bdw_color_funcs;
		else if (IS_HASWELL(i915))
			i915->display.funcs.color = &hsw_color_funcs;
		else if (DISPLAY_VER(i915) == 7)
			i915->display.funcs.color = &ivb_color_funcs;
		else
			i915->display.funcs.color = &ilk_color_funcs;
	}
}
