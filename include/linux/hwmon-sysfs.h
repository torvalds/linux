/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  hwmon-sysfs.h - hardware monitoring chip driver sysfs defines
 *
 *  Copyright (C) 2005 Yani Ioannou <yani.ioannou@gmail.com>
 */
#ifndef _LINUX_HWMON_SYSFS_H
#define _LINUX_HWMON_SYSFS_H

#include <linux/device.h>
#include <linux/kstrtox.h>

struct sensor_device_attribute{
	struct device_attribute dev_attr;
	int index;
};
#define to_sensor_dev_attr(_dev_attr) \
	container_of(_dev_attr, struct sensor_device_attribute, dev_attr)

#define SENSOR_ATTR(_name, _mode, _show, _store, _index)	\
	{ .dev_attr = __ATTR(_name, _mode, _show, _store),	\
	  .index = _index }

#define SENSOR_ATTR_RO(_name, _func, _index)			\
	SENSOR_ATTR(_name, 0444, _func##_show, NULL, _index)

#define SENSOR_ATTR_RW(_name, _func, _index)			\
	SENSOR_ATTR(_name, 0644, _func##_show, _func##_store, _index)

#define SENSOR_ATTR_WO(_name, _func, _index)			\
	SENSOR_ATTR(_name, 0200, NULL, _func##_store, _index)

#define SENSOR_DEVICE_ATTR(_name, _mode, _show, _store, _index)	\
struct sensor_device_attribute sensor_dev_attr_##_name		\
	= SENSOR_ATTR(_name, _mode, _show, _store, _index)

#define SENSOR_DEVICE_ATTR_RO(_name, _func, _index)		\
	SENSOR_DEVICE_ATTR(_name, 0444, _func##_show, NULL, _index)

#define SENSOR_DEVICE_ATTR_RW(_name, _func, _index)		\
	SENSOR_DEVICE_ATTR(_name, 0644, _func##_show, _func##_store, _index)

#define SENSOR_DEVICE_ATTR_WO(_name, _func, _index)		\
	SENSOR_DEVICE_ATTR(_name, 0200, NULL, _func##_store, _index)

struct sensor_device_attribute_2 {
	struct device_attribute dev_attr;
	u8 index;
	u8 nr;
};
#define to_sensor_dev_attr_2(_dev_attr) \
	container_of(_dev_attr, struct sensor_device_attribute_2, dev_attr)

#define SENSOR_ATTR_2(_name, _mode, _show, _store, _nr, _index)	\
	{ .dev_attr = __ATTR(_name, _mode, _show, _store),	\
	  .index = _index,					\
	  .nr = _nr }

#define SENSOR_ATTR_2_RO(_name, _func, _nr, _index)		\
	SENSOR_ATTR_2(_name, 0444, _func##_show, NULL, _nr, _index)

#define SENSOR_ATTR_2_RW(_name, _func, _nr, _index)		\
	SENSOR_ATTR_2(_name, 0644, _func##_show, _func##_store, _nr, _index)

#define SENSOR_ATTR_2_WO(_name, _func, _nr, _index)		\
	SENSOR_ATTR_2(_name, 0200, NULL, _func##_store, _nr, _index)

#define SENSOR_DEVICE_ATTR_2(_name,_mode,_show,_store,_nr,_index)	\
struct sensor_device_attribute_2 sensor_dev_attr_##_name		\
	= SENSOR_ATTR_2(_name, _mode, _show, _store, _nr, _index)

#define SENSOR_DEVICE_ATTR_2_RO(_name, _func, _nr, _index)		\
	SENSOR_DEVICE_ATTR_2(_name, 0444, _func##_show, NULL,		\
			     _nr, _index)

#define SENSOR_DEVICE_ATTR_2_RW(_name, _func, _nr, _index)		\
	SENSOR_DEVICE_ATTR_2(_name, 0644, _func##_show, _func##_store,	\
			     _nr, _index)

#define SENSOR_DEVICE_ATTR_2_WO(_name, _func, _nr, _index)		\
	SENSOR_DEVICE_ATTR_2(_name, 0200, NULL, _func##_store,		\
			     _nr, _index)

#endif /* _LINUX_HWMON_SYSFS_H */
