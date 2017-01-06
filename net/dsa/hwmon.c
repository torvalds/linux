/*
 * net/dsa/hwmon.c - HWMON subsystem support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/ctype.h>
#include <linux/hwmon.h>
#include <net/dsa.h>

#include "dsa_priv.h"

static ssize_t temp1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	int temp, ret;

	ret = ds->ops->get_temp(ds, &temp);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", temp * 1000);
}
static DEVICE_ATTR_RO(temp1_input);

static ssize_t temp1_max_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	int temp, ret;

	ret = ds->ops->get_temp_limit(ds, &temp);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", temp * 1000);
}

static ssize_t temp1_max_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	int temp, ret;

	ret = kstrtoint(buf, 0, &temp);
	if (ret < 0)
		return ret;

	ret = ds->ops->set_temp_limit(ds, DIV_ROUND_CLOSEST(temp, 1000));
	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(temp1_max);

static ssize_t temp1_max_alarm_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	bool alarm;
	int ret;

	ret = ds->ops->get_temp_alarm(ds, &alarm);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", alarm);
}
static DEVICE_ATTR_RO(temp1_max_alarm);

static struct attribute *dsa_hwmon_attrs[] = {
	&dev_attr_temp1_input.attr,	/* 0 */
	&dev_attr_temp1_max.attr,	/* 1 */
	&dev_attr_temp1_max_alarm.attr,	/* 2 */
	NULL
};

static umode_t dsa_hwmon_attrs_visible(struct kobject *kobj,
				       struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct dsa_switch *ds = dev_get_drvdata(dev);
	struct dsa_switch_ops *ops = ds->ops;
	umode_t mode = attr->mode;

	if (index == 1) {
		if (!ops->get_temp_limit)
			mode = 0;
		else if (!ops->set_temp_limit)
			mode &= ~S_IWUSR;
	} else if (index == 2 && !ops->get_temp_alarm) {
		mode = 0;
	}
	return mode;
}

static const struct attribute_group dsa_hwmon_group = {
	.attrs = dsa_hwmon_attrs,
	.is_visible = dsa_hwmon_attrs_visible,
};
__ATTRIBUTE_GROUPS(dsa_hwmon);

void dsa_hwmon_register(struct dsa_switch *ds)
{
	const char *netname = netdev_name(ds->dst->master_netdev);
	char hname[IFNAMSIZ + 1];
	int i, j;

	/* If the switch provides temperature accessors, register with hardware
	 * monitoring subsystem. Treat registration error as non-fatal.
	 */
	if (!ds->ops->get_temp)
		return;

	/* Create valid hwmon 'name' attribute */
	for (i = j = 0; i < IFNAMSIZ && netname[i]; i++) {
		if (isalnum(netname[i]))
			hname[j++] = netname[i];
	}
	hname[j] = '\0';
	scnprintf(ds->hwmon_name, sizeof(ds->hwmon_name), "%s_dsa%d", hname,
		  ds->index);
	ds->hwmon_dev = hwmon_device_register_with_groups(NULL, ds->hwmon_name,
							  ds, dsa_hwmon_groups);
	if (IS_ERR(ds->hwmon_dev)) {
		pr_warn("DSA: failed to register HWMON subsystem for switch %d\n",
			ds->index);
		ds->hwmon_dev = NULL;
	} else {
		pr_info("DSA: registered HWMON subsystem for switch %d\n",
			ds->index);
	}
}

void dsa_hwmon_unregister(struct dsa_switch *ds)
{
	if (ds->hwmon_dev) {
		hwmon_device_unregister(ds->hwmon_dev);
		ds->hwmon_dev = NULL;
	}
}
