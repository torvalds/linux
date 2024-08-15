// SPDX-License-Identifier: GPL-2.0
/*
 * USB CDC Device Management subdriver
 *
 * Copyright (c) 2012  Bjørn Mork <bjorn@mork.no>
 */

#ifndef __LINUX_USB_CDC_WDM_H
#define __LINUX_USB_CDC_WDM_H

#include <linux/wwan.h>
#include <uapi/linux/usb/cdc-wdm.h>

extern struct usb_driver *usb_cdc_wdm_register(struct usb_interface *intf,
					struct usb_endpoint_descriptor *ep,
					int bufsize, enum wwan_port_type type,
					int (*manage_power)(struct usb_interface *, int));

#endif /* __LINUX_USB_CDC_WDM_H */
