// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/stat.h>
/* FIX UP */
#include "soundbus.h"

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct soundbus_dev *sdev = to_soundbus_device(dev);
	struct platform_device *of = &sdev->ofdev;
	int length;

	if (*sdev->modalias) {
		strscpy(buf, sdev->modalias, sizeof(sdev->modalias) + 1);
		strcat(buf, "\n");
		length = strlen(buf);
	} else {
		length = sprintf(buf, "of:N%pOFn%c%s\n",
				 of->dev.of_node, 'T',
                                 of_node_get_device_type(of->dev.of_node));
	}

	return length;
}
static DEVICE_ATTR_RO(modalias);

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct soundbus_dev *sdev = to_soundbus_device(dev);
	struct platform_device *of = &sdev->ofdev;

	return sprintf(buf, "%pOFn\n", of->dev.of_node);
}
static DEVICE_ATTR_RO(name);

static ssize_t type_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct soundbus_dev *sdev = to_soundbus_device(dev);
	struct platform_device *of = &sdev->ofdev;

	return sprintf(buf, "%s\n", of_node_get_device_type(of->dev.of_node));
}
static DEVICE_ATTR_RO(type);

struct attribute *soundbus_dev_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_type.attr,
	&dev_attr_modalias.attr,
	NULL,
};
