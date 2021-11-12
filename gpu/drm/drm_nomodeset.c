// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/types.h>

static bool drm_nomodeset;

bool drm_firmware_drivers_only(void)
{
	return drm_nomodeset;
}
EXPORT_SYMBOL(drm_firmware_drivers_only);

static int __init disable_modeset(char *str)
{
	drm_nomodeset = true;

	pr_warn("You have booted with nomodeset. This means your GPU drivers are DISABLED\n");
	pr_warn("Any video related functionality will be severely degraded, and you may not even be able to suspend the system properly\n");
	pr_warn("Unless you actually understand what nomodeset does, you should reboot without enabling it\n");

	return 1;
}

/* Disable kernel modesetting */
__setup("nomodeset", disable_modeset);
