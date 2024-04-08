// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-2021 Intel Corporation.

/*
 * SDW Intel ACPI scan helpers
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/fwnode.h>
#include <linux/module.h>
#include <linux/soundwire/sdw_intel.h>
#include <linux/string.h>

#define SDW_LINK_TYPE		4 /* from Intel ACPI documentation */
#define SDW_MAX_LINKS		4

static int ctrl_link_mask;
module_param_named(sdw_link_mask, ctrl_link_mask, int, 0444);
MODULE_PARM_DESC(sdw_link_mask, "Intel link mask (one bit per link)");

static ulong ctrl_addr = 0x40000000;
module_param_named(sdw_ctrl_addr, ctrl_addr, ulong, 0444);
MODULE_PARM_DESC(sdw_ctrl_addr, "Intel SoundWire Controller _ADR");

static bool is_link_enabled(struct fwnode_handle *fw_node, u8 idx)
{
	struct fwnode_handle *link;
	char name[32];
	u32 quirk_mask = 0;

	/* Find master handle */
	snprintf(name, sizeof(name),
		 "mipi-sdw-link-%hhu-subproperties", idx);

	link = fwnode_get_named_child_node(fw_node, name);
	if (!link)
		return false;

	fwnode_property_read_u32(link,
				 "intel-quirk-mask",
				 &quirk_mask);

	if (quirk_mask & SDW_INTEL_QUIRK_MASK_BUS_DISABLE)
		return false;

	return true;
}

static int
sdw_intel_scan_controller(struct sdw_intel_acpi_info *info)
{
	struct acpi_device *adev = acpi_fetch_acpi_dev(info->handle);
	u8 count, i;
	int ret;

	if (!adev)
		return -EINVAL;

	/* Found controller, find links supported */
	count = 0;
	ret = fwnode_property_read_u8_array(acpi_fwnode_handle(adev),
					    "mipi-sdw-master-count", &count, 1);

	/*
	 * In theory we could check the number of links supported in
	 * hardware, but in that step we cannot assume SoundWire IP is
	 * powered.
	 *
	 * In addition, if the BIOS doesn't even provide this
	 * 'master-count' property then all the inits based on link
	 * masks will fail as well.
	 *
	 * We will check the hardware capabilities in the startup() step
	 */

	if (ret) {
		dev_err(&adev->dev,
			"Failed to read mipi-sdw-master-count: %d\n", ret);
		return -EINVAL;
	}

	/* Check count is within bounds */
	if (count > SDW_MAX_LINKS) {
		dev_err(&adev->dev, "Link count %d exceeds max %d\n",
			count, SDW_MAX_LINKS);
		return -EINVAL;
	}

	if (!count) {
		dev_warn(&adev->dev, "No SoundWire links detected\n");
		return -EINVAL;
	}
	dev_dbg(&adev->dev, "ACPI reports %d SDW Link devices\n", count);

	info->count = count;
	info->link_mask = 0;

	for (i = 0; i < count; i++) {
		if (ctrl_link_mask && !(ctrl_link_mask & BIT(i))) {
			dev_dbg(&adev->dev,
				"Link %d masked, will not be enabled\n", i);
			continue;
		}

		if (!is_link_enabled(acpi_fwnode_handle(adev), i)) {
			dev_dbg(&adev->dev,
				"Link %d not selected in firmware\n", i);
			continue;
		}

		info->link_mask |= BIT(i);
	}

	return 0;
}

static acpi_status sdw_intel_acpi_cb(acpi_handle handle, u32 level,
				     void *cdata, void **return_value)
{
	struct sdw_intel_acpi_info *info = cdata;
	acpi_status status;
	u64 adr;

	status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL, &adr);
	if (ACPI_FAILURE(status))
		return AE_OK; /* keep going */

	if (!acpi_fetch_acpi_dev(handle)) {
		pr_err("%s: Couldn't find ACPI handle\n", __func__);
		return AE_NOT_FOUND;
	}

	/*
	 * On some Intel platforms, multiple children of the HDAS
	 * device can be found, but only one of them is the SoundWire
	 * controller. The SNDW device is always exposed with
	 * Name(_ADR, 0x40000000), with bits 31..28 representing the
	 * SoundWire link so filter accordingly
	 */
	if (FIELD_GET(GENMASK(31, 28), adr) != SDW_LINK_TYPE)
		return AE_OK; /* keep going */

	if (adr != ctrl_addr)
		return AE_OK; /* keep going */

	/* found the correct SoundWire controller */
	info->handle = handle;

	/* device found, stop namespace walk */
	return AE_CTRL_TERMINATE;
}

/**
 * sdw_intel_acpi_scan() - SoundWire Intel init routine
 * @parent_handle: ACPI parent handle
 * @info: description of what firmware/DSDT tables expose
 *
 * This scans the namespace and queries firmware to figure out which
 * links to enable. A follow-up use of sdw_intel_probe() and
 * sdw_intel_startup() is required for creation of devices and bus
 * startup
 */
int sdw_intel_acpi_scan(acpi_handle *parent_handle,
			struct sdw_intel_acpi_info *info)
{
	acpi_status status;

	info->handle = NULL;
	/*
	 * In the HDAS ACPI scope, 'SNDW' may be either the child of
	 * 'HDAS' or the grandchild of 'HDAS'. So let's go through
	 * the ACPI from 'HDAS' at max depth of 2 to find the 'SNDW'
	 * device.
	 */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE,
				     parent_handle, 2,
				     sdw_intel_acpi_cb,
				     NULL, info, NULL);
	if (ACPI_FAILURE(status) || info->handle == NULL)
		return -ENODEV;

	return sdw_intel_scan_controller(info);
}
EXPORT_SYMBOL_NS(sdw_intel_acpi_scan, SND_INTEL_SOUNDWIRE_ACPI);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Intel Soundwire ACPI helpers");
