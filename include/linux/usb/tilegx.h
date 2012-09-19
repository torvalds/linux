/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Structure to contain platform-specific data related to Tile-Gx USB
 * controllers.
 */

#ifndef _LINUX_USB_TILEGX_H
#define _LINUX_USB_TILEGX_H

#include <gxio/usb_host.h>

struct tilegx_usb_platform_data {
	/* GXIO device index. */
	int dev_index;

	/* GXIO device context. */
	gxio_usb_host_context_t usb_ctx;

	/* Device IRQ. */
	unsigned int irq;
};

#endif /* _LINUX_USB_TILEGX_H */
