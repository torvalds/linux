// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2024 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/sysfs.h>
#include "avs.h"

static ssize_t fw_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct avs_dev *adev = to_avs_dev(dev);
	struct avs_fw_version *fw_version = &adev->fw_cfg.fw_version;

	return sysfs_emit(buf, "%d.%d.%d.%d\n", fw_version->major, fw_version->minor,
			  fw_version->hotfix, fw_version->build);
}
static DEVICE_ATTR_RO(fw_version);

static struct attribute *avs_fw_attrs[] = {
	&dev_attr_fw_version.attr,
	NULL
};

static const struct attribute_group avs_attr_group = {
	.name = "avs",
	.attrs = avs_fw_attrs,
};

const struct attribute_group *avs_attr_groups[] = {
	&avs_attr_group,
	NULL
};
