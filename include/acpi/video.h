#ifndef __ACPI_VIDEO_H
#define __ACPI_VIDEO_H

#include <linux/errno.h> /* for ENODEV */
#include <linux/types.h> /* for bool */

struct acpi_video_brightness_flags {
	u8 _BCL_no_ac_battery_levels:1;	/* no AC/Battery levels in _BCL */
	u8 _BCL_reversed:1;		/* _BCL package is in a reversed order */
	u8 _BQC_use_index:1;		/* _BQC returns an index value */
};

struct acpi_video_device_brightness {
	int curr;
	int count;
	int *levels;
	struct acpi_video_brightness_flags flags;
};

struct acpi_device;

#define ACPI_VIDEO_CLASS	"video"

#define ACPI_VIDEO_DISPLAY_CRT  1
#define ACPI_VIDEO_DISPLAY_TV   2
#define ACPI_VIDEO_DISPLAY_DVI  3
#define ACPI_VIDEO_DISPLAY_LCD  4

#define ACPI_VIDEO_DISPLAY_LEGACY_MONITOR 0x0100
#define ACPI_VIDEO_DISPLAY_LEGACY_PANEL   0x0110
#define ACPI_VIDEO_DISPLAY_LEGACY_TV      0x0200

enum acpi_backlight_type {
	acpi_backlight_undef = -1,
	acpi_backlight_none = 0,
	acpi_backlight_video,
	acpi_backlight_vendor,
	acpi_backlight_native,
};

#if IS_ENABLED(CONFIG_ACPI_VIDEO)
extern int acpi_video_register(void);
extern void acpi_video_unregister(void);
extern int acpi_video_get_edid(struct acpi_device *device, int type,
			       int device_id, void **edid);
extern enum acpi_backlight_type acpi_video_get_backlight_type(void);
extern void acpi_video_set_dmi_backlight_type(enum acpi_backlight_type type);
/*
 * Note: The value returned by acpi_video_handles_brightness_key_presses()
 * may change over time and should not be cached.
 */
extern bool acpi_video_handles_brightness_key_presses(void);
extern int acpi_video_get_levels(struct acpi_device *device,
				 struct acpi_video_device_brightness **dev_br,
				 int *pmax_level);
#else
static inline int acpi_video_register(void) { return 0; }
static inline void acpi_video_unregister(void) { return; }
static inline int acpi_video_get_edid(struct acpi_device *device, int type,
				      int device_id, void **edid)
{
	return -ENODEV;
}
static inline enum acpi_backlight_type acpi_video_get_backlight_type(void)
{
	return acpi_backlight_vendor;
}
static inline void acpi_video_set_dmi_backlight_type(enum acpi_backlight_type type)
{
}
static inline bool acpi_video_handles_brightness_key_presses(void)
{
	return false;
}
static inline int acpi_video_get_levels(struct acpi_device *device,
			struct acpi_video_device_brightness **dev_br,
			int *pmax_level)
{
	return -ENODEV;
}
#endif

#endif
