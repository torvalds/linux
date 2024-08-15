/* SPDX-License-Identifier: MIT */

#ifndef DRM_MODULE_H
#define DRM_MODULE_H

#include <linux/pci.h>
#include <linux/platform_device.h>

#include <drm/drm_drv.h>

/**
 * DOC: overview
 *
 * This library provides helpers registering DRM drivers during module
 * initialization and shutdown. The provided helpers act like bus-specific
 * module helpers, such as module_pci_driver(), but respect additional
 * parameters that control DRM driver registration.
 *
 * Below is an example of initializing a DRM driver for a device on the
 * PCI bus.
 *
 * .. code-block:: c
 *
 *	struct pci_driver my_pci_drv = {
 *	};
 *
 *	drm_module_pci_driver(my_pci_drv);
 *
 * The generated code will test if DRM drivers are enabled and register
 * the PCI driver my_pci_drv. For more complex module initialization, you
 * can still use module_init() and module_exit() in your driver.
 */

/*
 * PCI drivers
 */

static inline int __init drm_pci_register_driver(struct pci_driver *pci_drv)
{
	if (drm_firmware_drivers_only())
		return -ENODEV;

	return pci_register_driver(pci_drv);
}

/**
 * drm_module_pci_driver - Register a DRM driver for PCI-based devices
 * @__pci_drv: the PCI driver structure
 *
 * Registers a DRM driver for devices on the PCI bus. The helper
 * macro behaves like module_pci_driver() but tests the state of
 * drm_firmware_drivers_only(). For more complex module initialization,
 * use module_init() and module_exit() directly.
 *
 * Each module may only use this macro once. Calling it replaces
 * module_init() and module_exit().
 */
#define drm_module_pci_driver(__pci_drv) \
	module_driver(__pci_drv, drm_pci_register_driver, pci_unregister_driver)

static inline int __init
drm_pci_register_driver_if_modeset(struct pci_driver *pci_drv, int modeset)
{
	if (drm_firmware_drivers_only() && modeset == -1)
		return -ENODEV;
	if (modeset == 0)
		return -ENODEV;

	return pci_register_driver(pci_drv);
}

static inline void __exit
drm_pci_unregister_driver_if_modeset(struct pci_driver *pci_drv, int modeset)
{
	pci_unregister_driver(pci_drv);
}

/**
 * drm_module_pci_driver_if_modeset - Register a DRM driver for PCI-based devices
 * @__pci_drv: the PCI driver structure
 * @__modeset: an additional parameter that disables the driver
 *
 * This macro is deprecated and only provided for existing drivers. For
 * new drivers, use drm_module_pci_driver().
 *
 * Registers a DRM driver for devices on the PCI bus. The helper macro
 * behaves like drm_module_pci_driver() with an additional driver-specific
 * flag. If __modeset is 0, the driver has been disabled, if __modeset is
 * -1 the driver state depends on the global DRM state. For all other
 * values, the PCI driver has been enabled. The default should be -1.
 */
#define drm_module_pci_driver_if_modeset(__pci_drv, __modeset) \
	module_driver(__pci_drv, drm_pci_register_driver_if_modeset, \
		      drm_pci_unregister_driver_if_modeset, __modeset)

/*
 * Platform drivers
 */

static inline int __init
drm_platform_driver_register(struct platform_driver *platform_drv)
{
	if (drm_firmware_drivers_only())
		return -ENODEV;

	return platform_driver_register(platform_drv);
}

/**
 * drm_module_platform_driver - Register a DRM driver for platform devices
 * @__platform_drv: the platform driver structure
 *
 * Registers a DRM driver for devices on the platform bus. The helper
 * macro behaves like module_platform_driver() but tests the state of
 * drm_firmware_drivers_only(). For more complex module initialization,
 * use module_init() and module_exit() directly.
 *
 * Each module may only use this macro once. Calling it replaces
 * module_init() and module_exit().
 */
#define drm_module_platform_driver(__platform_drv) \
	module_driver(__platform_drv, drm_platform_driver_register, \
		      platform_driver_unregister)

#endif
