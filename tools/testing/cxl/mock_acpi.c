// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */

#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <cxl.h>
#include "test/mock.h"

struct acpi_device *to_cxl_host_bridge(struct device *host, struct device *dev)
{
	int index;
	struct acpi_device *adev, *found = NULL;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);

	if (ops && ops->is_mock_bridge(dev)) {
		found = ACPI_COMPANION(dev);
		goto out;
	}

	if (dev->bus == &platform_bus_type)
		goto out;

	adev = to_acpi_device(dev);
	if (!acpi_pci_find_root(adev->handle))
		goto out;

	if (strcmp(acpi_device_hid(adev), "ACPI0016") == 0) {
		found = adev;
		dev_dbg(host, "found host bridge %s\n", dev_name(&adev->dev));
	}
out:
	put_cxl_mock_ops(index);
	return found;
}

static int match_add_root_port(struct pci_dev *pdev, void *data)
{
	struct cxl_walk_context *ctx = data;
	struct pci_bus *root_bus = ctx->root;
	struct cxl_port *port = ctx->port;
	int type = pci_pcie_type(pdev);
	struct device *dev = ctx->dev;
	u32 lnkcap, port_num;
	int rc;

	if (pdev->bus != root_bus)
		return 0;
	if (!pci_is_pcie(pdev))
		return 0;
	if (type != PCI_EXP_TYPE_ROOT_PORT)
		return 0;
	if (pci_read_config_dword(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCAP,
				  &lnkcap) != PCIBIOS_SUCCESSFUL)
		return 0;

	/* TODO walk DVSEC to find component register base */
	port_num = FIELD_GET(PCI_EXP_LNKCAP_PN, lnkcap);
	cxl_device_lock(&port->dev);
	rc = cxl_add_dport(port, &pdev->dev, port_num, CXL_RESOURCE_NONE);
	cxl_device_unlock(&port->dev);
	if (rc) {
		dev_err(dev, "failed to add dport: %s (%d)\n",
			dev_name(&pdev->dev), rc);
		ctx->error = rc;
		return rc;
	}
	ctx->count++;

	dev_dbg(dev, "add dport%d: %s\n", port_num, dev_name(&pdev->dev));

	return 0;
}

static int mock_add_root_port(struct platform_device *pdev, void *data)
{
	struct cxl_walk_context *ctx = data;
	struct cxl_port *port = ctx->port;
	struct device *dev = ctx->dev;
	int rc;

	cxl_device_lock(&port->dev);
	rc = cxl_add_dport(port, &pdev->dev, pdev->id, CXL_RESOURCE_NONE);
	cxl_device_unlock(&port->dev);
	if (rc) {
		dev_err(dev, "failed to add dport: %s (%d)\n",
			dev_name(&pdev->dev), rc);
		ctx->error = rc;
		return rc;
	}
	ctx->count++;

	dev_dbg(dev, "add dport%d: %s\n", pdev->id, dev_name(&pdev->dev));

	return 0;
}

int match_add_root_ports(struct pci_dev *dev, void *data)
{
	int index, rc;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);
	struct platform_device *pdev = (struct platform_device *) dev;

	if (ops && ops->is_mock_port(pdev))
		rc = mock_add_root_port(pdev, data);
	else
		rc = match_add_root_port(dev, data);

	put_cxl_mock_ops(index);

	return rc;
}
