// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2017 Dialog Semiconductor
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 */

#ifndef __LINUX_USB_PD_EXT_SDB_H
#define __LINUX_USB_PD_EXT_SDB_H

/* SDB : Status Data Block */
enum usb_pd_ext_sdb_fields {
	USB_PD_EXT_SDB_INTERNAL_TEMP = 0,
	USB_PD_EXT_SDB_PRESENT_INPUT,
	USB_PD_EXT_SDB_PRESENT_BATT_INPUT,
	USB_PD_EXT_SDB_EVENT_FLAGS,
	USB_PD_EXT_SDB_TEMP_STATUS,
	USB_PD_EXT_SDB_DATA_SIZE,
};

/* Event Flags */
#define USB_PD_EXT_SDB_EVENT_OCP		BIT(1)
#define USB_PD_EXT_SDB_EVENT_OTP		BIT(2)
#define USB_PD_EXT_SDB_EVENT_OVP		BIT(3)
#define USB_PD_EXT_SDB_EVENT_CF_CV_MODE		BIT(4)

#define USB_PD_EXT_SDB_PPS_EVENTS	(USB_PD_EXT_SDB_EVENT_OCP |	\
					 USB_PD_EXT_SDB_EVENT_OTP |	\
					 USB_PD_EXT_SDB_EVENT_OVP)

#endif /* __LINUX_USB_PD_EXT_SDB_H */
