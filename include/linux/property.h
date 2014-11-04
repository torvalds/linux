/*
 * property.h - Unified device property interface.
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_PROPERTY_H_
#define _LINUX_PROPERTY_H_

#include <linux/types.h>

struct device;

enum dev_prop_type {
	DEV_PROP_U8,
	DEV_PROP_U16,
	DEV_PROP_U32,
	DEV_PROP_U64,
	DEV_PROP_STRING,
	DEV_PROP_MAX,
};

bool device_property_present(struct device *dev, const char *propname);
int device_property_read_u8_array(struct device *dev, const char *propname,
				  u8 *val, size_t nval);
int device_property_read_u16_array(struct device *dev, const char *propname,
				   u16 *val, size_t nval);
int device_property_read_u32_array(struct device *dev, const char *propname,
				   u32 *val, size_t nval);
int device_property_read_u64_array(struct device *dev, const char *propname,
				   u64 *val, size_t nval);
int device_property_read_string_array(struct device *dev, const char *propname,
				      const char **val, size_t nval);
int device_property_read_string(struct device *dev, const char *propname,
				const char **val);

static inline bool device_property_read_bool(struct device *dev,
					     const char *propname)
{
	return device_property_present(dev, propname);
}

static inline int device_property_read_u8(struct device *dev,
					  const char *propname, u8 *val)
{
	return device_property_read_u8_array(dev, propname, val, 1);
}

static inline int device_property_read_u16(struct device *dev,
					   const char *propname, u16 *val)
{
	return device_property_read_u16_array(dev, propname, val, 1);
}

static inline int device_property_read_u32(struct device *dev,
					   const char *propname, u32 *val)
{
	return device_property_read_u32_array(dev, propname, val, 1);
}

static inline int device_property_read_u64(struct device *dev,
					   const char *propname, u64 *val)
{
	return device_property_read_u64_array(dev, propname, val, 1);
}

#endif /* _LINUX_PROPERTY_H_ */
