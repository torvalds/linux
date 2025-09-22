// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "hsw_ips.h"
#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_color_regs.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_pcode.h"

static void hsw_ips_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	u32 val;

	if (!crtc_state->ips_enabled)
		return;

	/*
	 * We can only enable IPS after we enable a plane and wait for a vblank
	 * This function is called from post_plane_update, which is run after
	 * a vblank wait.
	 */
	drm_WARN_ON(&i915->drm,
		    !(crtc_state->active_planes & ~BIT(PLANE_CURSOR)));

	val = IPS_ENABLE;

	if (i915->display.ips.false_color)
		val |= IPS_FALSE_COLOR;

	if (IS_BROADWELL(i915)) {
		drm_WARN_ON(&i915->drm,
			    snb_pcode_write(&i915->uncore, DISPLAY_IPS_CONTROL,
					    val | IPS_PCODE_CONTROL));
		/*
		 * Quoting Art Runyan: "its not safe to expect any particular
		 * value in IPS_CTL bit 31 after enabling IPS through the
		 * mailbox." Moreover, the mailbox may return a bogus state,
		 * so we need to just enable it and continue on.
		 */
	} else {
		intel_de_write(i915, IPS_CTL, val);
		/*
		 * The bit only becomes 1 in the next vblank, so this wait here
		 * is essentially intel_wait_for_vblank. If we don't have this
		 * and don't wait for vblanks until the end of crtc_enable, then
		 * the HW state readout code will complain that the expected
		 * IPS_CTL value is not the one we read.
		 */
		if (intel_de_wait_for_set(i915, IPS_CTL, IPS_ENABLE, 50))
			drm_err(&i915->drm,
				"Timed out waiting for IPS enable\n");
	}
}

bool hsw_ips_disable(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	bool need_vblank_wait = false;

	if (!crtc_state->ips_enabled)
		return need_vblank_wait;

	if (IS_BROADWELL(i915)) {
		drm_WARN_ON(&i915->drm,
			    snb_pcode_write(&i915->uncore, DISPLAY_IPS_CONTROL, 0));
		/*
		 * Wait for PCODE to finish disabling IPS. The BSpec specified
		 * 42ms timeout value leads to occasional timeouts so use 100ms
		 * instead.
		 */
		if (intel_de_wait_for_clear(i915, IPS_CTL, IPS_ENABLE, 100))
			drm_err(&i915->drm,
				"Timed out waiting for IPS disable\n");
	} else {
		intel_de_write(i915, IPS_CTL, 0);
		intel_de_posting_read(i915, IPS_CTL);
	}

	/* We need to wait for a vblank before we can disable the plane. */
	need_vblank_wait = true;

	return need_vblank_wait;
}

static bool hsw_ips_need_disable(struct intel_atomic_state *state,
				 struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!old_crtc_state->ips_enabled)
		return false;

	if (intel_crtc_needs_modeset(new_crtc_state))
		return true;

	/*
	 * Workaround : Do not read or write the pipe palette/gamma data while
	 * GAMMA_MODE is configured for split gamma and IPS_CTL has IPS enabled.
	 *
	 * Disable IPS before we program the LUT.
	 */
	if (IS_HASWELL(i915) &&
	    intel_crtc_needs_color_update(new_crtc_state) &&
	    new_crtc_state->gamma_mode == GAMMA_MODE_MODE_SPLIT)
		return true;

	return !new_crtc_state->ips_enabled;
}

bool hsw_ips_pre_update(struct intel_atomic_state *state,
			struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);

	if (!hsw_ips_need_disable(state, crtc))
		return false;

	return hsw_ips_disable(old_crtc_state);
}

static bool hsw_ips_need_enable(struct intel_atomic_state *state,
				struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!new_crtc_state->ips_enabled)
		return false;

	if (intel_crtc_needs_modeset(new_crtc_state))
		return true;

	/*
	 * Workaround : Do not read or write the pipe palette/gamma data while
	 * GAMMA_MODE is configured for split gamma and IPS_CTL has IPS enabled.
	 *
	 * Re-enable IPS after the LUT has been programmed.
	 */
	if (IS_HASWELL(i915) &&
	    intel_crtc_needs_color_update(new_crtc_state) &&
	    new_crtc_state->gamma_mode == GAMMA_MODE_MODE_SPLIT)
		return true;

	/*
	 * We can't read out IPS on broadwell, assume the worst and
	 * forcibly enable IPS on the first fastset.
	 */
	if (intel_crtc_needs_fastset(new_crtc_state) && old_crtc_state->inherited)
		return true;

	return !old_crtc_state->ips_enabled;
}

void hsw_ips_post_update(struct intel_atomic_state *state,
			 struct intel_crtc *crtc)
{
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	if (!hsw_ips_need_enable(state, crtc))
		return;

	hsw_ips_enable(new_crtc_state);
}

/* IPS only exists on ULT machines and is tied to pipe A. */
bool hsw_crtc_supports_ips(struct intel_crtc *crtc)
{
	return HAS_IPS(to_i915(crtc->base.dev)) && crtc->pipe == PIPE_A;
}

bool hsw_crtc_state_ips_capable(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	/* IPS only exists on ULT machines and is tied to pipe A. */
	if (!hsw_crtc_supports_ips(crtc))
		return false;

	if (!i915->display.params.enable_ips)
		return false;

	if (crtc_state->pipe_bpp > 24)
		return false;

	/*
	 * We compare against max which means we must take
	 * the increased cdclk requirement into account when
	 * calculating the new cdclk.
	 *
	 * Should measure whether using a lower cdclk w/o IPS
	 */
	if (IS_BROADWELL(i915) &&
	    crtc_state->pixel_rate > i915->display.cdclk.max_cdclk_freq * 95 / 100)
		return false;

	return true;
}

int hsw_ips_compute_config(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	crtc_state->ips_enabled = false;

	if (!hsw_crtc_state_ips_capable(crtc_state))
		return 0;

	/*
	 * When IPS gets enabled, the pipe CRC changes. Since IPS gets
	 * enabled and disabled dynamically based on package C states,
	 * user space can't make reliable use of the CRCs, so let's just
	 * completely disable it.
	 */
	if (crtc_state->crc_enabled)
		return 0;

	/* IPS should be fine as long as at least one plane is enabled. */
	if (!(crtc_state->active_planes & ~BIT(PLANE_CURSOR)))
		return 0;

	if (IS_BROADWELL(i915)) {
		const struct intel_cdclk_state *cdclk_state;

		cdclk_state = intel_atomic_get_cdclk_state(state);
		if (IS_ERR(cdclk_state))
			return PTR_ERR(cdclk_state);

		/* pixel rate mustn't exceed 95% of cdclk with IPS on BDW */
		if (crtc_state->pixel_rate > cdclk_state->logical.cdclk * 95 / 100)
			return 0;
	}

	crtc_state->ips_enabled = true;

	return 0;
}

void hsw_ips_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (!hsw_crtc_supports_ips(crtc))
		return;

	if (IS_HASWELL(i915)) {
		crtc_state->ips_enabled = intel_de_read(i915, IPS_CTL) & IPS_ENABLE;
	} else {
		/*
		 * We cannot readout IPS state on broadwell, set to
		 * true so we can set it to a defined state on first
		 * commit.
		 */
		crtc_state->ips_enabled = true;
	}
}

static int hsw_ips_debugfs_false_color_get(void *data, u64 *val)
{
	struct intel_crtc *crtc = data;
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	*val = i915->display.ips.false_color;

	return 0;
}

static int hsw_ips_debugfs_false_color_set(void *data, u64 val)
{
	struct intel_crtc *crtc = data;
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_crtc_state *crtc_state;
	int ret;

	ret = drm_modeset_lock(&crtc->base.mutex, NULL);
	if (ret)
		return ret;

	i915->display.ips.false_color = val;

	crtc_state = to_intel_crtc_state(crtc->base.state);

	if (!crtc_state->hw.active)
		goto unlock;

	if (crtc_state->uapi.commit &&
	    !try_wait_for_completion(&crtc_state->uapi.commit->hw_done))
		goto unlock;

	hsw_ips_enable(crtc_state);

 unlock:
	drm_modeset_unlock(&crtc->base.mutex);

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(hsw_ips_debugfs_false_color_fops,
			 hsw_ips_debugfs_false_color_get,
			 hsw_ips_debugfs_false_color_set,
			 "%llu\n");

static int hsw_ips_debugfs_status_show(struct seq_file *m, void *unused)
{
	struct intel_crtc *crtc = m->private;
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	intel_wakeref_t wakeref;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	seq_printf(m, "Enabled by kernel parameter: %s\n",
		   str_yes_no(i915->display.params.enable_ips));

	if (DISPLAY_VER(i915) >= 8) {
		seq_puts(m, "Currently: unknown\n");
	} else {
		if (intel_de_read(i915, IPS_CTL) & IPS_ENABLE)
			seq_puts(m, "Currently: enabled\n");
		else
			seq_puts(m, "Currently: disabled\n");
	}

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(hsw_ips_debugfs_status);

void hsw_ips_crtc_debugfs_add(struct intel_crtc *crtc)
{
	if (!hsw_crtc_supports_ips(crtc))
		return;

	debugfs_create_file("i915_ips_false_color", 0644, crtc->base.debugfs_entry,
			    crtc, &hsw_ips_debugfs_false_color_fops);

	debugfs_create_file("i915_ips_status", 0444, crtc->base.debugfs_entry,
			    crtc, &hsw_ips_debugfs_status_fops);
}
