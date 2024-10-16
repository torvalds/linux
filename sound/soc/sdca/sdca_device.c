// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2024 Intel Corporation

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/acpi.h>
#include <linux/soundwire/sdw.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>

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

static bool sdca_device_quirk_rt712_vb(struct sdw_slave *slave)
{
	struct sdw_slave_id *id = &slave->id;
	int i;

	/*
	 * The RT712_VA relies on the v06r04 draft, and the
	 * RT712_VB on a more recent v08r01 draft.
	 */
	if (slave->sdca_data.interface_revision < 0x0801)
		return false;

	if (id->mfg_id != 0x025d)
		return false;

	if (id->part_id != 0x712 &&
	    id->part_id != 0x713 &&
	    id->part_id != 0x716 &&
	    id->part_id != 0x717)
		return false;

	for (i = 0; i < slave->sdca_data.num_functions; i++) {
		if (slave->sdca_data.sdca_func[i].type ==
		    SDCA_FUNCTION_TYPE_SMART_MIC)
			return true;
	}

	return false;
}

bool sdca_device_quirk_match(struct sdw_slave *slave, enum sdca_quirk quirk)
{
	switch (quirk) {
	case SDCA_QUIRKS_RT712_VB:
		return sdca_device_quirk_rt712_vb(slave);
	default:
		break;
	}
	return false;
}
EXPORT_SYMBOL_NS(sdca_device_quirk_match, SND_SOC_SDCA);
