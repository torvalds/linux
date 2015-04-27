/*
 * Copyright (C) 2014, Linaro Ltd.
 *	Author: Tomasz Nowicki <tomasz.nowicki@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARM_GIC_ACPI_H_
#define ARM_GIC_ACPI_H_

#ifdef CONFIG_ACPI

/*
 * Hard code here, we can not get memory size from MADT (but FDT does),
 * Actually no need to do that, because this size can be inferred
 * from GIC spec.
 */
#define ACPI_GICV2_DIST_MEM_SIZE	(SZ_4K)
#define ACPI_GIC_CPU_IF_MEM_SIZE	(SZ_8K)

struct acpi_table_header;

int gic_v2_acpi_init(struct acpi_table_header *table);
void acpi_gic_init(void);
#else
static inline void acpi_gic_init(void) { }
#endif

#endif /* ARM_GIC_ACPI_H_ */
