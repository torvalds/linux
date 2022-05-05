/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ACPI_AGDI_H__
#define __ACPI_AGDI_H__

#include <linux/acpi.h>

#ifdef CONFIG_ACPI_AGDI
void __init acpi_agdi_init(void);
#else
static inline void acpi_agdi_init(void) {}
#endif
#endif /* __ACPI_AGDI_H__ */
