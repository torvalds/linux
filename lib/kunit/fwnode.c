// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Qualcomm Technologies, Inc. and/or its subsidiaries
 */

#include <kunit/fwnode.h>
#include <kunit/test.h>

#include <linux/fwnode.h>
#include <linux/property.h>

KUNIT_DEFINE_ACTION_WRAPPER(fwnode_remove_software_node_wrapper,
			    fwnode_remove_software_node,
			    struct fwnode_handle *);

/**
 * kunit_fwnode_create_software_node() - Create a kunit-managed software node
 * @test: Test context
 * @properties: Properties to use to create the new software node
 * @parent: Parent of this software node
 *
 * Create a test-managed software node and return its firmware node handle.
 * The software node is removed after the test case completes.
 *
 * Returns:
 * Firmware node handle of the newly created software node or IS_ERR() on
 * failure.
 */
struct fwnode_handle *
kunit_fwnode_create_software_node(struct kunit *test,
				  const struct property_entry *properties,
				  const struct fwnode_handle *parent)
{
	struct fwnode_handle *fwnode;
	int ret;

	fwnode = fwnode_create_software_node(properties, parent);
	if (IS_ERR(fwnode))
		return fwnode;

	ret = kunit_add_action_or_reset(test, fwnode_remove_software_node_wrapper,
					fwnode);
	if (ret)
		return ERR_PTR(ret);

	return fwnode;
}
EXPORT_SYMBOL_GPL(kunit_fwnode_create_software_node);

KUNIT_DEFINE_ACTION_WRAPPER(software_node_unregister_wrapper,
			    software_node_unregister,
			    const struct software_node *);

/**
 * kunit_software_node_register() - Register a kunit-managed software node
 * @test: Test context
 * @swnode: Software node to register
 *
 * Register a test-managed software node and return its firmware node handle.
 * The software node is unregistered after the test case completes.
 *
 * Returns:
 * Firmware node handle of the registered software node or IS_ERR() on failure.
 */
struct fwnode_handle *
kunit_software_node_register(struct kunit *test,
			     const struct software_node *swnode)
{
	struct fwnode_handle *fwnode;
	int ret;

	ret = software_node_register(swnode);
	if (ret)
		return ERR_PTR(ret);

	fwnode = software_node_fwnode(swnode);
	if (WARN_ON(!fwnode))
		return ERR_PTR(-ENOENT);

	ret = kunit_add_action_or_reset(test, software_node_unregister_wrapper,
					(void *)swnode);
	if (ret)
		return ERR_PTR(ret);

	return fwnode;
}
EXPORT_SYMBOL_GPL(kunit_software_node_register);

KUNIT_DEFINE_ACTION_WRAPPER(software_node_unregister_node_group_wrapper,
			    software_node_unregister_node_group,
			    const struct software_node *const *);

/**
 * kunit_software_node_register_node_group() - Register a kunit-managed software node group
 * @test: Test context
 * @nodes: Software node group to register
 *
 * Register a test-managed software node group. The nodes are unregistered
 * after the test case completes.
 *
 * Returns:
 * 0 on success, negative error number on failure.
 */
int kunit_software_node_register_node_group(struct kunit *test,
					    const struct software_node *const *nodes)
{
	int ret;

	ret = software_node_register_node_group(nodes);
	if (ret)
		return ret;

	return kunit_add_action_or_reset(test, software_node_unregister_node_group_wrapper,
					 (void *)nodes);
}
EXPORT_SYMBOL_GPL(kunit_software_node_register_node_group);

KUNIT_DEFINE_ACTION_WRAPPER(device_remove_software_node_wrapper,
			    device_remove_software_node,
			    struct device *);

/**
 * kunit_device_add_software_node() - Assign a kunit-managed software node to a device
 * @test: Test context
 * @dev: Device to assign the software node for
 * @node: The software node to assign
 *
 * Make @node the secondary firmware node of @dev. If @dev has no primary
 * firmware node, @node will become the primary node. The software node will
 * be automatically removed from @dev when the test case completes.
 *
 * Returns:
 * 0 on success, negative error number on failure.
 */
int kunit_device_add_software_node(struct kunit *test, struct device *dev,
				   const struct software_node *node)
{
	int ret;

	ret = device_add_software_node(dev, node);
	if (ret)
		return ret;

	return kunit_add_action_or_reset(test, device_remove_software_node_wrapper, dev);
}
EXPORT_SYMBOL_GPL(kunit_device_add_software_node);
