/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024 Marvell.
 */

#ifndef __SOC_SILICON_H
#define __SOC_SILICON_H

#include <linux/types.h>
#include <linux/pci.h>

#if defined(CONFIG_ARM64)

#define CN20K_CHIPID	0x20
/*
 * Silicon check for CN20K family
 */
static inline bool is_cn20k(struct pci_dev *pdev)
{
	return (pdev->subsystem_device & 0xFF) == CN20K_CHIPID;
}
#else
#define is_cn20k(pdev)		((void)(pdev), 0)
#endif

#endif /* __SOC_SILICON_H */
