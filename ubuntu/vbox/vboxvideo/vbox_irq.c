/*
 * Copyright (C) 2016-2017 Oracle Corporation
 * This file is based on qxl_irq.c
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alon Levy
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */

#include "vbox_drv.h"

#include "vboxvideo.h"

#include <drm/drm_crtc_helper.h>

static void vbox_clear_irq(void)
{
	outl((u32)~0, VGA_PORT_HGSMI_HOST);
}

static u32 vbox_get_flags(struct vbox_private *vbox)
{
	return readl(vbox->guest_heap + HOST_FLAGS_OFFSET);
}

void vbox_report_hotplug(struct vbox_private *vbox)
{
	schedule_work(&vbox->hotplug_work);
}

irqreturn_t vbox_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct vbox_private *vbox = (struct vbox_private *)dev->dev_private;
	u32 host_flags = vbox_get_flags(vbox);

	if (!(host_flags & HGSMIHOSTFLAGS_IRQ))
		return IRQ_NONE;

	/*
	 * Due to a bug in the initial host implementation of hot-plug irqs,
	 * the hot-plug and cursor capability flags were never cleared.
	 * Fortunately we can tell when they would have been set by checking
	 * that the VSYNC flag is not set.
	 */
	if (host_flags &
	    (HGSMIHOSTFLAGS_HOTPLUG | HGSMIHOSTFLAGS_CURSOR_CAPABILITIES) &&
	    !(host_flags & HGSMIHOSTFLAGS_VSYNC))
		vbox_report_hotplug(vbox);

	vbox_clear_irq();

	return IRQ_HANDLED;
}

/**
 * Check that the position hints provided by the host are suitable for GNOME
 * shell (i.e. all screens disjoint and hints for all enabled screens) and if
 * not replace them with default ones.  Providing valid hints improves the
 * chances that we will get a known screen layout for pointer mapping.
 */
static void validate_or_set_position_hints(struct vbox_private *vbox)
{
	int i, j;
	u16 currentx = 0;
	bool valid = true;

	for (i = 0; i < vbox->num_crtcs; ++i) {
		for (j = 0; j < i; ++j) {
			struct vbva_modehint *hintsi = &vbox->last_mode_hints[i];
			struct vbva_modehint *hintsj = &vbox->last_mode_hints[j];

			if (hintsi->fEnabled && hintsj->fEnabled) {
				if (hintsi->dx >= 0xffff ||
				    hintsi->dy >= 0xffff ||
				    hintsj->dx >= 0xffff ||
				    hintsj->dy >= 0xffff ||
				    (hintsi->dx <
					hintsj->dx + (hintsj->cx & 0x8fff) &&
				     hintsi->dx + (hintsi->cx & 0x8fff) >
					hintsj->dx) ||
				    (hintsi->dy <
					hintsj->dy + (hintsj->cy & 0x8fff) &&
				     hintsi->dy + (hintsi->cy & 0x8fff) >
					hintsj->dy))
					valid = false;
			}
		}
	}
	if (!valid)
		for (i = 0; i < vbox->num_crtcs; ++i) {
			if (vbox->last_mode_hints[i].fEnabled) {
				vbox->last_mode_hints[i].dx = currentx;
				vbox->last_mode_hints[i].dy = 0;
				currentx +=
				    vbox->last_mode_hints[i].cx & 0x8fff;
			}
		}
}

/**
 * Query the host for the most recent video mode hints.
 */
static void vbox_update_mode_hints(struct vbox_private *vbox)
{
	struct drm_device *dev = vbox->dev;
	struct drm_connector *connector;
	struct vbox_connector *vbox_connector;
	struct vbva_modehint *hints;
	u16 flags;
	bool disconnected;
	unsigned int crtc_id;
	int rc;

	rc = hgsmi_get_mode_hints(vbox->guest_pool, vbox->num_crtcs,
				   vbox->last_mode_hints);
	if (RT_FAILURE(rc)) {
		DRM_ERROR("vboxvideo: hgsmi_get_mode_hints failed, rc=%i.\n",
			  rc);
		return;
	}
	validate_or_set_position_hints(vbox);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	drm_modeset_lock_all(dev);
#else
	mutex_lock(&dev->mode_config.mutex);
#endif
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		vbox_connector = to_vbox_connector(connector);
		hints =
		    &vbox->last_mode_hints[vbox_connector->vbox_crtc->crtc_id];
		if (hints->magic == VBVAMODEHINT_MAGIC) {
			disconnected = !(hints->fEnabled);
			crtc_id = vbox_connector->vbox_crtc->crtc_id;
			flags = VBVA_SCREEN_F_ACTIVE
			    | (disconnected ? VBVA_SCREEN_F_DISABLED :
			       VBVA_SCREEN_F_BLANK);
			vbox_connector->mode_hint.width = hints->cx;
			vbox_connector->mode_hint.height = hints->cy;
			vbox_connector->vbox_crtc->x_hint = hints->dx;
			vbox_connector->vbox_crtc->y_hint = hints->dy;
			vbox_connector->mode_hint.disconnected = disconnected;
			if (vbox_connector->vbox_crtc->disconnected !=
			    disconnected) {
				hgsmi_process_display_info(vbox->guest_pool,
							    crtc_id, 0, 0, 0,
							    hints->cx * 4,
							    hints->cx,
							    hints->cy, 0,
							    flags);
				vbox_connector->vbox_crtc->disconnected =
				    disconnected;
			}
		}
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	drm_modeset_unlock_all(dev);
#else
	mutex_unlock(&dev->mode_config.mutex);
#endif
}

static void vbox_hotplug_worker(struct work_struct *work)
{
	struct vbox_private *vbox = container_of(work, struct vbox_private,
						 hotplug_work);

	vbox_update_mode_hints(vbox);
	drm_kms_helper_hotplug_event(vbox->dev);
}

int vbox_irq_init(struct vbox_private *vbox)
{
	int ret;

	vbox_update_mode_hints(vbox);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0) || defined(RHEL_73)
	ret = drm_irq_install(vbox->dev, vbox->dev->pdev->irq);
#else
	ret = drm_irq_install(vbox->dev);
#endif
	if (unlikely(ret != 0)) {
		vbox_irq_fini(vbox);
		DRM_ERROR("Failed installing irq: %d\n", ret);
		return 1;
	}
	INIT_WORK(&vbox->hotplug_work, vbox_hotplug_worker);
	vbox->isr_installed = true;
	return 0;
}

void vbox_irq_fini(struct vbox_private *vbox)
{
	if (vbox->isr_installed) {
		drm_irq_uninstall(vbox->dev);
		flush_work(&vbox->hotplug_work);
		vbox->isr_installed = false;
	}
}
