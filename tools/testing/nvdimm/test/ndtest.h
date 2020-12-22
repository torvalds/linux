/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NDTEST_H
#define NDTEST_H

#include <linux/platform_device.h>
#include <linux/libnvdimm.h>

struct ndtest_priv {
	struct platform_device pdev;
	struct device_node *dn;
	struct list_head resources;
	struct nvdimm_bus_descriptor bus_desc;
	struct nvdimm_bus *bus;
};

#endif /* NDTEST_H */
