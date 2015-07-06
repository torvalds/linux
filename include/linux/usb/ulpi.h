/*
 * ulpi.h -- ULPI defines and function prorotypes
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * version 2 of that License.
 */

#ifndef __LINUX_USB_ULPI_H
#define __LINUX_USB_ULPI_H

#include <linux/usb/otg.h>
#include <linux/ulpi/regs.h>

/*-------------------------------------------------------------------------*/

/*
 * ULPI Flags
 */
#define ULPI_OTG_ID_PULLUP		(1 << 0)
#define ULPI_OTG_DP_PULLDOWN_DIS	(1 << 1)
#define ULPI_OTG_DM_PULLDOWN_DIS	(1 << 2)
#define ULPI_OTG_DISCHRGVBUS		(1 << 3)
#define ULPI_OTG_CHRGVBUS		(1 << 4)
#define ULPI_OTG_DRVVBUS		(1 << 5)
#define ULPI_OTG_DRVVBUS_EXT		(1 << 6)
#define ULPI_OTG_EXTVBUSIND		(1 << 7)

#define ULPI_IC_6PIN_SERIAL		(1 << 8)
#define ULPI_IC_3PIN_SERIAL		(1 << 9)
#define ULPI_IC_CARKIT			(1 << 10)
#define ULPI_IC_CLKSUSPM		(1 << 11)
#define ULPI_IC_AUTORESUME		(1 << 12)
#define ULPI_IC_EXTVBUS_INDINV		(1 << 13)
#define ULPI_IC_IND_PASSTHRU		(1 << 14)
#define ULPI_IC_PROTECT_DIS		(1 << 15)

#define ULPI_FC_HS			(1 << 16)
#define ULPI_FC_FS			(1 << 17)
#define ULPI_FC_LS			(1 << 18)
#define ULPI_FC_FS4LS			(1 << 19)
#define ULPI_FC_TERMSEL			(1 << 20)
#define ULPI_FC_OP_NORM			(1 << 21)
#define ULPI_FC_OP_NODRV		(1 << 22)
#define ULPI_FC_OP_DIS_NRZI		(1 << 23)
#define ULPI_FC_OP_NSYNC_NEOP		(1 << 24)
#define ULPI_FC_RST			(1 << 25)
#define ULPI_FC_SUSPM			(1 << 26)

/*-------------------------------------------------------------------------*/

#if IS_ENABLED(CONFIG_USB_ULPI)
struct usb_phy *otg_ulpi_create(struct usb_phy_io_ops *ops,
					unsigned int flags);
#else
static inline struct usb_phy *otg_ulpi_create(struct usb_phy_io_ops *ops,
					      unsigned int flags)
{
	return NULL;
}
#endif

#ifdef CONFIG_USB_ULPI_VIEWPORT
/* access ops for controllers with a viewport register */
extern struct usb_phy_io_ops ulpi_viewport_access_ops;
#endif

#endif /* __LINUX_USB_ULPI_H */
