/*
 * Copyright 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * generic videomode description
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_VIDEOMODE_H
#define __LINUX_VIDEOMODE_H

#include <linux/types.h>
#include <video/display_timing.h>

/*
 * Subsystem independent description of a videomode.
 * Can be generated from struct display_timing.
 */
struct videomode {
	unsigned long pixelclock;	/* pixelclock in Hz */

	u32 hactive;
	u32 hfront_porch;
	u32 hback_porch;
	u32 hsync_len;

	u32 vactive;
	u32 vfront_porch;
	u32 vback_porch;
	u32 vsync_len;

	enum display_flags flags; /* display flags */
};

/**
 * videomode_from_timing - convert display timing to videomode
 * @dt: display_timing structure
 * @vm: return value
 *
 * DESCRIPTION:
 * This function converts a struct display_timing to a struct videomode.
 */
void videomode_from_timing(const struct display_timing *dt,
			  struct videomode *vm);

/**
 * videomode_from_timings - convert one display timings entry to videomode
 * @disp: structure with all possible timing entries
 * @vm: return value
 * @index: index into the list of display timings in devicetree
 *
 * DESCRIPTION:
 * This function converts one struct display_timing entry to a struct videomode.
 */
int videomode_from_timings(const struct display_timings *disp,
			  struct videomode *vm, unsigned int index);

#endif
