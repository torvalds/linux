/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */

#ifndef __CXL_CXL_PCI_H__
#define __CXL_CXL_PCI_H__

/* Register Block Identifier (RBI) */
enum cxl_regloc_type {
	CXL_REGLOC_RBI_EMPTY = 0,
	CXL_REGLOC_RBI_COMPONENT,
	CXL_REGLOC_RBI_VIRT,
	CXL_REGLOC_RBI_MEMDEV,
	CXL_REGLOC_RBI_PMU,
	CXL_REGLOC_RBI_TYPES
};

struct cxl_register_map;
struct pci_dev;

int cxl_pci_setup_regs(struct pci_dev *pdev, enum cxl_regloc_type type,
		       struct cxl_register_map *map);
#endif
