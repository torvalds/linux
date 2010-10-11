/*
 * Definitions for building a device tree by calling into the
 * Open Firmware PROM.
 *
 * Copyright (C) 2010  Andres Salomon <dilinger@queued.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_OF_PDT_H
#define _LINUX_OF_PDT_H

extern void *prom_early_alloc(unsigned long size);

/* for building the device tree */
extern void of_pdt_build_devicetree(phandle root_node);

extern void (*prom_build_more)(struct device_node *dp,
		struct device_node ***nextp);

#endif /* _LINUX_OF_PDT_H */
