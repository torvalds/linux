// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/bitops.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_bw.h"
#include "intel_cdclk.h"
#include "intel_de.h"
#include "intel_display_trace.h"
#include "intel_pmdemand.h"
#include "skl_watermark.h"

static struct intel_global_state *
intel_pmdemand_duplicate_state(struct intel_global_obj *obj)
{
	struct intel_pmdemand_state *pmdemand_state;

	pmdemand_state = kmemdup(obj->state, sizeof(*pmdemand_state), GFP_KERNEL);
	if (!pmdemand_state)
		return NULL;

	return &pmdemand_state->base;
}

static void intel_pmdemand_destroy_state(struct intel_global_obj *obj,
					 struct intel_global_state *state)
{
	kfree(state);
}

static const struct intel_global_state_funcs intel_pmdemand_funcs = {
	.atomic_duplicate_state = intel_pmdemand_duplicate_state,
	.atomic_destroy_state = intel_pmdemand_destroy_state,
};

static struct intel_pmdemand_state *
intel_atomic_get_pmdemand_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_global_state *pmdemand_state =
		intel_atomic_get_global_obj_state(state,
						  &i915->display.pmdemand.obj);

	if (IS_ERR(pmdemand_state))
		return ERR_CAST(pmdemand_state);

	return to_intel_pmdemand_state(pmdemand_state);
}

static struct intel_pmdemand_state *
intel_atomic_get_old_pmdemand_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_global_state *pmdemand_state =
		intel_atomic_get_old_global_obj_state(state,
						      &i915->display.pmdemand.obj);

	if (!pmdemand_state)
		return NULL;

	return to_intel_pmdemand_state(pmdemand_state);
}

static struct intel_pmdemand_state *
intel_atomic_get_new_pmdemand_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_global_state *pmdemand_state =
		intel_atomic_get_new_global_obj_state(state,
						      &i915->display.pmdemand.obj);

	if (!pmdemand_state)
		return NULL;

	return to_intel_pmdemand_state(pmdemand_state);
}

int intel_pmdemand_init(struct drm_i915_private *i915)
{
	struct intel_pmdemand_state *pmdemand_state;

	pmdemand_state = kzalloc(sizeof(*pmdemand_state), GFP_KERNEL);
	if (!pmdemand_state)
		return -ENOMEM;

	intel_atomic_global_obj_init(i915, &i915->display.pmdemand.obj,
				     &pmdemand_state->base,
				     &intel_pmdemand_funcs);

	if (IS_DISPLAY_VER_STEP(i915, IP_VER(14, 0), STEP_A0, STEP_C0))
		/* Wa_14016740474 */
		intel_de_rmw(i915, XELPD_CHICKEN_DCPR_3, 0, DMD_RSP_TIMEOUT_DISABLE);

	return 0;
}

void intel_pmdemand_init_early(struct drm_i915_private *i915)
{
	rw_init(&i915->display.pmdemand.lock, "pmdem");
	init_waitqueue_head(&i915->display.pmdemand.waitqueue);
}

void
intel_pmdemand_update_phys_mask(struct drm_i915_private *i915,
				struct intel_encoder *encoder,
				struct intel_pmdemand_state *pmdemand_state,
				bool set_bit)
{
	enum phy phy;

	if (DISPLAY_VER(i915) < 14)
		return;

	if (!encoder)
		return;

	if (intel_encoder_is_tc(encoder))
		return;

	phy = intel_encoder_to_phy(encoder);

	if (set_bit)
		pmdemand_state->active_combo_phys_mask |= BIT(phy);
	else
		pmdemand_state->active_combo_phys_mask &= ~BIT(phy);
}

void
intel_pmdemand_update_port_clock(struct drm_i915_private *i915,
				 struct intel_pmdemand_state *pmdemand_state,
				 enum pipe pipe, int port_clock)
{
	if (DISPLAY_VER(i915) < 14)
		return;

	pmdemand_state->ddi_clocks[pipe] = port_clock;
}

static void
intel_pmdemand_update_max_ddiclk(struct drm_i915_private *i915,
				 struct intel_atomic_state *state,
				 struct intel_pmdemand_state *pmdemand_state)
{
	int max_ddiclk = 0;
	const struct intel_crtc_state *new_crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, new_crtc_state, i)
		intel_pmdemand_update_port_clock(i915, pmdemand_state,
						 crtc->pipe,
						 new_crtc_state->port_clock);

	for (i = 0; i < ARRAY_SIZE(pmdemand_state->ddi_clocks); i++)
		max_ddiclk = max(pmdemand_state->ddi_clocks[i], max_ddiclk);

	pmdemand_state->params.ddiclk_max = DIV_ROUND_UP(max_ddiclk, 1000);
}

static void
intel_pmdemand_update_connector_phys(struct drm_i915_private *i915,
				     struct intel_atomic_state *state,
				     struct drm_connector_state *conn_state,
				     bool set_bit,
				     struct intel_pmdemand_state *pmdemand_state)
{
	struct intel_encoder *encoder = to_intel_encoder(conn_state->best_encoder);
	struct intel_crtc *crtc = to_intel_crtc(conn_state->crtc);
	struct intel_crtc_state *crtc_state;

	if (!crtc)
		return;

	if (set_bit)
		crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	else
		crtc_state = intel_atomic_get_old_crtc_state(state, crtc);

	if (!crtc_state->hw.active)
		return;

	intel_pmdemand_update_phys_mask(i915, encoder, pmdemand_state,
					set_bit);
}

static void
intel_pmdemand_update_active_non_tc_phys(struct drm_i915_private *i915,
					 struct intel_atomic_state *state,
					 struct intel_pmdemand_state *pmdemand_state)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector_state *new_conn_state;
	struct drm_connector *connector;
	int i;

	for_each_oldnew_connector_in_state(&state->base, connector,
					   old_conn_state, new_conn_state, i) {
		if (!intel_connector_needs_modeset(state, connector))
			continue;

		/* First clear the active phys in the old connector state */
		intel_pmdemand_update_connector_phys(i915, state,
						     old_conn_state, false,
						     pmdemand_state);

		/* Then set the active phys in new connector state */
		intel_pmdemand_update_connector_phys(i915, state,
						     new_conn_state, true,
						     pmdemand_state);
	}

	pmdemand_state->params.active_phys =
		min_t(u16, hweight16(pmdemand_state->active_combo_phys_mask),
		      7);
}

static bool
intel_pmdemand_encoder_has_tc_phy(struct drm_i915_private *i915,
				  struct intel_encoder *encoder)
{
	return encoder && intel_encoder_is_tc(encoder);
}

static bool
intel_pmdemand_connector_needs_update(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct drm_connector_state *old_conn_state;
	struct drm_connector_state *new_conn_state;
	struct drm_connector *connector;
	int i;

	for_each_oldnew_connector_in_state(&state->base, connector,
					   old_conn_state, new_conn_state, i) {
		struct intel_encoder *old_encoder =
			to_intel_encoder(old_conn_state->best_encoder);
		struct intel_encoder *new_encoder =
			to_intel_encoder(new_conn_state->best_encoder);

		if (!intel_connector_needs_modeset(state, connector))
			continue;

		if (old_encoder == new_encoder ||
		    (intel_pmdemand_encoder_has_tc_phy(i915, old_encoder) &&
		     intel_pmdemand_encoder_has_tc_phy(i915, new_encoder)))
			continue;

		return true;
	}

	return false;
}

static bool intel_pmdemand_needs_update(struct intel_atomic_state *state)
{
	const struct intel_bw_state *new_bw_state, *old_bw_state;
	const struct intel_cdclk_state *new_cdclk_state, *old_cdclk_state;
	const struct intel_crtc_state *new_crtc_state, *old_crtc_state;
	const struct intel_dbuf_state *new_dbuf_state, *old_dbuf_state;
	struct intel_crtc *crtc;
	int i;

	new_bw_state = intel_atomic_get_new_bw_state(state);
	old_bw_state = intel_atomic_get_old_bw_state(state);
	if (new_bw_state && new_bw_state->qgv_point_peakbw !=
	    old_bw_state->qgv_point_peakbw)
		return true;

	new_dbuf_state = intel_atomic_get_new_dbuf_state(state);
	old_dbuf_state = intel_atomic_get_old_dbuf_state(state);
	if (new_dbuf_state &&
	    (new_dbuf_state->active_pipes !=
	     old_dbuf_state->active_pipes ||
	     new_dbuf_state->enabled_slices !=
	     old_dbuf_state->enabled_slices))
		return true;

	new_cdclk_state = intel_atomic_get_new_cdclk_state(state);
	old_cdclk_state = intel_atomic_get_old_cdclk_state(state);
	if (new_cdclk_state &&
	    (new_cdclk_state->actual.cdclk !=
	     old_cdclk_state->actual.cdclk ||
	     new_cdclk_state->actual.voltage_level !=
	     old_cdclk_state->actual.voltage_level))
		return true;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i)
		if (new_crtc_state->port_clock != old_crtc_state->port_clock)
			return true;

	return intel_pmdemand_connector_needs_update(state);
}

int intel_pmdemand_atomic_check(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_bw_state *new_bw_state;
	const struct intel_cdclk_state *new_cdclk_state;
	const struct intel_dbuf_state *new_dbuf_state;
	struct intel_pmdemand_state *new_pmdemand_state;

	if (DISPLAY_VER(i915) < 14)
		return 0;

	if (!intel_pmdemand_needs_update(state))
		return 0;

	new_pmdemand_state = intel_atomic_get_pmdemand_state(state);
	if (IS_ERR(new_pmdemand_state))
		return PTR_ERR(new_pmdemand_state);

	new_bw_state = intel_atomic_get_bw_state(state);
	if (IS_ERR(new_bw_state))
		return PTR_ERR(new_bw_state);

	/* firmware will calculate the qclk_gv_index, requirement is set to 0 */
	new_pmdemand_state->params.qclk_gv_index = 0;
	new_pmdemand_state->params.qclk_gv_bw = new_bw_state->qgv_point_peakbw;

	new_dbuf_state = intel_atomic_get_dbuf_state(state);
	if (IS_ERR(new_dbuf_state))
		return PTR_ERR(new_dbuf_state);

	new_pmdemand_state->params.active_pipes =
		min_t(u8, hweight8(new_dbuf_state->active_pipes), 3);
	new_pmdemand_state->params.active_dbufs =
		min_t(u8, hweight8(new_dbuf_state->enabled_slices), 3);

	new_cdclk_state = intel_atomic_get_cdclk_state(state);
	if (IS_ERR(new_cdclk_state))
		return PTR_ERR(new_cdclk_state);

	new_pmdemand_state->params.voltage_index =
		new_cdclk_state->actual.voltage_level;
	new_pmdemand_state->params.cdclk_freq_mhz =
		DIV_ROUND_UP(new_cdclk_state->actual.cdclk, 1000);

	intel_pmdemand_update_max_ddiclk(i915, state, new_pmdemand_state);

	intel_pmdemand_update_active_non_tc_phys(i915, state, new_pmdemand_state);

	/*
	 * Active_PLLs starts with 1 because of CDCLK PLL.
	 * TODO: Missing to account genlock filter when it gets used.
	 */
	new_pmdemand_state->params.plls =
		min_t(u16, new_pmdemand_state->params.active_phys + 1, 7);

	/*
	 * Setting scalers to max as it can not be calculated during flips and
	 * fastsets without taking global states locks.
	 */
	new_pmdemand_state->params.scalers = 7;

	if (state->base.allow_modeset)
		return intel_atomic_serialize_global_state(&new_pmdemand_state->base);
	else
		return intel_atomic_lock_global_state(&new_pmdemand_state->base);
}

static bool intel_pmdemand_check_prev_transaction(struct drm_i915_private *i915)
{
	return !(intel_de_wait_for_clear(i915,
					 XELPDP_INITIATE_PMDEMAND_REQUEST(1),
					 XELPDP_PMDEMAND_REQ_ENABLE, 10) ||
		 intel_de_wait_for_clear(i915,
					 GEN12_DCPR_STATUS_1,
					 XELPDP_PMDEMAND_INFLIGHT_STATUS, 10));
}

void
intel_pmdemand_init_pmdemand_params(struct drm_i915_private *i915,
				    struct intel_pmdemand_state *pmdemand_state)
{
	u32 reg1, reg2;

	if (DISPLAY_VER(i915) < 14)
		return;

	mutex_lock(&i915->display.pmdemand.lock);
	if (drm_WARN_ON(&i915->drm,
			!intel_pmdemand_check_prev_transaction(i915))) {
		memset(&pmdemand_state->params, 0,
		       sizeof(pmdemand_state->params));
		goto unlock;
	}

	reg1 = intel_de_read(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(0));

	reg2 = intel_de_read(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(1));

	/* Set 1*/
	pmdemand_state->params.qclk_gv_bw =
		REG_FIELD_GET(XELPDP_PMDEMAND_QCLK_GV_BW_MASK, reg1);
	pmdemand_state->params.voltage_index =
		REG_FIELD_GET(XELPDP_PMDEMAND_VOLTAGE_INDEX_MASK, reg1);
	pmdemand_state->params.qclk_gv_index =
		REG_FIELD_GET(XELPDP_PMDEMAND_QCLK_GV_INDEX_MASK, reg1);
	pmdemand_state->params.active_pipes =
		REG_FIELD_GET(XELPDP_PMDEMAND_PIPES_MASK, reg1);
	pmdemand_state->params.active_dbufs =
		REG_FIELD_GET(XELPDP_PMDEMAND_DBUFS_MASK, reg1);
	pmdemand_state->params.active_phys =
		REG_FIELD_GET(XELPDP_PMDEMAND_PHYS_MASK, reg1);

	/* Set 2*/
	pmdemand_state->params.cdclk_freq_mhz =
		REG_FIELD_GET(XELPDP_PMDEMAND_CDCLK_FREQ_MASK, reg2);
	pmdemand_state->params.ddiclk_max =
		REG_FIELD_GET(XELPDP_PMDEMAND_DDICLK_FREQ_MASK, reg2);
	pmdemand_state->params.scalers =
		REG_FIELD_GET(XELPDP_PMDEMAND_SCALERS_MASK, reg2);

unlock:
	mutex_unlock(&i915->display.pmdemand.lock);
}

static bool intel_pmdemand_req_complete(struct drm_i915_private *i915)
{
	return !(intel_de_read(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(1)) &
		 XELPDP_PMDEMAND_REQ_ENABLE);
}

static void intel_pmdemand_wait(struct drm_i915_private *i915)
{
	if (!wait_event_timeout(i915->display.pmdemand.waitqueue,
				intel_pmdemand_req_complete(i915),
				msecs_to_jiffies_timeout(10)))
		drm_err(&i915->drm,
			"timed out waiting for Punit PM Demand Response\n");
}

/* Required to be programmed during Display Init Sequences. */
void intel_pmdemand_program_dbuf(struct drm_i915_private *i915,
				 u8 dbuf_slices)
{
	u32 dbufs = min_t(u32, hweight8(dbuf_slices), 3);

	mutex_lock(&i915->display.pmdemand.lock);
	if (drm_WARN_ON(&i915->drm,
			!intel_pmdemand_check_prev_transaction(i915)))
		goto unlock;

	intel_de_rmw(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(0),
		     XELPDP_PMDEMAND_DBUFS_MASK,
		     REG_FIELD_PREP(XELPDP_PMDEMAND_DBUFS_MASK, dbufs));
	intel_de_rmw(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(1), 0,
		     XELPDP_PMDEMAND_REQ_ENABLE);

	intel_pmdemand_wait(i915);

unlock:
	mutex_unlock(&i915->display.pmdemand.lock);
}

static void
intel_pmdemand_update_params(const struct intel_pmdemand_state *new,
			     const struct intel_pmdemand_state *old,
			     u32 *reg1, u32 *reg2, bool serialized)
{
	/*
	 * The pmdemand parameter updates happens in two steps. Pre plane and
	 * post plane updates. During the pre plane, as DE might still be
	 * handling with some old operations, to avoid unexpected performance
	 * issues, program the pmdemand parameters with higher of old and new
	 * values. And then after once settled, use the new parameter values
	 * as part of the post plane update.
	 *
	 * If the pmdemand params update happens without modeset allowed, this
	 * means we can't serialize the updates. So that implies possibility of
	 * some parallel atomic commits affecting the pmdemand parameters. In
	 * that case, we need to consider the current values from the register
	 * as well. So in pre-plane case, we need to check the max of old, new
	 * and current register value if not serialized. In post plane update
	 * we need to consider max of new and current register value if not
	 * serialized
	 */

#define update_reg(reg, field, mask) do { \
	u32 current_val = serialized ? 0 : REG_FIELD_GET((mask), *(reg)); \
	u32 old_val = old ? old->params.field : 0; \
	u32 new_val = new->params.field; \
\
	*(reg) &= ~(mask); \
	*(reg) |= REG_FIELD_PREP((mask), max3(old_val, new_val, current_val)); \
} while (0)

	/* Set 1*/
	update_reg(reg1, qclk_gv_bw, XELPDP_PMDEMAND_QCLK_GV_BW_MASK);
	update_reg(reg1, voltage_index, XELPDP_PMDEMAND_VOLTAGE_INDEX_MASK);
	update_reg(reg1, qclk_gv_index, XELPDP_PMDEMAND_QCLK_GV_INDEX_MASK);
	update_reg(reg1, active_pipes, XELPDP_PMDEMAND_PIPES_MASK);
	update_reg(reg1, active_dbufs, XELPDP_PMDEMAND_DBUFS_MASK);
	update_reg(reg1, active_phys, XELPDP_PMDEMAND_PHYS_MASK);

	/* Set 2*/
	update_reg(reg2, cdclk_freq_mhz, XELPDP_PMDEMAND_CDCLK_FREQ_MASK);
	update_reg(reg2, ddiclk_max, XELPDP_PMDEMAND_DDICLK_FREQ_MASK);
	update_reg(reg2, scalers, XELPDP_PMDEMAND_SCALERS_MASK);
	update_reg(reg2, plls, XELPDP_PMDEMAND_PLLS_MASK);

#undef update_reg
}

static void
intel_pmdemand_program_params(struct drm_i915_private *i915,
			      const struct intel_pmdemand_state *new,
			      const struct intel_pmdemand_state *old,
			      bool serialized)
{
	bool changed = false;
	u32 reg1, mod_reg1;
	u32 reg2, mod_reg2;

	mutex_lock(&i915->display.pmdemand.lock);
	if (drm_WARN_ON(&i915->drm,
			!intel_pmdemand_check_prev_transaction(i915)))
		goto unlock;

	reg1 = intel_de_read(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(0));
	mod_reg1 = reg1;

	reg2 = intel_de_read(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(1));
	mod_reg2 = reg2;

	intel_pmdemand_update_params(new, old, &mod_reg1, &mod_reg2,
				     serialized);

	if (reg1 != mod_reg1) {
		intel_de_write(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(0),
			       mod_reg1);
		changed = true;
	}

	if (reg2 != mod_reg2) {
		intel_de_write(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(1),
			       mod_reg2);
		changed = true;
	}

	/* Initiate pm demand request only if register values are changed */
	if (!changed)
		goto unlock;

	drm_dbg_kms(&i915->drm,
		    "initate pmdemand request values: (0x%x 0x%x)\n",
		    mod_reg1, mod_reg2);

	intel_de_rmw(i915, XELPDP_INITIATE_PMDEMAND_REQUEST(1), 0,
		     XELPDP_PMDEMAND_REQ_ENABLE);

	intel_pmdemand_wait(i915);

unlock:
	mutex_unlock(&i915->display.pmdemand.lock);
}

static bool
intel_pmdemand_state_changed(const struct intel_pmdemand_state *new,
			     const struct intel_pmdemand_state *old)
{
	return memcmp(&new->params, &old->params, sizeof(new->params)) != 0;
}

void intel_pmdemand_pre_plane_update(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_pmdemand_state *new_pmdemand_state =
		intel_atomic_get_new_pmdemand_state(state);
	const struct intel_pmdemand_state *old_pmdemand_state =
		intel_atomic_get_old_pmdemand_state(state);

	if (DISPLAY_VER(i915) < 14)
		return;

	if (!new_pmdemand_state ||
	    !intel_pmdemand_state_changed(new_pmdemand_state,
					  old_pmdemand_state))
		return;

	WARN_ON(!new_pmdemand_state->base.changed);

	intel_pmdemand_program_params(i915, new_pmdemand_state,
				      old_pmdemand_state,
				      intel_atomic_global_state_is_serialized(state));
}

void intel_pmdemand_post_plane_update(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_pmdemand_state *new_pmdemand_state =
		intel_atomic_get_new_pmdemand_state(state);
	const struct intel_pmdemand_state *old_pmdemand_state =
		intel_atomic_get_old_pmdemand_state(state);

	if (DISPLAY_VER(i915) < 14)
		return;

	if (!new_pmdemand_state ||
	    !intel_pmdemand_state_changed(new_pmdemand_state,
					  old_pmdemand_state))
		return;

	WARN_ON(!new_pmdemand_state->base.changed);

	intel_pmdemand_program_params(i915, new_pmdemand_state, NULL,
				      intel_atomic_global_state_is_serialized(state));
}
