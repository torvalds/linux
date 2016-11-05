/******************************************************************************
 *
 * Module Name: acpidump.h - Include file for acpi_dump utility
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

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
