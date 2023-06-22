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
EXPORT_SYMBOL_NS_GPL(__wrap_acpi_table_parse_cedt, ACPI);

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

struct cxl_hdm *__wrap_devm_cxl_setup_hdm(struct cxl_port *port,
					  struct cxl_endpoint_dvsec_info *info)

{
	int index;
	struct cxl_hdm *cxlhdm;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_port(port->uport))
		cxlhdm = ops->devm_cxl_setup_hdm(port, info);
	else
		cxlhdm = devm_cxl_setup_hdm(port, info);
	put_cxl_mock_ops(index);

	return cxlhdm;
}
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_setup_hdm, CXL);

int __wrap_devm_cxl_enable_hdm(struct cxl_port *port, struct cxl_hdm *cxlhdm)
{
	int index, rc;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_port(port->uport))
		rc = 0;
	else
		rc = devm_cxl_enable_hdm(port, cxlhdm);
	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_enable_hdm, CXL);

int __wrap_devm_cxl_add_passthrough_decoder(struct cxl_port *port)
{
	int rc, index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_port(port->uport))
		rc = ops->devm_cxl_add_passthrough_decoder(port);
	else
		rc = devm_cxl_add_passthrough_decoder(port);
	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_add_passthrough_decoder, CXL);

int __wrap_devm_cxl_enumerate_decoders(struct cxl_hdm *cxlhdm,
				       struct cxl_endpoint_dvsec_info *info)
{
	int rc, index;
	struct cxl_port *port = cxlhdm->port;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_port(port->uport))
		rc = ops->devm_cxl_enumerate_decoders(cxlhdm, info);
	else
		rc = devm_cxl_enumerate_decoders(cxlhdm, info);
	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_enumerate_decoders, CXL);

int __wrap_devm_cxl_port_enumerate_dports(struct cxl_port *port)
{
	int rc, index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_port(port->uport))
		rc = ops->devm_cxl_port_enumerate_dports(port);
	else
		rc = devm_cxl_port_enumerate_dports(port);
	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_port_enumerate_dports, CXL);

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
EXPORT_SYMBOL_NS_GPL(__wrap_cxl_await_media_ready, CXL);

int __wrap_cxl_hdm_decode_init(struct cxl_dev_state *cxlds,
			       struct cxl_hdm *cxlhdm,
			       struct cxl_endpoint_dvsec_info *info)
{
	int rc = 0, index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_dev(cxlds->dev))
		rc = 0;
	else
		rc = cxl_hdm_decode_init(cxlds, cxlhdm, info);
	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_cxl_hdm_decode_init, CXL);

int __wrap_cxl_dvsec_rr_decode(struct device *dev, int dvsec,
			       struct cxl_endpoint_dvsec_info *info)
{
	int rc = 0, index;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_dev(dev))
		rc = 0;
	else
		rc = cxl_dvsec_rr_decode(dev, dvsec, info);
	put_cxl_mock_ops(index);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(__wrap_cxl_dvsec_rr_decode, CXL);

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
EXPORT_SYMBOL_NS_GPL(__wrap_devm_cxl_add_rch_dport, CXL);

resource_size_t __wrap_cxl_rcd_component_reg_phys(struct device *dev,
						  struct cxl_dport *dport)
{
	int index;
	resource_size_t component_reg_phys;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_port(dev))
		component_reg_phys = CXL_RESOURCE_NONE;
	else
		component_reg_phys = cxl_rcd_component_reg_phys(dev, dport);
	put_cxl_mock_ops(index);

	return component_reg_phys;
}
EXPORT_SYMBOL_NS_GPL(__wrap_cxl_rcd_component_reg_phys, CXL);

MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(ACPI);
MODULE_IMPORT_NS(CXL);
