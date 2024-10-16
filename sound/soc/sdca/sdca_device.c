// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2024 Intel Corporation

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/acpi.h>
#include <linux/soundwire/sdw.h>
#include <sound/sdca.h>

void sdca_lookup_interface_revision(struct sdw_slave *slave)
{
	struct fwnode_handle *fwnode = slave->dev.fwnode;

	/*
	 * if this property is not present, then the sdca_interface_revision will
	 * remain zero, which will be considered as 'not defined' or 'invalid'.
	 */
	fwnode_property_read_u32(fwnode, "mipi-sdw-sdca-interface-revision",
				 &slave->sdca_data.interface_revision);
}
EXPORT_SYMBOL_NS(sdca_lookup_interface_revision, SND_SOC_SDCA);
