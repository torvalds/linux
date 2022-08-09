// SPDX-License-Identifier: GPL-2.0-only
//Copyright(c) 2021 Intel Corporation. All rights reserved.

#include <linux/libnvdimm.h>
#include <linux/rculist.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include "mock.h"

static LIST_HEAD(mock);

void register_cxl_mock_ops(struct cxl_mock_ops *ops)
{
	list_add_rcu(&ops->list, &mock);
}
EXPORT_SYMBOL_GPL(register_cxl_mock_ops);

static DEFINE_SRCU(cxl_mock_srcu);

void unregister_cxl_mock_ops(struct cxl_mock_ops *ops)
{
	list_del_rcu(&ops->list);
	synchronize_srcu(&cxl_mock_srcu);
}
EXPORT_SYMBOL_GPL(unregister_cxl_mock_ops);

struct cxl_mock_ops *get_cxl_mock_ops(int *index)
{
	*index = srcu_read_lock(&cxl_mock_srcu);
	return list_first_or_null_rcu(&mock, struct cxl_mock_ops, list);
}
EXPORT_SYMBOL_GPL(get_cxl_mock_ops);

void put_cxl_mock_ops(int index)
{
	srcu_read_unlock(&cxl_mock_srcu, index);
}
EXPORT_SYMBOL_GPL(put_cxl_mock_ops);

bool __wrap_is_acpi_device_node(const struct fwnode_handle *fwnode)
{
	struct acpi_device *adev =
		container_of(fwnode, struct acpi_device, fwnode);
	int index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);
	bool retval = false;

	if (ops)
		retval = ops->is_mock_adev(adev);

	if (!retval)
		retval = is_acpi_device_node(fwnode);

	put_cxl_mock_ops(index);
	return retval;
}
EXPORT_SYMBOL(__wrap_is_acpi_device_node);

acpi_status __wrap_acpi_get_table(char *signature, u32 instance,
				  struct acpi_table_header **out_table)
{
	int index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);
	acpi_status status;

	if (ops)
		status = ops->acpi_get_table(signature, instance, out_table);
	else
		status = acpi_get_table(signature, instance, out_table);

	put_cxl_mock_ops(index);

	return status;
}
EXPORT_SYMBOL(__wrap_acpi_get_table);

void __wrap_acpi_put_table(struct acpi_table_header *table)
{
	int index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops)
		ops->acpi_put_table(table);
	else
		acpi_put_table(table);
	put_cxl_mock_ops(index);
}
EXPORT_SYMBOL(__wrap_acpi_put_table);

acpi_status __wrap_acpi_evaluate_integer(acpi_handle handle,
					 acpi_string pathname,
					 struct acpi_object_list *arguments,
					 unsigned long long *data)
{
	int index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);
	acpi_status status;

	if (ops)
		status = ops->acpi_evaluate_integer(handle, pathname, arguments,
						    data);
	else
		status = acpi_evaluate_integer(handle, pathname, arguments,
					       data);
	put_cxl_mock_ops(index);

	return status;
}
EXPORT_SYMBOL(__wrap_acpi_evaluate_integer);

struct acpi_pci_root *__wrap_acpi_pci_find_root(acpi_handle handle)
{
	int index;
	struct acpi_pci_root *root;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops)
		root = ops->acpi_pci_find_root(handle);
	else
		root = acpi_pci_find_root(handle);

	put_cxl_mock_ops(index);

	return root;
}
EXPORT_SYMBOL_GPL(__wrap_acpi_pci_find_root);

void __wrap_pci_walk_bus(struct pci_bus *bus,
			 int (*cb)(struct pci_dev *, void *), void *userdata)
{
	int index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_bus(bus)) {
		int rc, i;

		/*
		 * Simulate 2 root ports per host-bridge and no
		 * depth recursion.
		 */
		for (i = 0; i < 2; i++) {
			rc = cb((struct pci_dev *) ops->mock_port(bus, i),
				userdata);
			if (rc)
				break;
		}
	} else
		pci_walk_bus(bus, cb, userdata);

	put_cxl_mock_ops(index);
}
EXPORT_SYMBOL_GPL(__wrap_pci_walk_bus);

struct nvdimm_bus *
__wrap_nvdimm_bus_register(struct device *dev,
			   struct nvdimm_bus_descriptor *nd_desc)
{
	int index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_dev(dev->parent->parent))
		nd_desc->provider_name = "cxl_test";
	put_cxl_mock_ops(index);

	return nvdimm_bus_register(dev, nd_desc);
}
EXPORT_SYMBOL_GPL(__wrap_nvdimm_bus_register);

MODULE_LICENSE("GPL v2");
