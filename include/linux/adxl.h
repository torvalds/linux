/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Address translation interface via ACPI DSM.
 * Copyright (C) 2018 Intel Corporation
 */

#ifndef _LINUX_ADXL_H
#define _LINUX_ADXL_H

#ifdef CONFIG_ACPI_ADXL
const char * const *adxl_get_component_names(void);
int adxl_decode(u64 addr, u64 component_values[]);
#else
static inline const char * const *adxl_get_component_names(void)  { return NULL; }
static inline int adxl_decode(u64 addr, u64 component_values[])   { return  -EOPNOTSUPP; }
#endif

#endif /* _LINUX_ADXL_H */
