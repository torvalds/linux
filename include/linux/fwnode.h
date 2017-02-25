/*
 * fwnode.h - Firmware device node object handle type definition.
 *
 * Copyright (C) 2015, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_FWNODE_H_
#define _LINUX_FWNODE_H_

enum fwnode_type {
	FWNODE_INVALID = 0,
	FWNODE_OF,
	FWNODE_ACPI,
	FWNODE_ACPI_DATA,
	FWNODE_ACPI_STATIC,
	FWNODE_PDATA,
	FWNODE_IRQCHIP
};

struct fwnode_handle {
	enum fwnode_type type;
	struct fwnode_handle *secondary;
};

#endif
