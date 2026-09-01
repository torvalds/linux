/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit resource management helpers for firmware nodes.
 *
 * Copyright (C) Qualcomm Technologies, Inc. and/or its subsidiaries
 */

#ifndef _KUNIT_FWNODE_H
#define _KUNIT_FWNODE_H

struct device;
struct fwnode_handle;
struct kunit;
struct property_entry;
struct software_node;

struct fwnode_handle *
kunit_fwnode_create_software_node(struct kunit *test,
				  const struct property_entry *properties,
				  const struct fwnode_handle *parent);
struct fwnode_handle *
kunit_software_node_register(struct kunit *test,
			     const struct software_node *node);
int kunit_software_node_register_node_group(struct kunit *test,
					    const struct software_node *const *nodes);
int kunit_device_add_software_node(struct kunit *test, struct device *dev,
				   const struct software_node *node);

#endif /* _KUNIT_FWNODE_H */
