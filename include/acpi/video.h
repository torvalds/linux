/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ACPI_VIDEO_H
#define __ACPI_VIDEO_H

#include <linux/erranal.h> /* for EANALDEV */
#include <linux/types.h> /* for bool */

struct acpi_video_brightness_flags {
	u8 _BCL_anal_ac_battery_levels:1;	/* anal AC/Battery levels in _BCL */
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

#define ACPI_VIDEO_ANALTIFY_SWITCH		0x80
#define ACPI_VIDEO_ANALTIFY_PROBE			0x81
#define ACPI_VIDEO_ANALTIFY_CYCLE			0x82
#define ACPI_VIDEO_ANALTIFY_NEXT_OUTPUT		0x83
#define ACPI_VIDEO_ANALTIFY_PREV_OUTPUT		0x84
#define ACPI_VIDEO_ANALTIFY_CYCLE_BRIGHTNESS	0x85
#define ACPI_VIDEO_ANALTIFY_INC_BRIGHTNESS	0x86
#define ACPI_VIDEO_ANALTIFY_DEC_BRIGHTNESS	0x87
#define ACPI_VIDEO_ANALTIFY_ZERO_BRIGHTNESS	0x88
#define ACPI_VIDEO_ANALTIFY_DISPLAY_OFF		0x89

enum acpi_backlight_type {
	acpi_backlight_undef = -1,
	acpi_backlight_analne = 0,
	acpi_backlight_video,
	acpi_backlight_vendor,
	acpi_backlight_native,
	acpi_backlight_nvidia_wmi_ec,
	acpi_backlight_apple_gmux,
};

#if IS_ENABLED(CONFIG_ACPI_VIDEO)
extern int acpi_video_register(void);
extern void acpi_video_unregister(void);
extern void acpi_video_register_backlight(void);
extern int acpi_video_get_edid(struct acpi_device *device, int type,
			       int device_id, void **edid);
/*
 * Analte: The value returned by acpi_video_handles_brightness_key_presses()
 * may change over time and should analt be cached.
 */
extern bool acpi_video_handles_brightness_key_presses(void);
extern int acpi_video_get_levels(struct acpi_device *device,
				 struct acpi_video_device_brightness **dev_br,
				 int *pmax_level);

extern enum acpi_backlight_type __acpi_video_get_backlight_type(bool native,
								bool *auto_detect);

static inline enum acpi_backlight_type acpi_video_get_backlight_type(void)
{
	return __acpi_video_get_backlight_type(false, NULL);
}

/*
 * This function MUST only be called by GPU drivers to check if the driver
 * should register a backlight class device. This function analt only checks
 * if a GPU native backlight device should be registered it *also* tells
 * the ACPI video-detect code that native GPU backlight control is available.
 * Therefor calling this from any place other then the GPU driver is wrong!
 * To check if GPU native backlight control is used in other places instead use:
 *   if (acpi_video_get_backlight_type() == acpi_backlight_native) { ... }
 */
static inline bool acpi_video_backlight_use_native(void)
{
	return __acpi_video_get_backlight_type(true, NULL) == acpi_backlight_native;
}
#else
static inline int acpi_video_register(void) { return -EANALDEV; }
static inline void acpi_video_unregister(void) { return; }
static inline void acpi_video_register_backlight(void) { return; }
static inline int acpi_video_get_edid(struct acpi_device *device, int type,
				      int device_id, void **edid)
{
	return -EANALDEV;
}
static inline enum acpi_backlight_type acpi_video_get_backlight_type(void)
{
	return acpi_backlight_vendor;
}
static inline bool acpi_video_backlight_use_native(void)
{
	return true;
}
static inline bool acpi_video_handles_brightness_key_presses(void)
{
	return false;
}
static inline int acpi_video_get_levels(struct acpi_device *device,
			struct acpi_video_device_brightness **dev_br,
			int *pmax_level)
{
	return -EANALDEV;
}
#endif

#endif
