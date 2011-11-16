Notes on integrating the Mali DRM module:

The Mali DRM is a platform device, meaning that you have to add an entry for it in your kernel architecture specification.

Example: (arch/arm/mach-<platform>/mach-<platform>.c)

#ifdef CONFIG_DRM_MALI
static struct platform_device <platform>_device_mali_drm = {
	.name = "mali_drm",
	.id   = -1,
};
#endif

static struct platform_device *<platform>_devices[] __initdata = {
...
#ifdef CONFIG_DRM_MALI
	&<platform>_device_mali_drm,
#endif
...
};

Where <platform> is substituted with the selected platform.

The "mali" folder should be placed under drivers/gpu/drm/
