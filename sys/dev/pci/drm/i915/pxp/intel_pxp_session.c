// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#include "i915_drv.h"

#include "intel_pxp.h"
#include "intel_pxp_cmd.h"
#include "intel_pxp_gsccs.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
#include "intel_pxp_types.h"
#include "intel_pxp_regs.h"

#define ARB_SESSION I915_PROTECTED_CONTENT_DEFAULT_SESSION /* shorter define */

static bool intel_pxp_session_is_in_play(struct intel_pxp *pxp, u32 id)
{
	struct intel_uncore *uncore = pxp->ctrl_gt->uncore;
	intel_wakeref_t wakeref;
	u32 sip = 0;

	/* if we're suspended the session is considered off */
	with_intel_runtime_pm_if_in_use(uncore->rpm, wakeref)
		sip = intel_uncore_read(uncore, KCR_SIP(pxp->kcr_base));

	return sip & BIT(id);
}

static int pxp_wait_for_session_state(struct intel_pxp *pxp, u32 id, bool in_play)
{
	struct intel_uncore *uncore = pxp->ctrl_gt->uncore;
	intel_wakeref_t wakeref;
	u32 mask = BIT(id);
	int ret;

	/* if we're suspended the session is considered off */
	wakeref = intel_runtime_pm_get_if_in_use(uncore->rpm);
	if (!wakeref)
		return in_play ? -ENODEV : 0;

	ret = intel_wait_for_register(uncore,
				      KCR_SIP(pxp->kcr_base),
				      mask,
				      in_play ? mask : 0,
				      250);

	intel_runtime_pm_put(uncore->rpm, wakeref);

	return ret;
}

static int pxp_create_arb_session(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp->ctrl_gt;
	int ret;

	pxp->arb_is_valid = false;

	if (intel_pxp_session_is_in_play(pxp, ARB_SESSION)) {
		drm_err(&gt->i915->drm, "arb session already in play at creation time\n");
		return -EEXIST;
	}

	if (HAS_ENGINE(pxp->ctrl_gt, GSC0))
		ret = intel_pxp_gsccs_create_session(pxp, ARB_SESSION);
	else
		ret = intel_pxp_tee_cmd_create_arb_session(pxp, ARB_SESSION);
	if (ret) {
		drm_err(&gt->i915->drm, "tee cmd for arb session creation failed\n");
		return ret;
	}

	ret = pxp_wait_for_session_state(pxp, ARB_SESSION, true);
	if (ret) {
		drm_dbg(&gt->i915->drm, "arb session failed to go in play\n");
		return ret;
	}
	drm_dbg(&gt->i915->drm, "PXP ARB session is alive\n");

	if (!++pxp->key_instance)
		++pxp->key_instance;

	pxp->arb_is_valid = true;

	return 0;
}

static int pxp_terminate_arb_session_and_global(struct intel_pxp *pxp)
{
	int ret;
	struct intel_gt *gt = pxp->ctrl_gt;

	/* must mark termination in progress calling this function */
	GEM_WARN_ON(pxp->arb_is_valid);

	/* terminate the hw sessions */
	ret = intel_pxp_terminate_session(pxp, ARB_SESSION);
	if (ret) {
		drm_err(&gt->i915->drm, "Failed to submit session termination\n");
		return ret;
	}

	ret = pxp_wait_for_session_state(pxp, ARB_SESSION, false);
	if (ret) {
		drm_err(&gt->i915->drm, "Session state did not clear\n");
		return ret;
	}

	intel_uncore_write(gt->uncore, KCR_GLOBAL_TERMINATE(pxp->kcr_base), 1);

	if (HAS_ENGINE(gt, GSC0))
		intel_pxp_gsccs_end_arb_fw_session(pxp, ARB_SESSION);
	else
		intel_pxp_tee_end_arb_fw_session(pxp, ARB_SESSION);

	return ret;
}

void intel_pxp_terminate(struct intel_pxp *pxp, bool post_invalidation_needs_restart)
{
	int ret;

	pxp->hw_state_invalidated = post_invalidation_needs_restart;

	/*
	 * if we fail to submit the termination there is no point in waiting for
	 * it to complete. PXP will be marked as non-active until the next
	 * termination is issued.
	 */
	ret = pxp_terminate_arb_session_and_global(pxp);
	if (ret)
		complete_all(&pxp->termination);
}

static void pxp_terminate_complete(struct intel_pxp *pxp)
{
	/* Re-create the arb session after teardown handle complete */
	if (fetch_and_zero(&pxp->hw_state_invalidated)) {
		drm_dbg(&pxp->ctrl_gt->i915->drm, "PXP: creating arb_session after invalidation");
		pxp_create_arb_session(pxp);
	}

	complete_all(&pxp->termination);
}

static void pxp_session_work(struct work_struct *work)
{
	struct intel_pxp *pxp = container_of(work, typeof(*pxp), session_work);
	struct intel_gt *gt = pxp->ctrl_gt;
	intel_wakeref_t wakeref;
	u32 events = 0;

	spin_lock_irq(gt->irq_lock);
	events = fetch_and_zero(&pxp->session_events);
	spin_unlock_irq(gt->irq_lock);

	if (!events)
		return;

	drm_dbg(&gt->i915->drm, "PXP: processing event-flags 0x%08x", events);

	if (events & PXP_INVAL_REQUIRED)
		intel_pxp_invalidate(pxp);

	/*
	 * If we're processing an event while suspending then don't bother,
	 * we're going to re-init everything on resume anyway.
	 */
	wakeref = intel_runtime_pm_get_if_in_use(gt->uncore->rpm);
	if (!wakeref)
		return;

	if (events & PXP_TERMINATION_REQUEST) {
		events &= ~PXP_TERMINATION_COMPLETE;
		intel_pxp_terminate(pxp, true);
	}

	if (events & PXP_TERMINATION_COMPLETE)
		pxp_terminate_complete(pxp);

	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
}

void intel_pxp_session_management_init(struct intel_pxp *pxp)
{
	mutex_init(&pxp->arb_mutex);
	INIT_WORK(&pxp->session_work, pxp_session_work);
}
