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

/*
 * List of supported TMPI IDs.
 * Some TMPI IDs are not used by Linux, so the numbers are not consecutive.
 */
enum intel_tpmi_id {
	TPMI_ID_RAPL = 0,	/* Running Average Power Limit */
	TPMI_ID_PEM = 1,	/* Power and Perf excursion Monitor */
	TPMI_ID_UNCORE = 2,	/* Uncore Frequency Scaling */
	TPMI_ID_SST = 5,	/* Speed Select Technology */
	TPMI_CONTROL_ID = 0x80,	/* Special ID for getting feature status */
	TPMI_INFO_ID = 0x81,	/* Special ID for PCI BDF and Package ID information */
};

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
int tpmi_get_feature_status(struct auxiliary_device *auxdev, int feature_id, bool *read_blocked,
			    bool *write_blocked);
#endif
