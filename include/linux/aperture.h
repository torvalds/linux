/* SPDX-License-Identifier: MIT */

#ifndef _LINUX_APERTURE_H_
#define _LINUX_APERTURE_H_

#include <linux/types.h>

struct pci_dev;
struct platform_device;

#if defined(CONFIG_APERTURE_HELPERS)
int devm_aperture_acquire_for_platform_device(struct platform_device *pdev,
					      resource_size_t base,
					      resource_size_t size);

int aperture_remove_conflicting_devices(resource_size_t base, resource_size_t size,
					bool primary, const char *name);

int aperture_remove_conflicting_pci_devices(struct pci_dev *pdev, const char *name);
#else
static inline int devm_aperture_acquire_for_platform_device(struct platform_device *pdev,
							    resource_size_t base,
							    resource_size_t size)
{
	return 0;
}

static inline int aperture_remove_conflicting_devices(resource_size_t base, resource_size_t size,
						      bool primary, const char *name)
{
	return 0;
}

static inline int aperture_remove_conflicting_pci_devices(struct pci_dev *pdev, const char *name)
{
	return 0;
}
#endif

/**
 * aperture_remove_all_conflicting_devices - remove all existing framebuffers
 * @primary: also kick vga16fb if present; only relevant for VGA devices
 * @name: a descriptive name of the requesting driver
 *
 * This function removes all graphics device drivers. Use this function on systems
 * that can have their framebuffer located anywhere in memory.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
static inline int aperture_remove_all_conflicting_devices(bool primary, const char *name)
{
	return aperture_remove_conflicting_devices(0, (resource_size_t)-1, primary, name);
}

#endif
