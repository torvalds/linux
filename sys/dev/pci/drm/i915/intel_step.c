// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020,2021 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_step.h"

/*
 * Some platforms have unusual ways of mapping PCI revision ID to GT/display
 * steppings.  E.g., in some cases a higher PCI revision may translate to a
 * lower stepping of the GT and/or display IP.  This file provides lookup
 * tables to map the PCI revision into a standard set of stepping values that
 * can be compared numerically.
 *
 * Also note that some revisions/steppings may have been set aside as
 * placeholders but never materialized in real hardware; in those cases there
 * may be jumps in the revision IDs or stepping values in the tables below.
 */

/*
 * Some platforms always have the same stepping value for GT and display;
 * use a macro to define these to make it easier to identify the platforms
 * where the two steppings can deviate.
 */
#define COMMON_STEP(x)  .graphics_step = STEP_##x, .media_step = STEP_##x

static const struct intel_step_info skl_revids[] = {
	[0x6] = { COMMON_STEP(G0) },
	[0x7] = { COMMON_STEP(H0) },
	[0x9] = { COMMON_STEP(J0) },
	[0xA] = { COMMON_STEP(I1) },
};

static const struct intel_step_info kbl_revids[] = {
	[1] = { COMMON_STEP(B0) },
	[2] = { COMMON_STEP(C0) },
	[3] = { COMMON_STEP(D0) },
	[4] = { COMMON_STEP(F0) },
	[5] = { COMMON_STEP(C0) },
	[6] = { COMMON_STEP(D1) },
	[7] = { COMMON_STEP(G0) },
};

static const struct intel_step_info bxt_revids[] = {
	[0xA] = { COMMON_STEP(C0) },
	[0xB] = { COMMON_STEP(C0) },
	[0xC] = { COMMON_STEP(D0) },
	[0xD] = { COMMON_STEP(E0) },
};

static const struct intel_step_info glk_revids[] = {
	[3] = { COMMON_STEP(B0) },
};

static const struct intel_step_info icl_revids[] = {
	[7] = { COMMON_STEP(D0) },
};

static const struct intel_step_info jsl_ehl_revids[] = {
	[0] = { COMMON_STEP(A0) },
	[1] = { COMMON_STEP(B0) },
};

static const struct intel_step_info tgl_uy_revids[] = {
	[0] = { COMMON_STEP(A0) },
	[1] = { COMMON_STEP(B0) },
	[2] = { COMMON_STEP(B1) },
	[3] = { COMMON_STEP(C0) },
};

/* Same GT stepping between tgl_uy_revids and tgl_revids don't mean the same HW */
static const struct intel_step_info tgl_revids[] = {
	[0] = { COMMON_STEP(A0) },
	[1] = { COMMON_STEP(B0) },
};

static const struct intel_step_info rkl_revids[] = {
	[0] = { COMMON_STEP(A0) },
	[1] = { COMMON_STEP(B0) },
	[4] = { COMMON_STEP(C0) },
};

static const struct intel_step_info dg1_revids[] = {
	[0] = { COMMON_STEP(A0) },
	[1] = { COMMON_STEP(B0) },
};

static const struct intel_step_info adls_revids[] = {
	[0x0] = { COMMON_STEP(A0) },
	[0x1] = { COMMON_STEP(A0) },
	[0x4] = { COMMON_STEP(B0) },
	[0x8] = { COMMON_STEP(C0) },
	[0xC] = { COMMON_STEP(D0) },
};

static const struct intel_step_info adlp_revids[] = {
	[0x0] = { COMMON_STEP(A0) },
	[0x4] = { COMMON_STEP(B0) },
	[0x8] = { COMMON_STEP(C0) },
	[0xC] = { COMMON_STEP(C0) },
};

static const struct intel_step_info dg2_g10_revid_step_tbl[] = {
	[0x0] = { COMMON_STEP(A0) },
	[0x1] = { COMMON_STEP(A1) },
	[0x4] = { COMMON_STEP(B0) },
	[0x8] = { COMMON_STEP(C0) },
};

static const struct intel_step_info dg2_g11_revid_step_tbl[] = {
	[0x0] = { COMMON_STEP(A0) },
	[0x4] = { COMMON_STEP(B0) },
	[0x5] = { COMMON_STEP(B1) },
};

static const struct intel_step_info dg2_g12_revid_step_tbl[] = {
	[0x0] = { COMMON_STEP(A0) },
	[0x1] = { COMMON_STEP(A1) },
};

static const struct intel_step_info adls_rpls_revids[] = {
	[0x4] = { COMMON_STEP(D0) },
	[0xC] = { COMMON_STEP(D0) },
};

static const struct intel_step_info adlp_rplp_revids[] = {
	[0x4] = { COMMON_STEP(C0) },
};

static const struct intel_step_info adlp_n_revids[] = {
	[0x0] = { COMMON_STEP(A0) },
};

static u8 gmd_to_intel_step(struct drm_i915_private *i915,
			    struct intel_ip_version *gmd)
{
	u8 step = gmd->step + STEP_A0;

	if (step >= STEP_FUTURE) {
		drm_dbg(&i915->drm, "Using future steppings\n");
		return STEP_FUTURE;
	}

	return step;
}

void intel_step_init(struct drm_i915_private *i915)
{
	const struct intel_step_info *revids = NULL;
	int size = 0;
	int revid = INTEL_REVID(i915);
	struct intel_step_info step = {};

	if (HAS_GMD_ID(i915)) {
		step.graphics_step = gmd_to_intel_step(i915,
						       &RUNTIME_INFO(i915)->graphics.ip);
		step.media_step = gmd_to_intel_step(i915,
						    &RUNTIME_INFO(i915)->media.ip);

		RUNTIME_INFO(i915)->step = step;

		return;
	}

	if (IS_DG2_G10(i915)) {
		revids = dg2_g10_revid_step_tbl;
		size = ARRAY_SIZE(dg2_g10_revid_step_tbl);
	} else if (IS_DG2_G11(i915)) {
		revids = dg2_g11_revid_step_tbl;
		size = ARRAY_SIZE(dg2_g11_revid_step_tbl);
	} else if (IS_DG2_G12(i915)) {
		revids = dg2_g12_revid_step_tbl;
		size = ARRAY_SIZE(dg2_g12_revid_step_tbl);
	} else if (IS_ALDERLAKE_P_N(i915)) {
		revids = adlp_n_revids;
		size = ARRAY_SIZE(adlp_n_revids);
	} else if (IS_RAPTORLAKE_P(i915)) {
		revids = adlp_rplp_revids;
		size = ARRAY_SIZE(adlp_rplp_revids);
	} else if (IS_ALDERLAKE_P(i915)) {
		revids = adlp_revids;
		size = ARRAY_SIZE(adlp_revids);
	} else if (IS_RAPTORLAKE_S(i915)) {
		revids = adls_rpls_revids;
		size = ARRAY_SIZE(adls_rpls_revids);
	} else if (IS_ALDERLAKE_S(i915)) {
		revids = adls_revids;
		size = ARRAY_SIZE(adls_revids);
	} else if (IS_DG1(i915)) {
		revids = dg1_revids;
		size = ARRAY_SIZE(dg1_revids);
	} else if (IS_ROCKETLAKE(i915)) {
		revids = rkl_revids;
		size = ARRAY_SIZE(rkl_revids);
	} else if (IS_TIGERLAKE_UY(i915)) {
		revids = tgl_uy_revids;
		size = ARRAY_SIZE(tgl_uy_revids);
	} else if (IS_TIGERLAKE(i915)) {
		revids = tgl_revids;
		size = ARRAY_SIZE(tgl_revids);
	} else if (IS_JASPERLAKE(i915) || IS_ELKHARTLAKE(i915)) {
		revids = jsl_ehl_revids;
		size = ARRAY_SIZE(jsl_ehl_revids);
	} else if (IS_ICELAKE(i915)) {
		revids = icl_revids;
		size = ARRAY_SIZE(icl_revids);
	} else if (IS_GEMINILAKE(i915)) {
		revids = glk_revids;
		size = ARRAY_SIZE(glk_revids);
	} else if (IS_BROXTON(i915)) {
		revids = bxt_revids;
		size = ARRAY_SIZE(bxt_revids);
	} else if (IS_KABYLAKE(i915)) {
		revids = kbl_revids;
		size = ARRAY_SIZE(kbl_revids);
	} else if (IS_SKYLAKE(i915)) {
		revids = skl_revids;
		size = ARRAY_SIZE(skl_revids);
	}

	/* Not using the stepping scheme for the platform yet. */
	if (!revids)
		return;

	if (revid < size && revids[revid].graphics_step != STEP_NONE) {
		step = revids[revid];
	} else {
		drm_warn(&i915->drm, "Unknown revid 0x%02x\n", revid);

		/*
		 * If we hit a gap in the revid array, use the information for
		 * the next revid.
		 *
		 * This may be wrong in all sorts of ways, especially if the
		 * steppings in the array are not monotonically increasing, but
		 * it's better than defaulting to 0.
		 */
		while (revid < size && revids[revid].graphics_step == STEP_NONE)
			revid++;

		if (revid < size) {
			drm_dbg(&i915->drm, "Using steppings for revid 0x%02x\n",
				revid);
			step = revids[revid];
		} else {
			drm_dbg(&i915->drm, "Using future steppings\n");
			step.graphics_step = STEP_FUTURE;
		}
	}

	if (drm_WARN_ON(&i915->drm, step.graphics_step == STEP_NONE))
		return;

	RUNTIME_INFO(i915)->step = step;
}

#define STEP_NAME_CASE(name)	\
	case STEP_##name:	\
		return #name;

const char *intel_step_name(enum intel_step step)
{
	switch (step) {
	STEP_NAME_LIST(STEP_NAME_CASE);

	default:
		return "**";
	}
}
