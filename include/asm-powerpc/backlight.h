/*
 * Routines for handling backlight control on PowerBooks
 *
 * For now, implementation resides in
 * arch/powerpc/platforms/powermac/backlight.c
 *
 */
#ifndef __ASM_POWERPC_BACKLIGHT_H
#define __ASM_POWERPC_BACKLIGHT_H
#ifdef __KERNEL__

#include <linux/fb.h>
#include <linux/mutex.h>

/* For locking instructions, see the implementation file */
extern struct backlight_device *pmac_backlight;
extern struct mutex pmac_backlight_mutex;

extern void pmac_backlight_calc_curve(struct fb_info*);
extern int pmac_backlight_curve_lookup(struct fb_info *info, int value);

extern int pmac_has_backlight_type(const char *type);

extern void pmac_backlight_key_up(void);
extern void pmac_backlight_key_down(void);

extern int pmac_backlight_set_legacy_brightness(int brightness);
extern int pmac_backlight_get_legacy_brightness(void);

#endif /* __KERNEL__ */
#endif
