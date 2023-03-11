/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data Object Exchange
 *	PCIe r6.0, sec 6.30 DOE
 *
 * Copyright (C) 2021 Huawei
 *     Jonathan Cameron <Jonathan.Cameron@huawei.com>
 *
 * Copyright (C) 2022 Intel Corporation
 *	Ira Weiny <ira.weiny@intel.com>
 */

#ifndef LINUX_PCI_DOE_H
#define LINUX_PCI_DOE_H

struct pci_doe_mb;

/**
 * pci_doe_for_each_off - Iterate each DOE capability
 * @pdev: struct pci_dev to iterate
 * @off: u16 of config space offset of each mailbox capability found
 */
#define pci_doe_for_each_off(pdev, off) \
	for (off = pci_find_next_ext_capability(pdev, off, \
					PCI_EXT_CAP_ID_DOE); \
		off > 0; \
		off = pci_find_next_ext_capability(pdev, off, \
					PCI_EXT_CAP_ID_DOE))

struct pci_doe_mb *pcim_doe_create_mb(struct pci_dev *pdev, u16 cap_offset);
bool pci_doe_supports_prot(struct pci_doe_mb *doe_mb, u16 vid, u8 type);
struct pci_doe_mb *pci_find_doe_mailbox(struct pci_dev *pdev, u16 vendor,
					u8 type);

int pci_doe(struct pci_doe_mb *doe_mb, u16 vendor, u8 type,
	    const void *request, size_t request_sz,
	    void *response, size_t response_sz);

#endif
