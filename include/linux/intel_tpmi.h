/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_tpmi.h: Intel TPMI core external interface
 */

#ifndef _INTEL_TPMI_H_
#define _INTEL_TPMI_H_

#include <linux/bitfield.h>

#define TPMI_VERSION_INVALID	0xff
#define TPMI_MINOR_VERSION(val)	FIELD_GET(GENMASK(4, 0), val)
#define TPMI_MAJOR_VERSION(val)	FIELD_GET(GENMASK(7, 5), val)

/**
 * struct intel_tpmi_plat_info - Platform information for a TPMI device instance
 * @package_id:	CPU Package id
 * @bus_number:	PCI bus number
 * @device_number: PCI device number
 * @function_number: PCI function number
 *
 * Structure to store platform data for a TPMI device instance. This
 * struct is used to return data via tpmi_get_platform_data().
 */
struct intel_tpmi_plat_info {
	u8 package_id;
	u8 bus_number;
	u8 device_number;
	u8 function_number;
};

struct intel_tpmi_plat_info *tpmi_get_platform_data(struct auxiliary_device *auxdev);
struct resource *tpmi_get_resource_at_index(struct auxiliary_device *auxdev, int index);
int tpmi_get_resource_count(struct auxiliary_device *auxdev);

int tpmi_get_feature_status(struct auxiliary_device *auxdev, int feature_id, int *locked,
			    int *disabled);
#endif
