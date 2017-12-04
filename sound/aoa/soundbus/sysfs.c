// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/stat.h>
/* FIX UP */
#include "soundbus.h"

#define soundbus_config_of_attr(field, format_string)			\
static ssize_t								\
field##_show (struct device *dev, struct device_attribute *attr,	\
              char *buf)						\
{									\
	struct soundbus_dev *mdev = to_soundbus_device (dev);		\
	return sprintf (buf, format_string, mdev->ofdev.dev.of_node->field); \
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct soundbus_dev *sdev = to_soundbus_device(dev);
	struct platform_device *of = &sdev->ofdev;
	int length;

	if (*sdev->modalias) {
		strlcpy(buf, sdev->modalias, sizeof(sdev->modalias) + 1);
		strcat(buf, "\n");
		length = strlen(buf);
	} else {
		length = sprintf(buf, "of:N%sT%s\n",
				 of->dev.of_node->name, of->dev.of_node->type);
	}

	return length;
}
static DEVICE_ATTR_RO(modalias);

soundbus_config_of_attr (name, "%s\n");
static DEVICE_ATTR_RO(name);
soundbus_config_of_attr (type, "%s\n");
static DEVICE_ATTR_RO(type);

struct attribute *soundbus_dev_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_type.attr,
	&dev_attr_modalias.attr,
	NULL,
};
