/* Public domain. */

#ifndef _ACPI_VIDEO_H
#define _ACPI_VIDEO_H

#include <linux/types.h>

static inline void
acpi_video_register(void)
{
}

static inline void
acpi_video_unregister(void)
{
}

static inline void
acpi_video_register_backlight(void)
{
}

static inline bool
acpi_video_backlight_use_native(void)
{
	return true;
}

static inline void
acpi_video_report_nolcd(void)
{
}

enum acpi_backlight_type {
	acpi_backlight_none = 0,
	acpi_backlight_video,
	acpi_backlight_native
};

enum acpi_backlight_type acpi_video_get_backlight_type(void);

#endif
