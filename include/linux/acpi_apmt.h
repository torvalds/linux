/* SPDX-License-Identifier: GPL-2.0
 *
 * ARM CoreSight PMU driver.
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES.
 *
 */

#ifndef __ACPI_APMT_H__
#define __ACPI_APMT_H__

#include <linux/acpi.h>

#ifdef CONFIG_ACPI_APMT
void acpi_apmt_init(void);
#else
static inline void acpi_apmt_init(void) { }
#endif /* CONFIG_ACPI_APMT */

#endif /* __ACPI_APMT_H__ */
