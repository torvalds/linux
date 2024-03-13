/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2018 Intel Corporation
 */

#ifndef __ACPI_NFIT_H
#define __ACPI_NFIT_H

#if IS_ENABLED(CONFIG_ACPI_NFIT)
int nfit_get_smbios_id(u32 device_handle, u16 *flags);
#else
static inline int nfit_get_smbios_id(u32 device_handle, u16 *flags)
{
	return -EOPNOTSUPP;
}
#endif

#endif /* __ACPI_NFIT_H */
