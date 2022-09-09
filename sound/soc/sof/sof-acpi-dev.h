/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 */

#ifndef __SOUND_SOC_SOF_ACPI_H
#define __SOUND_SOC_SOF_ACPI_H

extern const struct dev_pm_ops sof_acpi_pm;
int sof_acpi_probe(struct platform_device *pdev, const struct sof_dev_desc *desc);
int sof_acpi_remove(struct platform_device *pdev);

#endif
