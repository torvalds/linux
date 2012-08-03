/*
 * Copyright (C) 2011-2012 by Texas Instruments
 *
 * The Inventra Controller Driver for Linux is free software; you
 * can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 2 as published by the Free Software
 * Foundation.
 */

#ifndef __MUSB_OMAP_H__
#define __MUSB_OMAP_H__

enum omap_musb_vbus_id_status {
	OMAP_MUSB_UNKNOWN = 0,
	OMAP_MUSB_ID_GROUND,
	OMAP_MUSB_ID_FLOAT,
	OMAP_MUSB_VBUS_VALID,
	OMAP_MUSB_VBUS_OFF,
};

#if (defined(CONFIG_USB_MUSB_OMAP2PLUS) || \
				defined(CONFIG_USB_MUSB_OMAP2PLUS_MODULE))
void omap_musb_mailbox(enum omap_musb_vbus_id_status status);
#else
static inline void omap_musb_mailbox(enum omap_musb_vbus_id_status status)
{
}
#endif

#endif	/* __MUSB_OMAP_H__ */
