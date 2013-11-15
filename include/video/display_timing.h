/*
 * Copyright 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * description of display timings
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_DISPLAY_TIMING_H
#define __LINUX_DISPLAY_TIMING_H

#include <linux/bitops.h>
#include <linux/types.h>

enum display_flags {
	DISPLAY_FLAGS_HSYNC_LOW		= BIT(0),
	DISPLAY_FLAGS_HSYNC_HIGH	= BIT(1),
	DISPLAY_FLAGS_VSYNC_LOW		= BIT(2),
	DISPLAY_FLAGS_VSYNC_HIGH	= BIT(3),

	/* data enable flag */
	DISPLAY_FLAGS_DE_LOW		= BIT(4),
	DISPLAY_FLAGS_DE_HIGH		= BIT(5),
	/* drive data on pos. edge */
	DISPLAY_FLAGS_PIXDATA_POSEDGE	= BIT(6),
	/* drive data on neg. edge */
	DISPLAY_FLAGS_PIXDATA_NEGEDGE	= BIT(7),
	DISPLAY_FLAGS_INTERLACED	= BIT(8),
	DISPLAY_FLAGS_DOUBLESCAN	= BIT(9),
	DISPLAY_FLAGS_DOUBLECLK		= BIT(10),
};

/*
 * A single signal can be specified via a range of minimal and maximal values
 * with a typical value, that lies somewhere inbetween.
 */
struct timing_entry {
	u32 min;
	u32 typ;
	u32 max;
};

/*
 * Single "mode" entry. This describes one set of signal timings a display can
 * have in one setting. This struct can later be converted to struct videomode
 * (see include/video/videomode.h). As each timing_entry can be defined as a
 * range, one struct display_timing may become multiple struct videomodes.
 *
 * Example: hsync active high, vsync active low
 *
 *				    Active Video
 * Video  ______________________XXXXXXXXXXXXXXXXXXXXXX_____________________
 *	  |<- sync ->|<- back ->|<----- active ----->|<- front ->|<- sync..
 *	  |	     |	 porch  |		     |	 porch	 |
 *
 * HSync _|¯¯¯¯¯¯¯¯¯¯|___________________________________________|¯¯¯¯¯¯¯¯¯
 *
 * VSync ¯|__________|¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯|_________
 */
struct display_timing {
	struct timing_entry pixelclock;

	struct timing_entry hactive;		/* hor. active video */
	struct timing_entry hfront_porch;	/* hor. front porch */
	struct timing_entry hback_porch;	/* hor. back porch */
	struct timing_entry hsync_len;		/* hor. sync len */

	struct timing_entry vactive;		/* ver. active video */
	struct timing_entry vfront_porch;	/* ver. front porch */
	struct timing_entry vback_porch;	/* ver. back porch */
	struct timing_entry vsync_len;		/* ver. sync len */

	enum display_flags flags;		/* display flags */
};

/*
 * This describes all timing settings a display provides.
 * The native_mode is the default setting for this display.
 * Drivers that can handle multiple videomodes should work with this struct and
 * convert each entry to the desired end result.
 */
struct display_timings {
	unsigned int num_timings;
	unsigned int native_mode;

	struct display_timing **timings;
};

/* get one entry from struct display_timings */
static inline struct display_timing *display_timings_get(const struct
							 display_timings *disp,
							 unsigned int index)
{
	if (disp->num_timings > index)
		return disp->timings[index];
	else
		return NULL;
}

void display_timings_release(struct display_timings *disp);

#endif
