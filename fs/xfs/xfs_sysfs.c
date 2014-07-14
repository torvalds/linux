/*
 * Copyright (c) 2014 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "xfs.h"
#include "xfs_sysfs.h"

struct xfs_sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(char *buf, void *data);
	ssize_t (*store)(const char *buf, size_t count, void *data);
};

static inline struct xfs_sysfs_attr *
to_attr(struct attribute *attr)
{
	return container_of(attr, struct xfs_sysfs_attr, attr);
}

#define XFS_SYSFS_ATTR_RW(name) \
	static struct xfs_sysfs_attr xfs_sysfs_attr_##name = __ATTR_RW(name)
#define XFS_SYSFS_ATTR_RO(name) \
	static struct xfs_sysfs_attr xfs_sysfs_attr_##name = __ATTR_RO(name)

#define ATTR_LIST(name) &xfs_sysfs_attr_##name.attr

/*
 * xfs_mount kobject. This currently has no attributes and thus no need for show
 * and store helpers. The mp kobject serves as the per-mount parent object that
 * is identified by the fsname under sysfs.
 */

struct kobj_type xfs_mp_ktype = {
	.release = xfs_sysfs_release,
};
