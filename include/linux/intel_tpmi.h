/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_tpmi.h: Intel TPMI core external interface
 */

#ifndef _INTEL_TPMI_H_
#define _INTEL_TPMI_H_

#include <linux/bitfield.h>

struct oobmsm_plat_info;

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
	TPMI_ID_PLR = 0xc,	/* Performance Limit Reasons */
	TPMI_CONTROL_ID = 0x80,	/* Special ID for getting feature status */
	TPMI_INFO_ID = 0x81,	/* Special ID for PCI BDF and Package ID information */
};

struct oobmsm_plat_info *tpmi_get_platform_data(struct auxiliary_device *auxdev);
struct resource *tpmi_get_resource_at_index(struct auxiliary_device *auxdev, int index);
int tpmi_get_resource_count(struct auxiliary_device *auxdev);
int tpmi_get_feature_status(struct auxiliary_device *auxdev, int feature_id, bool *read_blocked,
			    bool *write_blocked);
struct dentry *tpmi_get_debugfs_dir(struct auxiliary_device *auxdev);
#endif
