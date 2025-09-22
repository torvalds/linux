// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/workqueue.h>

#include "i915_drv.h"

#include "intel_display_types.h"
#include "intel_encoder.h"

static void intel_encoder_link_check_work_fn(struct work_struct *work)
{
	struct intel_encoder *encoder =
		container_of(work, typeof(*encoder), link_check_work.work);

	encoder->link_check(encoder);
}

void intel_encoder_link_check_init(struct intel_encoder *encoder,
				   void (*callback)(struct intel_encoder *encoder))
{
	INIT_DELAYED_WORK(&encoder->link_check_work, intel_encoder_link_check_work_fn);
	encoder->link_check = callback;
}

void intel_encoder_link_check_flush_work(struct intel_encoder *encoder)
{
	cancel_delayed_work_sync(&encoder->link_check_work);
}

void intel_encoder_link_check_queue_work(struct intel_encoder *encoder, int delay_ms)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	mod_delayed_work(i915->unordered_wq,
			 &encoder->link_check_work, msecs_to_jiffies(delay_ms));
}

void intel_encoder_suspend_all(struct intel_display *display)
{
	struct intel_encoder *encoder;

	if (!HAS_DISPLAY(display))
		return;

	/*
	 * TODO: check and remove holding the modeset locks if none of
	 * the encoders depends on this.
	 */
	drm_modeset_lock_all(display->drm);
	for_each_intel_encoder(display->drm, encoder)
		if (encoder->suspend)
			encoder->suspend(encoder);
	drm_modeset_unlock_all(display->drm);

	for_each_intel_encoder(display->drm, encoder)
		if (encoder->suspend_complete)
			encoder->suspend_complete(encoder);
}

void intel_encoder_shutdown_all(struct intel_display *display)
{
	struct intel_encoder *encoder;

	if (!HAS_DISPLAY(display))
		return;

	/*
	 * TODO: check and remove holding the modeset locks if none of
	 * the encoders depends on this.
	 */
	drm_modeset_lock_all(display->drm);
	for_each_intel_encoder(display->drm, encoder)
		if (encoder->shutdown)
			encoder->shutdown(encoder);
	drm_modeset_unlock_all(display->drm);

	for_each_intel_encoder(display->drm, encoder)
		if (encoder->shutdown_complete)
			encoder->shutdown_complete(encoder);
}
