/*
 * Copyright © 2014 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_irq.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_fbc.h"
#include "intel_fifo_underrun.h"
#include "intel_pch_display.h"

/**
 * DOC: fifo underrun handling
 *
 * The i915 driver checks for display fifo underruns using the interrupt signals
 * provided by the hardware. This is enabled by default and fairly useful to
 * debug display issues, especially watermark settings.
 *
 * If an underrun is detected this is logged into dmesg. To avoid flooding logs
 * and occupying the cpu underrun interrupts are disabled after the first
 * occurrence until the next modeset on a given pipe.
 *
 * Note that underrun detection on gmch platforms is a bit more ugly since there
 * is no interrupt (despite that the signalling bit is in the PIPESTAT pipe
 * interrupt register). Also on some other platforms underrun interrupts are
 * shared, which means that if we detect an underrun we need to disable underrun
 * reporting on all pipes.
 *
 * The code also supports underrun detection on the PCH transcoder.
 */

static bool ivb_can_enable_err_int(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc;
	enum pipe pipe;

	lockdep_assert_held(&dev_priv->irq_lock);

	for_each_pipe(dev_priv, pipe) {
		crtc = intel_crtc_for_pipe(dev_priv, pipe);

		if (crtc->cpu_fifo_underrun_disabled)
			return false;
	}

	return true;
}

static bool cpt_can_enable_serr_int(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe;
	struct intel_crtc *crtc;

	lockdep_assert_held(&dev_priv->irq_lock);

	for_each_pipe(dev_priv, pipe) {
		crtc = intel_crtc_for_pipe(dev_priv, pipe);

		if (crtc->pch_fifo_underrun_disabled)
			return false;
	}

	return true;
}

static void i9xx_check_fifo_underruns(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	i915_reg_t reg = PIPESTAT(dev_priv, crtc->pipe);
	u32 enable_mask;

	lockdep_assert_held(&dev_priv->irq_lock);

	if ((intel_de_read(dev_priv, reg) & PIPE_FIFO_UNDERRUN_STATUS) == 0)
		return;

	enable_mask = i915_pipestat_enable_mask(dev_priv, crtc->pipe);
	intel_de_write(dev_priv, reg, enable_mask | PIPE_FIFO_UNDERRUN_STATUS);
	intel_de_posting_read(dev_priv, reg);

	trace_intel_cpu_fifo_underrun(dev_priv, crtc->pipe);
	drm_err(&dev_priv->drm, "pipe %c underrun\n", pipe_name(crtc->pipe));
}

static void i9xx_set_fifo_underrun_reporting(struct drm_device *dev,
					     enum pipe pipe,
					     bool enable, bool old)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	i915_reg_t reg = PIPESTAT(dev_priv, pipe);

	lockdep_assert_held(&dev_priv->irq_lock);

	if (enable) {
		u32 enable_mask = i915_pipestat_enable_mask(dev_priv, pipe);

		intel_de_write(dev_priv, reg,
			       enable_mask | PIPE_FIFO_UNDERRUN_STATUS);
		intel_de_posting_read(dev_priv, reg);
	} else {
		if (old && intel_de_read(dev_priv, reg) & PIPE_FIFO_UNDERRUN_STATUS)
			drm_err(&dev_priv->drm, "pipe %c underrun\n",
				pipe_name(pipe));
	}
}

static void ilk_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum pipe pipe, bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 bit = (pipe == PIPE_A) ?
		DE_PIPEA_FIFO_UNDERRUN : DE_PIPEB_FIFO_UNDERRUN;

	if (enable)
		ilk_enable_display_irq(dev_priv, bit);
	else
		ilk_disable_display_irq(dev_priv, bit);
}

static void ivb_check_fifo_underruns(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 err_int = intel_de_read(dev_priv, GEN7_ERR_INT);

	lockdep_assert_held(&dev_priv->irq_lock);

	if ((err_int & ERR_INT_FIFO_UNDERRUN(pipe)) == 0)
		return;

	intel_de_write(dev_priv, GEN7_ERR_INT, ERR_INT_FIFO_UNDERRUN(pipe));
	intel_de_posting_read(dev_priv, GEN7_ERR_INT);

	trace_intel_cpu_fifo_underrun(dev_priv, pipe);
	drm_err(&dev_priv->drm, "fifo underrun on pipe %c\n", pipe_name(pipe));
}

static void ivb_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum pipe pipe, bool enable,
					    bool old)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	if (enable) {
		intel_de_write(dev_priv, GEN7_ERR_INT,
			       ERR_INT_FIFO_UNDERRUN(pipe));

		if (!ivb_can_enable_err_int(dev))
			return;

		ilk_enable_display_irq(dev_priv, DE_ERR_INT_IVB);
	} else {
		ilk_disable_display_irq(dev_priv, DE_ERR_INT_IVB);

		if (old &&
		    intel_de_read(dev_priv, GEN7_ERR_INT) & ERR_INT_FIFO_UNDERRUN(pipe)) {
			drm_err(&dev_priv->drm,
				"uncleared fifo underrun on pipe %c\n",
				pipe_name(pipe));
		}
	}
}

static u32
icl_pipe_status_underrun_mask(struct drm_i915_private *dev_priv)
{
	u32 mask = PIPE_STATUS_UNDERRUN;

	if (DISPLAY_VER(dev_priv) >= 13)
		mask |= PIPE_STATUS_SOFT_UNDERRUN_XELPD |
			PIPE_STATUS_HARD_UNDERRUN_XELPD |
			PIPE_STATUS_PORT_UNDERRUN_XELPD;

	return mask;
}

static void bdw_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum pipe pipe, bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 mask = gen8_de_pipe_underrun_mask(dev_priv);

	if (enable) {
		if (DISPLAY_VER(dev_priv) >= 11)
			intel_de_write(dev_priv,
				       ICL_PIPESTATUS(dev_priv, pipe),
				       icl_pipe_status_underrun_mask(dev_priv));

		bdw_enable_pipe_irq(dev_priv, pipe, mask);
	} else {
		bdw_disable_pipe_irq(dev_priv, pipe, mask);
	}
}

static void ibx_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum pipe pch_transcoder,
					    bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 bit = (pch_transcoder == PIPE_A) ?
		SDE_TRANSA_FIFO_UNDER : SDE_TRANSB_FIFO_UNDER;

	if (enable)
		ibx_enable_display_interrupt(dev_priv, bit);
	else
		ibx_disable_display_interrupt(dev_priv, bit);
}

static void cpt_check_pch_fifo_underruns(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pch_transcoder = crtc->pipe;
	u32 serr_int = intel_de_read(dev_priv, SERR_INT);

	lockdep_assert_held(&dev_priv->irq_lock);

	if ((serr_int & SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder)) == 0)
		return;

	intel_de_write(dev_priv, SERR_INT,
		       SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder));
	intel_de_posting_read(dev_priv, SERR_INT);

	trace_intel_pch_fifo_underrun(dev_priv, pch_transcoder);
	drm_err(&dev_priv->drm, "pch fifo underrun on pch transcoder %c\n",
		pipe_name(pch_transcoder));
}

static void cpt_set_fifo_underrun_reporting(struct drm_device *dev,
					    enum pipe pch_transcoder,
					    bool enable, bool old)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (enable) {
		intel_de_write(dev_priv, SERR_INT,
			       SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder));

		if (!cpt_can_enable_serr_int(dev))
			return;

		ibx_enable_display_interrupt(dev_priv, SDE_ERROR_CPT);
	} else {
		ibx_disable_display_interrupt(dev_priv, SDE_ERROR_CPT);

		if (old && intel_de_read(dev_priv, SERR_INT) &
		    SERR_INT_TRANS_FIFO_UNDERRUN(pch_transcoder)) {
			drm_err(&dev_priv->drm,
				"uncleared pch fifo underrun on pch transcoder %c\n",
				pipe_name(pch_transcoder));
		}
	}
}

static bool __intel_set_cpu_fifo_underrun_reporting(struct drm_device *dev,
						    enum pipe pipe, bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc = intel_crtc_for_pipe(dev_priv, pipe);
	bool old;

	lockdep_assert_held(&dev_priv->irq_lock);

	old = !crtc->cpu_fifo_underrun_disabled;
	crtc->cpu_fifo_underrun_disabled = !enable;

	if (HAS_GMCH(dev_priv))
		i9xx_set_fifo_underrun_reporting(dev, pipe, enable, old);
	else if (IS_IRONLAKE(dev_priv) || IS_SANDYBRIDGE(dev_priv))
		ilk_set_fifo_underrun_reporting(dev, pipe, enable);
	else if (DISPLAY_VER(dev_priv) == 7)
		ivb_set_fifo_underrun_reporting(dev, pipe, enable, old);
	else if (DISPLAY_VER(dev_priv) >= 8)
		bdw_set_fifo_underrun_reporting(dev, pipe, enable);

	return old;
}

/**
 * intel_set_cpu_fifo_underrun_reporting - set cpu fifo underrrun reporting state
 * @dev_priv: i915 device instance
 * @pipe: (CPU) pipe to set state for
 * @enable: whether underruns should be reported or not
 *
 * This function sets the fifo underrun state for @pipe. It is used in the
 * modeset code to avoid false positives since on many platforms underruns are
 * expected when disabling or enabling the pipe.
 *
 * Notice that on some platforms disabling underrun reports for one pipe
 * disables for all due to shared interrupts. Actual reporting is still per-pipe
 * though.
 *
 * Returns the previous state of underrun reporting.
 */
bool intel_set_cpu_fifo_underrun_reporting(struct drm_i915_private *dev_priv,
					   enum pipe pipe, bool enable)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&dev_priv->irq_lock, flags);
	ret = __intel_set_cpu_fifo_underrun_reporting(&dev_priv->drm, pipe,
						      enable);
	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);

	return ret;
}

/**
 * intel_set_pch_fifo_underrun_reporting - set PCH fifo underrun reporting state
 * @dev_priv: i915 device instance
 * @pch_transcoder: the PCH transcoder (same as pipe on IVB and older)
 * @enable: whether underruns should be reported or not
 *
 * This function makes us disable or enable PCH fifo underruns for a specific
 * PCH transcoder. Notice that on some PCHs (e.g. CPT/PPT), disabling FIFO
 * underrun reporting for one transcoder may also disable all the other PCH
 * error interruts for the other transcoders, due to the fact that there's just
 * one interrupt mask/enable bit for all the transcoders.
 *
 * Returns the previous state of underrun reporting.
 */
bool intel_set_pch_fifo_underrun_reporting(struct drm_i915_private *dev_priv,
					   enum pipe pch_transcoder,
					   bool enable)
{
	struct intel_crtc *crtc =
		intel_crtc_for_pipe(dev_priv, pch_transcoder);
	unsigned long flags;
	bool old;

	/*
	 * NOTE: Pre-LPT has a fixed cpu pipe -> pch transcoder mapping, but LPT
	 * has only one pch transcoder A that all pipes can use. To avoid racy
	 * pch transcoder -> pipe lookups from interrupt code simply store the
	 * underrun statistics in crtc A. Since we never expose this anywhere
	 * nor use it outside of the fifo underrun code here using the "wrong"
	 * crtc on LPT won't cause issues.
	 */

	spin_lock_irqsave(&dev_priv->irq_lock, flags);

	old = !crtc->pch_fifo_underrun_disabled;
	crtc->pch_fifo_underrun_disabled = !enable;

	if (HAS_PCH_IBX(dev_priv))
		ibx_set_fifo_underrun_reporting(&dev_priv->drm,
						pch_transcoder,
						enable);
	else
		cpt_set_fifo_underrun_reporting(&dev_priv->drm,
						pch_transcoder,
						enable, old);

	spin_unlock_irqrestore(&dev_priv->irq_lock, flags);
	return old;
}

/**
 * intel_cpu_fifo_underrun_irq_handler - handle CPU fifo underrun interrupt
 * @dev_priv: i915 device instance
 * @pipe: (CPU) pipe to set state for
 *
 * This handles a CPU fifo underrun interrupt, generating an underrun warning
 * into dmesg if underrun reporting is enabled and then disables the underrun
 * interrupt to avoid an irq storm.
 */
void intel_cpu_fifo_underrun_irq_handler(struct drm_i915_private *dev_priv,
					 enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(dev_priv, pipe);
	u32 underruns = 0;

	/* We may be called too early in init, thanks BIOS! */
	if (crtc == NULL)
		return;

	/* GMCH can't disable fifo underruns, filter them. */
	if (HAS_GMCH(dev_priv) &&
	    crtc->cpu_fifo_underrun_disabled)
		return;

	/*
	 * Starting with display version 11, the PIPE_STAT register records
	 * whether an underrun has happened, and on XELPD+, it will also record
	 * whether the underrun was soft/hard and whether it was triggered by
	 * the downstream port logic.  We should clear these bits (which use
	 * write-1-to-clear logic) too.
	 *
	 * Note that although the IIR gives us the same underrun and soft/hard
	 * information, PIPE_STAT is the only place we can find out whether
	 * the underrun was caused by the downstream port.
	 */
	if (DISPLAY_VER(dev_priv) >= 11) {
		underruns = intel_de_read(dev_priv,
					  ICL_PIPESTATUS(dev_priv, pipe)) &
			icl_pipe_status_underrun_mask(dev_priv);
		intel_de_write(dev_priv, ICL_PIPESTATUS(dev_priv, pipe),
			       underruns);
	}

	if (intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false)) {
		trace_intel_cpu_fifo_underrun(dev_priv, pipe);

		if (DISPLAY_VER(dev_priv) >= 11)
			drm_err(&dev_priv->drm, "CPU pipe %c FIFO underrun: %s%s%s%s\n",
				pipe_name(pipe),
				underruns & PIPE_STATUS_SOFT_UNDERRUN_XELPD ? "soft," : "",
				underruns & PIPE_STATUS_HARD_UNDERRUN_XELPD ? "hard," : "",
				underruns & PIPE_STATUS_PORT_UNDERRUN_XELPD ? "port," : "",
				underruns & PIPE_STATUS_UNDERRUN ? "transcoder," : "");
		else
			drm_err(&dev_priv->drm, "CPU pipe %c FIFO underrun\n", pipe_name(pipe));
	}

	intel_fbc_handle_fifo_underrun_irq(&dev_priv->display);
}

/**
 * intel_pch_fifo_underrun_irq_handler - handle PCH fifo underrun interrupt
 * @dev_priv: i915 device instance
 * @pch_transcoder: the PCH transcoder (same as pipe on IVB and older)
 *
 * This handles a PCH fifo underrun interrupt, generating an underrun warning
 * into dmesg if underrun reporting is enabled and then disables the underrun
 * interrupt to avoid an irq storm.
 */
void intel_pch_fifo_underrun_irq_handler(struct drm_i915_private *dev_priv,
					 enum pipe pch_transcoder)
{
	if (intel_set_pch_fifo_underrun_reporting(dev_priv, pch_transcoder,
						  false)) {
		trace_intel_pch_fifo_underrun(dev_priv, pch_transcoder);
		drm_err(&dev_priv->drm, "PCH transcoder %c FIFO underrun\n",
			pipe_name(pch_transcoder));
	}
}

/**
 * intel_check_cpu_fifo_underruns - check for CPU fifo underruns immediately
 * @dev_priv: i915 device instance
 *
 * Check for CPU fifo underruns immediately. Useful on IVB/HSW where the shared
 * error interrupt may have been disabled, and so CPU fifo underruns won't
 * necessarily raise an interrupt, and on GMCH platforms where underruns never
 * raise an interrupt.
 */
void intel_check_cpu_fifo_underruns(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;

	spin_lock_irq(&dev_priv->irq_lock);

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		if (crtc->cpu_fifo_underrun_disabled)
			continue;

		if (HAS_GMCH(dev_priv))
			i9xx_check_fifo_underruns(crtc);
		else if (DISPLAY_VER(dev_priv) == 7)
			ivb_check_fifo_underruns(crtc);
	}

	spin_unlock_irq(&dev_priv->irq_lock);
}

/**
 * intel_check_pch_fifo_underruns - check for PCH fifo underruns immediately
 * @dev_priv: i915 device instance
 *
 * Check for PCH fifo underruns immediately. Useful on CPT/PPT where the shared
 * error interrupt may have been disabled, and so PCH fifo underruns won't
 * necessarily raise an interrupt.
 */
void intel_check_pch_fifo_underruns(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc;

	spin_lock_irq(&dev_priv->irq_lock);

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		if (crtc->pch_fifo_underrun_disabled)
			continue;

		if (HAS_PCH_CPT(dev_priv))
			cpt_check_pch_fifo_underruns(crtc);
	}

	spin_unlock_irq(&dev_priv->irq_lock);
}

void intel_init_fifo_underrun_reporting(struct drm_i915_private *i915,
					struct intel_crtc *crtc,
					bool enable)
{
	crtc->cpu_fifo_underrun_disabled = !enable;

	/*
	 * We track the PCH trancoder underrun reporting state
	 * within the crtc. With crtc for pipe A housing the underrun
	 * reporting state for PCH transcoder A, crtc for pipe B housing
	 * it for PCH transcoder B, etc. LPT-H has only PCH transcoder A,
	 * and marking underrun reporting as disabled for the non-existing
	 * PCH transcoders B and C would prevent enabling the south
	 * error interrupt (see cpt_can_enable_serr_int()).
	 */
	if (intel_has_pch_trancoder(i915, crtc->pipe))
		crtc->pch_fifo_underrun_disabled = !enable;
}
