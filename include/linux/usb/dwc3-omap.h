/*
 * Copyright (C) 2013 by Texas Instruments
 *
 * The Inventra Controller Driver for Linux is free software; you
 * can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 2 as published by the Free Software
 * Foundation.
 */

#ifndef __DWC3_OMAP_H__
#define __DWC3_OMAP_H__

enum omap_dwc3_vbus_id_status {
	OMAP_DWC3_UNKNOWN = 0,
	OMAP_DWC3_ID_GROUND,
	OMAP_DWC3_ID_FLOAT,
	OMAP_DWC3_VBUS_VALID,
	OMAP_DWC3_VBUS_OFF,
};

#if (defined(CONFIG_USB_DWC3) || defined(CONFIG_USB_DWC3_MODULE))
extern void dwc3_omap_mailbox(enum omap_dwc3_vbus_id_status status);
#else
static inline void dwc3_omap_mailbox(enum omap_dwc3_vbus_id_status status)
{
	return;
}
#endif

#endif	/* __DWC3_OMAP_H__ */
