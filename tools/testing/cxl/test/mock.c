// SPDX-License-Identifier: GPL-2.0-only
//Copyright(c) 2021 Intel Corporation. All rights reserved.

#include <linux/libnvdimm.h>
#include <linux/rculist.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <cxlmem.h>
#include <cxlpci.h>
#include "mock.h"

static LIST_HEAD(mock);

void register_cxl_mock_ops(struct cxl_mock_ops *ops)
{
	list_add_rcu(&ops->list, &mock);
}
EXPORT_SYMBOL_GPL(register_cxl_mock_ops);

DEFINE_STATIC_SRCU(cxl_mock_srcu);

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

int __wrap_acpi_table_parse_cedt(enum acpi_cedt_type id,
				 acpi_tbl_entry_handler_arg handler_arg,
				 void *arg)
{
	int index, rc;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops)
		rc = ops->acpi_table_parse_cedt(id, handler_arg, arg);
	else
		rc = acpi_table_parse_cedt(id, handler_arg, arg);

	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_acpi_table_parse_cedt, "ACPI");

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

int __wrap_hmat_get_extended_linear_cache_size(struct resource *backing_res,
					       int nid,
					       resource_size_t *cache_size)
{
	int index, rc;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops)
		rc = ops->hmat_get_extended_linear_cache_size(backing_res, nid,
							      cache_size);
	else
		rc = hmat_get_extended_linear_cache_size(backing_res, nid,
							 cache_size);

	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_GPL(__wrap_hmat_get_extended_linear_cache_size);

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

int __wrap_devm_cxl_switch_port_decoders_setup(struct cxl_port *port)
{
	int rc, index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_port(port->uport_dev))
		rc = ops->devm_cxl_switch_port_decoders_setup(port);
	else
		rc = devm_cxl_switch_port_decoders_setup(port);
	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_switch_port_decoders_setup, "CXL");

int __wrap_devm_cxl_endpoint_decoders_setup(struct cxl_port *port)
{
	int rc, index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_port(port->uport_dev))
		rc = ops->devm_cxl_endpoint_decoders_setup(port);
	else
		rc = devm_cxl_endpoint_decoders_setup(port);
	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_endpoint_decoders_setup, "CXL");

int __wrap_cxl_await_media_ready(struct cxl_dev_state *cxlds)
{
	int rc, index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_dev(cxlds->dev))
		rc = 0;
	else
		rc = cxl_await_media_ready(cxlds);
	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_cxl_await_media_ready, "CXL");

struct cxl_dport *__wrap_devm_cxl_add_rch_dport(struct cxl_port *port,
						struct device *dport_dev,
						int port_id,
						resource_size_t rcrb)
{
	int index;
	struct cxl_dport *dport;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_port(dport_dev)) {
		dport = devm_cxl_add_dport(port, dport_dev, port_id,
					   CXL_RESOURCE_NONE);
		if (!IS_ERR(dport)) {
			dport->rcrb.base = rcrb;
			dport->rch = true;
		}
	} else
		dport = devm_cxl_add_rch_dport(port, dport_dev, port_id, rcrb);
	put_cxl_mock_ops(index);

	return dport;
}
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_add_rch_dport, "CXL");

void __wrap_cxl_endpoint_parse_cdat(struct cxl_port *port)
{
	int index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);
	struct cxl_memdev *cxlmd = to_cxl_memdev(port->uport_dev);

	if (ops && ops->is_mock_dev(cxlmd->dev.parent))
		ops->cxl_endpoint_parse_cdat(port);
	else
		cxl_endpoint_parse_cdat(port);
	put_cxl_mock_ops(index);
}
EXPORT_SYMBOL_NS_GPL(__wrap_cxl_endpoint_parse_cdat, "CXL");

struct cxl_dport *__wrap_devm_cxl_add_dport_by_dev(struct cxl_port *port,
						   struct device *dport_dev)
{
	int index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);
	struct cxl_dport *dport;

	if (ops && ops->is_mock_port(port->uport_dev))
		dport = ops->devm_cxl_add_dport_by_dev(port, dport_dev);
	else
		dport = devm_cxl_add_dport_by_dev(port, dport_dev);
	put_cxl_mock_ops(index);

	return dport;
}
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_add_dport_by_dev, "CXL");

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("cxl_test: emulation module");
MODULE_IMPORT_NS("ACPI");
MODULE_IMPORT_NS("CXL");
