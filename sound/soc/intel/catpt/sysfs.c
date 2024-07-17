// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/pm_runtime.h>
#include "core.h"

static ssize_t fw_version_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct catpt_dev *cdev = dev_get_drvdata(dev);
	struct catpt_fw_version version;
	int ret;

	ret = pm_runtime_resume_and_get(cdev->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	ret = catpt_ipc_get_fw_version(cdev, &version);

	pm_runtime_mark_last_busy(cdev->dev);
	pm_runtime_put_autosuspend(cdev->dev);

	if (ret)
		return CATPT_IPC_ERROR(ret);

	return sysfs_emit(buf, "%d.%d.%d.%d\n", version.type, version.major,
			  version.minor, version.build);
}
static DEVICE_ATTR_RO(fw_version);

static ssize_t fw_info_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct catpt_dev *cdev = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", cdev->ipc.config.fw_info);
}
static DEVICE_ATTR_RO(fw_info);

static struct attribute *catpt_attrs[] = {
	&dev_attr_fw_version.attr,
	&dev_attr_fw_info.attr,
	NULL
};

static const struct attribute_group catpt_attr_group = {
	.attrs = catpt_attrs,
};

const struct attribute_group *catpt_attr_groups[] = {
	&catpt_attr_group,
	NULL
};
