// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */

#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/acpi.h>
#include <cxl.h>
#include "test/mock.h"

struct acpi_device *to_cxl_host_bridge(struct device *host, struct device *dev)
{
	int index;
	struct acpi_device *adev, *found = NULL;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_bridge(dev)) {
		found = ACPI_COMPANION(dev);
		goto out;
	}

	if (dev_is_platform(dev))
		goto out;

	adev = to_acpi_device(dev);
	if (!acpi_pci_find_root(adev->handle))
		goto out;

	if (strcmp(acpi_device_hid(adev), "ACPI0016") == 0) {
		found = adev;
		dev_dbg(host, "found host bridge %s\n", dev_name(&adev->dev));
	}
out:
	put_cxl_mock_ops(index);
	return found;
}
