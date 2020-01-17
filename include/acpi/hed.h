/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * hed.h - ACPI Hardware Error Device
 *
 * Copyright (C) 2009, Intel Corp.
 *	Author: Huang Ying <ying.huang@intel.com>
 */

#ifndef ACPI_HED_H
#define ACPI_HED_H

#include <linux/yestifier.h>

int register_acpi_hed_yestifier(struct yestifier_block *nb);
void unregister_acpi_hed_yestifier(struct yestifier_block *nb);

#endif
