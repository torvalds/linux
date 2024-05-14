/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (c) 2021 Mellanox Technologies Ltd.  All rights reserved.
 */
#ifndef DEF_RDMA_IB_SYSFS_H
#define DEF_RDMA_IB_SYSFS_H

#include <linux/sysfs.h>

struct ib_device;

struct ib_port_attribute {
	struct attribute attr;
	ssize_t (*show)(struct ib_device *ibdev, u32 port_num,
			struct ib_port_attribute *attr, char *buf);
	ssize_t (*store)(struct ib_device *ibdev, u32 port_num,
			 struct ib_port_attribute *attr, const char *buf,
			 size_t count);
};

#define IB_PORT_ATTR_RW(_name)                                                 \
	struct ib_port_attribute ib_port_attr_##_name = __ATTR_RW(_name)

#define IB_PORT_ATTR_ADMIN_RW(_name)                                           \
	struct ib_port_attribute ib_port_attr_##_name =                        \
		__ATTR_RW_MODE(_name, 0600)

#define IB_PORT_ATTR_RO(_name)                                                 \
	struct ib_port_attribute ib_port_attr_##_name = __ATTR_RO(_name)

#define IB_PORT_ATTR_WO(_name)                                                 \
	struct ib_port_attribute ib_port_attr_##_name = __ATTR_WO(_name)

struct ib_device *ib_port_sysfs_get_ibdev_kobj(struct kobject *kobj,
					       u32 *port_num);

#endif
