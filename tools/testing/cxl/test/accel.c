// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2026 Intel Corporation. All rights reserved.

#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <cxl/mailbox.h>
#include <cxlmem.h>

struct mock_cxl_accel {
	struct cxl_dev_state cxlds;
	struct cxl_memdev *cxlmd;
};

static int cxl_mock_accel_probe(struct platform_device *pdev)
{
	struct mock_cxl_accel *cxl_accel;
	struct device *dev = &pdev->dev;
	struct cxl_dev_state *cxlds;
	struct cxl_memdev *cxlmd;
	struct range mock_range;
	int rc;

	cxl_accel = devm_cxl_dev_state_create(&pdev->dev, CXL_DEVTYPE_DEVMEM,
					      pdev->id + 1, 0,
					      struct mock_cxl_accel, cxlds,
					      false);
	if (!cxl_accel)
		return -ENOMEM;

	cxlds = &cxl_accel->cxlds;
	cxlds->media_ready = true;
	rc = cxl_set_capacity(cxlds, SZ_512M);
	if (rc)
		return rc;

	cxlmd = devm_cxl_probe_mem(cxlds, &mock_range);
	if (IS_ERR(cxlmd))
		return PTR_ERR(cxlmd);
	cxl_accel->cxlmd = cxlmd;

	dev_dbg(dev, "Probed mock accelerator with range %pra\n", &mock_range);

	return 0;
}

static const struct platform_device_id cxl_mock_accel_ids[] = {
	{ .name = "cxl_type2_accel" },
	{ }
};
MODULE_DEVICE_TABLE(platform, cxl_mock_accel_ids);

static struct platform_driver cxl_mock_accel_driver = {
	.probe = cxl_mock_accel_probe,
	.id_table = cxl_mock_accel_ids,
	.driver = {
		.name = KBUILD_MODNAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

module_platform_driver(cxl_mock_accel_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("cxl_test: accelerator device mock module");
MODULE_IMPORT_NS("CXL");
