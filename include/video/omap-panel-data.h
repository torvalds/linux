/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header containing platform_data structs for omap panels
 *
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *	   Archit Taneja <archit@ti.com>
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Mayuresh Janorkar <mayur@ti.com>
 *
 * Copyright (C) 2010 Canonical Ltd.
 * Author: Bryan Wu <bryan.wu@canonical.com>
 */

#ifndef __OMAP_PANEL_DATA_H
#define __OMAP_PANEL_DATA_H

#include <video/display_timing.h>

/**
 * connector_atv platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @invert_polarity: invert signal polarity
 */
struct connector_atv_platform_data {
	const char *name;
	const char *source;

	bool invert_polarity;
};

#endif /* __OMAP_PANEL_DATA_H */
