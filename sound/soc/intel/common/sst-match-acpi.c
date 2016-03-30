/*
 * sst_match_apci.c - SST (LPE) match for ACPI enumeration.
 *
 * Copyright (c) 2013-15, Intel Corporation.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "sst-acpi.h"

static acpi_status sst_acpi_find_name(acpi_handle handle, u32 level,
				      void *context, void **ret)
{
	struct acpi_device *adev;
	const char *name = NULL;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;

	if (adev->status.present && adev->status.functional) {
		name = acpi_dev_name(adev);
		*(const char **)ret = name;
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

const char *sst_acpi_find_name_from_hid(const u8 hid[ACPI_ID_LEN])
{
	const char *name = NULL;
	acpi_status status;

	status = acpi_get_devices(hid, sst_acpi_find_name, NULL,
				  (void **)&name);

	if (ACPI_FAILURE(status) || name[0] == '\0')
		return NULL;

	return name;
}
EXPORT_SYMBOL_GPL(sst_acpi_find_name_from_hid);

static acpi_status sst_acpi_mach_match(acpi_handle handle, u32 level,
				       void *context, void **ret)
{
	unsigned long long sta;
	acpi_status status;

	*(bool *)context = true;
	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status) || !(sta & ACPI_STA_DEVICE_PRESENT))
		*(bool *)context = false;

	return AE_OK;
}

struct sst_acpi_mach *sst_acpi_find_machine(struct sst_acpi_mach *machines)
{
	struct sst_acpi_mach *mach;
	bool found = false;

	for (mach = machines; mach->id[0]; mach++)
		if (ACPI_SUCCESS(acpi_get_devices(mach->id,
						  sst_acpi_mach_match,
						  &found, NULL)) && found)
			return mach;
	return NULL;
}
EXPORT_SYMBOL_GPL(sst_acpi_find_machine);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
