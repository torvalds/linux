/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2022, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_GSCCS_H__
#define __INTEL_PXP_GSCCS_H__

#include <linux/types.h>

#include "gt/uc/intel_gsc_uc_heci_cmd_submit.h"

struct intel_pxp;

#define GSC_PENDING_RETRY_MAXCOUNT 40
#define GSC_PENDING_RETRY_PAUSE_MS 50
#define GSCFW_MAX_ROUND_TRIP_LATENCY_MS (GSC_HECI_REPLY_LATENCY_MS + \
					 (GSC_PENDING_RETRY_MAXCOUNT * GSC_PENDING_RETRY_PAUSE_MS))

#ifdef CONFIG_DRM_I915_PXP
void intel_pxp_gsccs_fini(struct intel_pxp *pxp);
int intel_pxp_gsccs_init(struct intel_pxp *pxp);

int intel_pxp_gsccs_create_session(struct intel_pxp *pxp, int arb_session_id);
void intel_pxp_gsccs_end_arb_fw_session(struct intel_pxp *pxp, u32 arb_session_id);
bool intel_pxp_gsccs_is_ready_for_sessions(struct intel_pxp *pxp);

#else
static inline void intel_pxp_gsccs_fini(struct intel_pxp *pxp)
{
}

static inline int intel_pxp_gsccs_init(struct intel_pxp *pxp)
{
	return 0;
}

static inline bool intel_pxp_gsccs_is_ready_for_sessions(struct intel_pxp *pxp)
{
	return false;
}

#endif

#endif /*__INTEL_PXP_GSCCS_H__ */
