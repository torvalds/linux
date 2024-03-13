/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Module Name: acpidump.h - Include file for acpi_dump utility
 *
 * Copyright (C) 2000 - 2022, Intel Corp.
 *
 *****************************************************************************/

/*
 * Global variables. Defined in main.c only, externed in all other files
 */
#ifdef _DECLARE_GLOBALS
#define EXTERN
#define INIT_GLOBAL(a,b)        a=b
#else
#define EXTERN                  extern
#define INIT_GLOBAL(a,b)        a
#endif

#include <acpi/acpi.h>
#include "accommon.h"
#include "actables.h"
#include "acapps.h"

/* Globals */

EXTERN u8 INIT_GLOBAL(gbl_summary_mode, FALSE);
EXTERN u8 INIT_GLOBAL(gbl_verbose_mode, FALSE);
EXTERN u8 INIT_GLOBAL(gbl_binary_mode, FALSE);
EXTERN u8 INIT_GLOBAL(gbl_dump_customized_tables, TRUE);
EXTERN u8 INIT_GLOBAL(gbl_do_not_dump_xsdt, FALSE);
EXTERN ACPI_FILE INIT_GLOBAL(gbl_output_file, NULL);
EXTERN char INIT_GLOBAL(*gbl_output_filename, NULL);
EXTERN u64 INIT_GLOBAL(gbl_rsdp_base, 0);

/* Action table used to defer requested options */

struct ap_dump_action {
	char *argument;
	u32 to_be_done;
};

#define AP_MAX_ACTIONS              32

#define AP_DUMP_ALL_TABLES          0
#define AP_DUMP_TABLE_BY_ADDRESS    1
#define AP_DUMP_TABLE_BY_NAME       2
#define AP_DUMP_TABLE_BY_FILE       3

#define AP_MAX_ACPI_FILES           256	/* Prevent infinite loops */

/* Minimum FADT sizes for various table addresses */

#define MIN_FADT_FOR_DSDT           (ACPI_FADT_OFFSET (dsdt) + sizeof (u32))
#define MIN_FADT_FOR_FACS           (ACPI_FADT_OFFSET (facs) + sizeof (u32))
#define MIN_FADT_FOR_XDSDT          (ACPI_FADT_OFFSET (Xdsdt) + sizeof (u64))
#define MIN_FADT_FOR_XFACS          (ACPI_FADT_OFFSET (Xfacs) + sizeof (u64))

/*
 * apdump - Table get/dump routines
 */
int ap_dump_table_from_file(char *pathname);

int ap_dump_table_by_name(char *signature);

int ap_dump_table_by_address(char *ascii_address);

int ap_dump_all_tables(void);

u8 ap_is_valid_header(struct acpi_table_header *table);

u8 ap_is_valid_checksum(struct acpi_table_header *table);

u32 ap_get_table_length(struct acpi_table_header *table);

/*
 * apfiles - File I/O utilities
 */
int ap_open_output_file(char *pathname);

int ap_write_to_binary_file(struct acpi_table_header *table, u32 instance);

struct acpi_table_header *ap_get_table_from_file(char *pathname,
						 u32 *file_size);
