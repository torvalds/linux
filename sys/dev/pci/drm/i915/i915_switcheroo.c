// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/vga_switcheroo.h>

#include "i915_driver.h"
#include "i915_drv.h"
#include "i915_switcheroo.h"

#ifdef notyet

static void i915_switcheroo_set_state(struct pci_dev *pdev,
				      enum vga_switcheroo_state state)
{
	struct drm_i915_private *i915 = pdev_to_i915(pdev);
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };

	if (!i915) {
		dev_err(&pdev->dev, "DRM not initialized, aborting switch.\n");
		return;
	}
	if (!HAS_DISPLAY(i915)) {
		dev_err(&pdev->dev, "Device state not initialized, aborting switch.\n");
		return;
	}

	if (state == VGA_SWITCHEROO_ON) {
		drm_info(&i915->drm, "switched on\n");
		i915->drm.switch_power_state = DRM_SWITCH_POWER_CHANGING;
		/* i915 resume handler doesn't set to D0 */
		pci_set_power_state(pdev, PCI_D0);
		i915_driver_resume_switcheroo(i915);
		i915->drm.switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		drm_info(&i915->drm, "switched off\n");
		i915->drm.switch_power_state = DRM_SWITCH_POWER_CHANGING;
		i915_driver_suspend_switcheroo(i915, pmm);
		i915->drm.switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static bool i915_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_i915_private *i915 = pdev_to_i915(pdev);

	/*
	 * FIXME: open_count is protected by drm_global_mutex but that would lead to
	 * locking inversion with the driver load path. And the access here is
	 * completely racy anyway. So don't bother with locking for now.
	 */
	return i915 && HAS_DISPLAY(i915) && atomic_read(&i915->drm.open_count) == 0;
}

static const struct vga_switcheroo_client_ops i915_switcheroo_ops = {
	.set_gpu_state = i915_switcheroo_set_state,
	.reprobe = NULL,
	.can_switch = i915_switcheroo_can_switch,
};

#endif /* notyet */

int i915_switcheroo_register(struct drm_i915_private *i915)
{
#ifdef notyet
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	return vga_switcheroo_register_client(pdev, &i915_switcheroo_ops, false);
#else
	return 0;
#endif
}

void i915_switcheroo_unregister(struct drm_i915_private *i915)
{
#ifdef notyet
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	vga_switcheroo_unregister_client(pdev);
#endif
}
