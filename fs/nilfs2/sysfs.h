/*
 * sysfs.h - sysfs support declarations.
 *
 * Copyright (C) 2005-2014 Nippon Telegraph and Telephone Corporation.
 * Copyright (C) 2014 HGST, Inc., a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by Vyacheslav Dubeyko <Vyacheslav.Dubeyko@hgst.com>
 */

#ifndef _NILFS_SYSFS_H
#define _NILFS_SYSFS_H

#include <linux/sysfs.h>

#define NILFS_ROOT_GROUP_NAME	"nilfs2"

#define NILFS_COMMON_ATTR_STRUCT(name) \
struct nilfs_##name##_attr { \
	struct attribute attr; \
	ssize_t (*show)(struct kobject *, struct attribute *, \
			char *); \
	ssize_t (*store)(struct kobject *, struct attribute *, \
			 const char *, size_t); \
};

NILFS_COMMON_ATTR_STRUCT(feature);

#define NILFS_ATTR(type, name, mode, show, store) \
	static struct nilfs_##type##_attr nilfs_##type##_attr_##name = \
		__ATTR(name, mode, show, store)

#define NILFS_INFO_ATTR(type, name) \
	NILFS_ATTR(type, name, 0444, NULL, NULL)
#define NILFS_RO_ATTR(type, name) \
	NILFS_ATTR(type, name, 0444, nilfs_##type##_##name##_show, NULL)
#define NILFS_RW_ATTR(type, name) \
	NILFS_ATTR(type, name, 0644, \
		    nilfs_##type##_##name##_show, \
		    nilfs_##type##_##name##_store)

#define NILFS_FEATURE_INFO_ATTR(name) \
	NILFS_INFO_ATTR(feature, name)
#define NILFS_FEATURE_RO_ATTR(name) \
	NILFS_RO_ATTR(feature, name)
#define NILFS_FEATURE_RW_ATTR(name) \
	NILFS_RW_ATTR(feature, name)

#define NILFS_FEATURE_ATTR_LIST(name) \
	(&nilfs_feature_attr_##name.attr)

#endif /* _NILFS_SYSFS_H */
