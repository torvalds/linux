/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 */

#ifndef __SDCA_HID_H__
#define __SDCA_HID_H__

#include <linux/types.h>
#include <linux/hid.h>

#if IS_ENABLED(CONFIG_SND_SOC_SDCA_HID)
int sdca_add_hid_device(struct device *dev, struct sdca_entity *entity);

#else
static inline int sdca_add_hid_device(struct device *dev, struct sdca_entity *entity)
{
	return 0;
}

#endif

#endif /* __SDCA_HID_H__ */
