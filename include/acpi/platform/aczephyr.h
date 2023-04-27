/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Module Name: aczephyr.h - OS specific defines, etc.
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACZEPHYR_H__
#define __ACZEPHYR_H__

#define ACPI_MACHINE_WIDTH      64

#define ACPI_NO_ERROR_MESSAGES
#undef ACPI_DEBUG_OUTPUT
#define ACPI_USE_SYSTEM_CLIBRARY
#undef ACPI_DBG_TRACK_ALLOCATIONS
#define ACPI_SINGLE_THREADED
#define ACPI_USE_NATIVE_RSDP_POINTER

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/__assert.h>

/******************************************************************************
 *
 * FUNCTION:    acpi_enable_dbg_print
 *
 * PARAMETERS:  Enable, 	            - Enable/Disable debug print
 *
 * RETURN:      None
 *
 * DESCRIPTION: Enable/disable debug print
 *
 *****************************************************************************/

void acpi_enable_dbg_print(bool enable);
#endif
