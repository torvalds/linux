/*
 * acpi_lpat.h - LPAT table processing functions
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef ACPI_LPAT_H
#define ACPI_LPAT_H

struct acpi_lpat {
	int temp;
	int raw;
};

struct acpi_lpat_conversion_table {
	struct acpi_lpat *lpat;
	int lpat_count;
};

#ifdef CONFIG_ACPI

int acpi_lpat_raw_to_temp(struct acpi_lpat_conversion_table *lpat_table,
			  int raw);
int acpi_lpat_temp_to_raw(struct acpi_lpat_conversion_table *lpat_table,
			  int temp);
struct acpi_lpat_conversion_table *acpi_lpat_get_conversion_table(acpi_handle
								  handle);
void acpi_lpat_free_conversion_table(struct acpi_lpat_conversion_table
				     *lpat_table);

#else
static int acpi_lpat_raw_to_temp(struct acpi_lpat_conversion_table *lpat_table,
				 int raw)
{
	return 0;
}

static int acpi_lpat_temp_to_raw(struct acpi_lpat_conversion_table *lpat_table,
				 int temp)
{
	return 0;
}

static struct acpi_lpat_conversion_table *acpi_lpat_get_conversion_table(
							acpi_handle handle)
{
	return NULL;
}

static void acpi_lpat_free_conversion_table(struct acpi_lpat_conversion_table
					    *lpat_table)
{
}

#endif
#endif
