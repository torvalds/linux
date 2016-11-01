/*
 * Copyright (C) 2013-15, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kconfig.h>
#include <linux/stddef.h>
#include <linux/acpi.h>

/* translation fron HID to I2C name, needed for DAI codec_name */
#if IS_ENABLED(CONFIG_ACPI)
const char *sst_acpi_find_name_from_hid(const u8 hid[ACPI_ID_LEN]);
#else
static inline const char *sst_acpi_find_name_from_hid(const u8 hid[ACPI_ID_LEN])
{
	return NULL;
}
#endif

/* acpi match */
struct sst_acpi_mach *sst_acpi_find_machine(struct sst_acpi_mach *machines);

/* Descriptor for SST ASoC machine driver */
struct sst_acpi_mach {
	/* ACPI ID for the matching machine driver. Audio codec for instance */
	const u8 id[ACPI_ID_LEN];
	/* machine driver name */
	const char *drv_name;
	/* firmware file name */
	const char *fw_filename;

	/* board name */
	const char *board;
	struct sst_acpi_mach * (*machine_quirk)(void *arg);
	void *pdata;
};
