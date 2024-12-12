// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Vijendar Mukunda <Vijendar.Mukunda@amd.com>

/*
 * SDW AMD ACPI scan helper function
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/fwnode.h>
#include <linux/module.h>
#include <linux/soundwire/sdw_amd.h>
#include <linux/string.h>

int amd_sdw_scan_controller(struct sdw_amd_acpi_info *info)
{
	struct acpi_device *adev = acpi_fetch_acpi_dev(info->handle);
	u32 sdw_bitmap = 0;
	u8 count = 0;
	int ret;

	if (!adev)
		return -EINVAL;

	/* Found controller, find links supported */
	ret = fwnode_property_read_u32_array(acpi_fwnode_handle(adev),
					     "mipi-sdw-manager-list", &sdw_bitmap, 1);
	if (ret) {
		dev_err(&adev->dev,
			"Failed to read mipi-sdw-manager-list: %d\n", ret);
		return -EINVAL;
	}
	count = hweight32(sdw_bitmap);
	/* Check count is within bounds */
	if (count > info->count) {
		dev_err(&adev->dev, "Manager count %d exceeds max %d\n",
			count, info->count);
		return -EINVAL;
	}

	if (!count) {
		dev_dbg(&adev->dev, "No SoundWire Managers detected\n");
		return -EINVAL;
	}
	dev_dbg(&adev->dev, "ACPI reports %d SoundWire Manager devices\n", count);
	info->link_mask = sdw_bitmap;
	return 0;
}
EXPORT_SYMBOL_NS(amd_sdw_scan_controller, "SND_AMD_SOUNDWIRE_ACPI");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("AMD SoundWire ACPI helpers");
