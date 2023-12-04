// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) Intel Corporation, 2022
// Copyright Everest Semiconductor Co.,Ltd

#include <linux/module.h>
#include <linux/acpi.h>
#include "es83xx-dsm-common.h"

/* UUID ("a9800c04-e016-343e-41f4-6bcce70f4332") */
static const guid_t es83xx_dsm_guid =
	GUID_INIT(0xa9800c04, 0xe016, 0x343e,
		  0x41, 0xf4, 0x6b, 0xcc, 0xe7, 0x0f, 0x43, 0x32);

#define ES83xx_DSM_REVID 1

int es83xx_dsm(struct device *dev, int arg, int *value)
{
	acpi_handle dhandle;
	union acpi_object *obj;
	int ret = 0;

	dhandle = ACPI_HANDLE(dev);
	if (!dhandle)
		return -ENOENT;

	obj = acpi_evaluate_dsm(dhandle, &es83xx_dsm_guid, ES83xx_DSM_REVID,
				arg, NULL);
	if (!obj) {
		dev_err(dev, "%s: acpi_evaluate_dsm() failed\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	if (obj->type != ACPI_TYPE_INTEGER) {
		dev_err(dev, "%s: object is not ACPI_TYPE_INTEGER\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	*value = obj->integer.value;
err:
	ACPI_FREE(obj);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(es83xx_dsm);

int es83xx_dsm_dump(struct device *dev)
{
	int value;
	int ret;

	ret = es83xx_dsm(dev, PLATFORM_MAINMIC_TYPE_ARG, &value);
	if (ret < 0)
		return ret;
	dev_info(dev, "PLATFORM_MAINMIC_TYPE %#x\n", value);

	ret = es83xx_dsm(dev, PLATFORM_HPMIC_TYPE_ARG, &value);
	if (ret < 0)
		return ret;
	dev_info(dev, "PLATFORM_HPMIC_TYPE %#x\n", value);

	ret = es83xx_dsm(dev, PLATFORM_SPK_TYPE_ARG, &value);
	if (ret < 0)
		return ret;
	dev_info(dev, "PLATFORM_SPK_TYPE %#x\n", value);

	ret = es83xx_dsm(dev, PLATFORM_HPDET_INV_ARG, &value);
	if (ret < 0)
		return ret;
	dev_info(dev, "PLATFORM_HPDET_INV %#x\n", value);

	ret = es83xx_dsm(dev, PLATFORM_PCM_TYPE_ARG, &value);
	if (ret < 0)
		return ret;
	dev_info(dev, "PLATFORM_PCM_TYPE %#x\n", value);

	ret = es83xx_dsm(dev, PLATFORM_MIC_DE_POP_ARG, &value);
	if (ret < 0)
		return ret;
	dev_info(dev, "PLATFORM_MIC_DE_POP %#x\n", value);

	return 0;
}
EXPORT_SYMBOL_GPL(es83xx_dsm_dump);

MODULE_DESCRIPTION("Everest Semi ES83xx DSM helpers");
MODULE_LICENSE("GPL");
