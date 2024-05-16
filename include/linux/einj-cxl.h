/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CXL protocol Error INJection support.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Ben Cheatham <benjamin.cheatham@amd.com>
 */
#ifndef EINJ_CXL_H
#define EINJ_CXL_H

#include <linux/errno.h>
#include <linux/types.h>

struct pci_dev;
struct seq_file;

#if IS_ENABLED(CONFIG_ACPI_APEI_EINJ_CXL)
int einj_cxl_available_error_type_show(struct seq_file *m, void *v);
int einj_cxl_inject_error(struct pci_dev *dport_dev, u64 type);
int einj_cxl_inject_rch_error(u64 rcrb, u64 type);
bool einj_cxl_is_initialized(void);
#else /* !IS_ENABLED(CONFIG_ACPI_APEI_EINJ_CXL) */
static inline int einj_cxl_available_error_type_show(struct seq_file *m,
						     void *v)
{
	return -ENXIO;
}

static inline int einj_cxl_inject_error(struct pci_dev *dport_dev, u64 type)
{
	return -ENXIO;
}

static inline int einj_cxl_inject_rch_error(u64 rcrb, u64 type)
{
	return -ENXIO;
}

static inline bool einj_cxl_is_initialized(void) { return false; }
#endif /* CONFIG_ACPI_APEI_EINJ_CXL */

#endif /* EINJ_CXL_H */
