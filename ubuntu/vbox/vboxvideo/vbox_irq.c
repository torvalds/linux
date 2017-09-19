/* $Id: vbox_irq.c $ */
/** @file
 * VirtualBox Additions Linux kernel video driver
 */

/*
 * Copyright (C) 2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 * --------------------------------------------------------------------
 *
 * This code is based on
 * qxl_irq.c
 * with the following copyright and permission notice:
 *
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
 */

#include "vbox_drv.h"

#include <VBox/VBoxVideo.h>

#include <drm/drm_crtc_helper.h>

static void vbox_clear_irq(void)
{
    outl((uint32_t)~0, VGA_PORT_HGSMI_HOST);
}

static uint32_t vbox_get_flags(struct vbox_private *vbox)
{
    return (uint32_t)readl(vbox->mapped_vram + vbox->host_flags_offset);
}

void vbox_report_hotplug(struct vbox_private *vbox)
{
    schedule_work(&vbox->hotplug_work);
}

irqreturn_t vbox_irq_handler(int irq, void *arg)
{
    struct drm_device *dev = (struct drm_device *) arg;
    struct vbox_private *vbox = (struct vbox_private *)dev->dev_private;
    uint32_t host_flags = vbox_get_flags(vbox);

    if (!(host_flags & HGSMIHOSTFLAGS_IRQ))
        return IRQ_NONE;

    /* Due to a bug in the initial host implementation of hot-plug interrupts,
     * the hot-plug and cursor capability flags were never cleared.  Fortunately
     * we can tell when they would have been set by checking that the VSYNC flag
     * is not set. */
    if (   host_flags & (HGSMIHOSTFLAGS_HOTPLUG | HGSMIHOSTFLAGS_CURSOR_CAPABILITIES)
        && !(host_flags & HGSMIHOSTFLAGS_VSYNC))
        vbox_report_hotplug(vbox);
    vbox_clear_irq();
    return IRQ_HANDLED;
}

/**
 * Query the host for
 */
static void vbox_update_mode_hints(struct vbox_private *vbox)
{
    struct drm_device *dev = vbox->dev;
    struct drm_connector *connector;
    struct vbox_connector *vbox_connector;
    struct VBVAMODEHINT *hints;
    uint16_t flags;
    bool disconnected;
    unsigned crtc_id;
    int rc;

    rc = VBoxHGSMIGetModeHints(&vbox->submit_info, vbox->num_crtcs,
                               vbox->last_mode_hints);
    AssertMsgRCReturnVoid(rc, ("VBoxHGSMIGetModeHints failed, rc=%Rrc.\n", rc));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
    drm_modeset_lock_all(dev);
#else
    mutex_lock(&dev->mode_config.mutex);
#endif
    list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
        vbox_connector = to_vbox_connector(connector);
        hints = &vbox->last_mode_hints[vbox_connector->vbox_crtc->crtc_id];
        if (hints->magic == VBVAMODEHINT_MAGIC) {
            LogFunc(("vboxvideo: %d: crtc_id=%u, mode %hdx%hd(enabled:%d),%hdx%hd\n",
                     __LINE__, (unsigned)vbox_connector->vbox_crtc->crtc_id,
                     (short)hints->cx, (short)hints->cy, (int)hints->fEnabled,
                     (short)hints->dx, (short)hints->dy));
            disconnected = !(hints->fEnabled);
            crtc_id = vbox_connector->vbox_crtc->crtc_id;
            flags =   VBVA_SCREEN_F_ACTIVE
                    | (disconnected ? VBVA_SCREEN_F_DISABLED : VBVA_SCREEN_F_BLANK);
            vbox_connector->mode_hint.width = hints->cx & 0x8fff;
            vbox_connector->mode_hint.height = hints->cy & 0x8fff;
            vbox_connector->mode_hint.disconnected = disconnected;
            if (vbox_connector->vbox_crtc->disconnected != disconnected) {
                VBoxHGSMIProcessDisplayInfo(&vbox->submit_info, crtc_id,
                                            0, 0, 0, hints->cx * 4, hints->cx,
                                            hints->cy, 0, flags);
                vbox_connector->vbox_crtc->disconnected = disconnected;
            }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0) || defined(RHEL_73)
            if ((hints->dx < 0xffff) && (hints->dy < 0xffff)) {
                drm_object_property_set_value(&connector->base,
                    dev->mode_config.suggested_x_property, hints->dx & 0x8fff);
                drm_object_property_set_value(&connector->base,
                    dev->mode_config.suggested_y_property, hints->dy & 0x8fff);
            }
#endif
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

    LogFunc(("vboxvideo: %d: vbox=%p\n", __LINE__, vbox));
    vbox_update_mode_hints(vbox);
    drm_kms_helper_hotplug_event(vbox->dev);
}

int vbox_irq_init(struct vbox_private *vbox)
{
    int ret;

    LogFunc(("vboxvideo: %d: vbox=%p\n", __LINE__, vbox));
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
    LogFunc(("vboxvideo: %d: vbox=%p\n", __LINE__, vbox));
    return 0;
}

void vbox_irq_fini(struct vbox_private *vbox)
{
    LogFunc(("vboxvideo: %d: vbox=%p\n", __LINE__, vbox));
    if (vbox->isr_installed) {
        drm_irq_uninstall(vbox->dev);
        flush_work(&vbox->hotplug_work);
        vbox->isr_installed = false;
    }
}
