/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  fw_tables.h - Parsing support for ACPI and ACPI-like tables provided by
 *                platform or device firmware
 *
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2023 Intel Corp.
 */
#ifndef _FW_TABLE_H_
#define _FW_TABLE_H_

union acpi_subtable_headers;

typedef int (*acpi_tbl_entry_handler)(union acpi_subtable_headers *header,
				      const unsigned long end);

typedef int (*acpi_tbl_entry_handler_arg)(union acpi_subtable_headers *header,
					  void *arg, const unsigned long end);

struct acpi_subtable_proc {
	int id;
	acpi_tbl_entry_handler handler;
	acpi_tbl_entry_handler_arg handler_arg;
	void *arg;
	int count;
};

union fw_table_header {
	struct acpi_table_header acpi;
	struct acpi_table_cdat cdat;
};

union acpi_subtable_headers {
	struct acpi_subtable_header common;
	struct acpi_hmat_structure hmat;
	struct acpi_prmt_module_header prmt;
	struct acpi_cedt_header cedt;
	struct acpi_cdat_header cdat;
};

int acpi_parse_entries_array(char *id, unsigned long table_size,
			     union fw_table_header *table_header,
			     unsigned long max_length,
			     struct acpi_subtable_proc *proc,
			     int proc_num, unsigned int max_entries);

int cdat_table_parse(enum acpi_cdat_type type,
		     acpi_tbl_entry_handler_arg handler_arg, void *arg,
		     struct acpi_table_cdat *table_header,
		     unsigned long length);

/* CXL is the only non-ACPI consumer of the FIRMWARE_TABLE library */
#if IS_ENABLED(CONFIG_ACPI) && !IS_ENABLED(CONFIG_CXL_BUS)
#define EXPORT_SYMBOL_FWTBL_LIB(x) EXPORT_SYMBOL_ACPI_LIB(x)
#define __init_or_fwtbl_lib __init_or_acpilib
#else
#define EXPORT_SYMBOL_FWTBL_LIB(x) EXPORT_SYMBOL_NS_GPL(x, CXL)
#define __init_or_fwtbl_lib
#endif

#endif
