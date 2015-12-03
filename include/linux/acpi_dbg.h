/*
 * ACPI AML interfacing support
 *
 * Copyright (C) 2015, Intel Corporation
 * Authors: Lv Zheng <lv.zheng@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_ACPI_DBG_H
#define _LINUX_ACPI_DBG_H

#include <linux/acpi.h>

#ifdef CONFIG_ACPI_DEBUGGER
int __init acpi_aml_init(void);
int acpi_aml_create_thread(acpi_osd_exec_callback function, void *context);
ssize_t acpi_aml_write_log(const char *msg);
ssize_t acpi_aml_read_cmd(char *buffer, size_t buffer_length);
int acpi_aml_wait_command_ready(void);
int acpi_aml_notify_command_complete(void);
#else
static int inline acpi_aml_init(void)
{
	return 0;
}
static inline int acpi_aml_create_thread(acpi_osd_exec_callback function,
					 void *context)
{
	return -ENODEV;
}
static inline int acpi_aml_write_log(const char *msg)
{
	return -ENODEV;
}
static inline int acpi_aml_read_cmd(char *buffer, u32 buffer_length)
{
	return -ENODEV;
}
static inline int acpi_aml_wait_command_ready(void)
{
	return -ENODEV;
}
static inline int acpi_aml_notify_command_complete(void)
{
	return -ENODEV;
}
#endif

#endif /* _LINUX_ACPI_DBG_H */
