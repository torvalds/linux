/******************************************************************************
 *
 * Module Name: oslinuxtbl - Linux OSL for obtaining ACPI tables
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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

#include "acpidump.h"

#define _COMPONENT          ACPI_OS_SERVICES
ACPI_MODULE_NAME("oslinuxtbl")

#ifndef PATH_MAX
#define PATH_MAX 256
#endif
/* List of information about obtained ACPI tables */
typedef struct osl_table_info {
	struct osl_table_info *next;
	u32 instance;
	char signature[ACPI_NAME_SIZE];

} osl_table_info;

/* Local prototypes */

static acpi_status osl_table_initialize(void);

static acpi_status
osl_table_name_from_file(char *filename, char *signature, u32 *instance);

static acpi_status osl_add_table_to_list(char *signature, u32 instance);

static acpi_status
osl_read_table_from_file(char *filename,
			 acpi_size file_offset,
			 char *signature, struct acpi_table_header **table);

static acpi_status
osl_map_table(acpi_size address,
	      char *signature, struct acpi_table_header **table);

static void osl_unmap_table(struct acpi_table_header *table);

static acpi_physical_address
osl_find_rsdp_via_efi_by_keyword(FILE * file, const char *keyword);

static acpi_physical_address osl_find_rsdp_via_efi(void);

static acpi_status osl_load_rsdp(void);

static acpi_status osl_list_customized_tables(char *directory);

static acpi_status
osl_get_customized_table(char *pathname,
			 char *signature,
			 u32 instance,
			 struct acpi_table_header **table,
			 acpi_physical_address * address);

static acpi_status osl_list_bios_tables(void);

static acpi_status
osl_get_bios_table(char *signature,
		   u32 instance,
		   struct acpi_table_header **table,
		   acpi_physical_address * address);

static acpi_status osl_get_last_status(acpi_status default_status);

/* File locations */

#define DYNAMIC_TABLE_DIR   "/sys/firmware/acpi/tables/dynamic"
#define STATIC_TABLE_DIR    "/sys/firmware/acpi/tables"
#define EFI_SYSTAB          "/sys/firmware/efi/systab"

/* Should we get dynamically loaded SSDTs from DYNAMIC_TABLE_DIR? */

u8 gbl_dump_dynamic_tables = TRUE;

/* Initialization flags */

u8 gbl_table_list_initialized = FALSE;

/* Local copies of main ACPI tables */

struct acpi_table_rsdp gbl_rsdp;
struct acpi_table_fadt *gbl_fadt = NULL;
struct acpi_table_rsdt *gbl_rsdt = NULL;
struct acpi_table_xsdt *gbl_xsdt = NULL;

/* Table addresses */

acpi_physical_address gbl_fadt_address = 0;
acpi_physical_address gbl_rsdp_address = 0;

/* Revision of RSD PTR */

u8 gbl_revision = 0;

struct osl_table_info *gbl_table_list_head = NULL;
u32 gbl_table_count = 0;

/******************************************************************************
 *
 * FUNCTION:    osl_get_last_status
 *
 * PARAMETERS:  default_status  - Default error status to return
 *
 * RETURN:      Status; Converted from errno.
 *
 * DESCRIPTION: Get last errno and conver it to acpi_status.
 *
 *****************************************************************************/

static acpi_status osl_get_last_status(acpi_status default_status)
{

	switch (errno) {
	case EACCES:
	case EPERM:

		return (AE_ACCESS);

	case ENOENT:

		return (AE_NOT_FOUND);

	case ENOMEM:

		return (AE_NO_MEMORY);

	default:

		return (default_status);
	}
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_get_table_by_address
 *
 * PARAMETERS:  address         - Physical address of the ACPI table
 *              table           - Where a pointer to the table is returned
 *
 * RETURN:      Status; Table buffer is returned if AE_OK.
 *              AE_NOT_FOUND: A valid table was not found at the address
 *
 * DESCRIPTION: Get an ACPI table via a physical memory address.
 *
 *****************************************************************************/

acpi_status
acpi_os_get_table_by_address(acpi_physical_address address,
			     struct acpi_table_header ** table)
{
	u32 table_length;
	struct acpi_table_header *mapped_table;
	struct acpi_table_header *local_table = NULL;
	acpi_status status = AE_OK;

	/* Get main ACPI tables from memory on first invocation of this function */

	status = osl_table_initialize();
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Map the table and validate it */

	status = osl_map_table(address, NULL, &mapped_table);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Copy table to local buffer and return it */

	table_length = ap_get_table_length(mapped_table);
	if (table_length == 0) {
		status = AE_BAD_HEADER;
		goto exit;
	}

	local_table = calloc(1, table_length);
	if (!local_table) {
		status = AE_NO_MEMORY;
		goto exit;
	}

	memcpy(local_table, mapped_table, table_length);

exit:
	osl_unmap_table(mapped_table);
	*table = local_table;
	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_get_table_by_name
 *
 * PARAMETERS:  signature       - ACPI Signature for desired table. Must be
 *                                a null terminated 4-character string.
 *              instance        - Multiple table support for SSDT/UEFI (0...n)
 *                                Must be 0 for other tables.
 *              table           - Where a pointer to the table is returned
 *              address         - Where the table physical address is returned
 *
 * RETURN:      Status; Table buffer and physical address returned if AE_OK.
 *              AE_LIMIT: Instance is beyond valid limit
 *              AE_NOT_FOUND: A table with the signature was not found
 *
 * NOTE:        Assumes the input signature is uppercase.
 *
 *****************************************************************************/

acpi_status
acpi_os_get_table_by_name(char *signature,
			  u32 instance,
			  struct acpi_table_header ** table,
			  acpi_physical_address * address)
{
	acpi_status status;

	/* Get main ACPI tables from memory on first invocation of this function */

	status = osl_table_initialize();
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Not a main ACPI table, attempt to extract it from the RSDT/XSDT */

	if (!gbl_dump_customized_tables) {

		/* Attempt to get the table from the memory */

		status =
		    osl_get_bios_table(signature, instance, table, address);
	} else {
		/* Attempt to get the table from the static directory */

		status = osl_get_customized_table(STATIC_TABLE_DIR, signature,
						  instance, table, address);
	}

	if (ACPI_FAILURE(status) && status == AE_LIMIT) {
		if (gbl_dump_dynamic_tables) {

			/* Attempt to get a dynamic table */

			status =
			    osl_get_customized_table(DYNAMIC_TABLE_DIR,
						     signature, instance, table,
						     address);
		}
	}

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    osl_add_table_to_list
 *
 * PARAMETERS:  signature       - Table signature
 *              instance        - Table instance
 *
 * RETURN:      Status; Successfully added if AE_OK.
 *              AE_NO_MEMORY: Memory allocation error
 *
 * DESCRIPTION: Insert a table structure into OSL table list.
 *
 *****************************************************************************/

static acpi_status osl_add_table_to_list(char *signature, u32 instance)
{
	struct osl_table_info *new_info;
	struct osl_table_info *next;
	u32 next_instance = 0;
	u8 found = FALSE;

	new_info = calloc(1, sizeof(struct osl_table_info));
	if (!new_info) {
		return (AE_NO_MEMORY);
	}

	ACPI_MOVE_NAME(new_info->signature, signature);

	if (!gbl_table_list_head) {
		gbl_table_list_head = new_info;
	} else {
		next = gbl_table_list_head;
		while (1) {
			if (ACPI_COMPARE_NAME(next->signature, signature)) {
				if (next->instance == instance) {
					found = TRUE;
				}
				if (next->instance >= next_instance) {
					next_instance = next->instance + 1;
				}
			}

			if (!next->next) {
				break;
			}
			next = next->next;
		}
		next->next = new_info;
	}

	if (found) {
		if (instance) {
			fprintf(stderr,
				"%4.4s: Warning unmatched table instance %d, expected %d\n",
				signature, instance, next_instance);
		}
		instance = next_instance;
	}

	new_info->instance = instance;
	gbl_table_count++;

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_get_table_by_index
 *
 * PARAMETERS:  index           - Which table to get
 *              table           - Where a pointer to the table is returned
 *              instance        - Where a pointer to the table instance no. is
 *                                returned
 *              address         - Where the table physical address is returned
 *
 * RETURN:      Status; Table buffer and physical address returned if AE_OK.
 *              AE_LIMIT: Index is beyond valid limit
 *
 * DESCRIPTION: Get an ACPI table via an index value (0 through n). Returns
 *              AE_LIMIT when an invalid index is reached. Index is not
 *              necessarily an index into the RSDT/XSDT.
 *
 *****************************************************************************/

acpi_status
acpi_os_get_table_by_index(u32 index,
			   struct acpi_table_header ** table,
			   u32 *instance, acpi_physical_address * address)
{
	struct osl_table_info *info;
	acpi_status status;
	u32 i;

	/* Get main ACPI tables from memory on first invocation of this function */

	status = osl_table_initialize();
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Validate Index */

	if (index >= gbl_table_count) {
		return (AE_LIMIT);
	}

	/* Point to the table list entry specified by the Index argument */

	info = gbl_table_list_head;
	for (i = 0; i < index; i++) {
		info = info->next;
	}

	/* Now we can just get the table via the signature */

	status = acpi_os_get_table_by_name(info->signature, info->instance,
					   table, address);

	if (ACPI_SUCCESS(status)) {
		*instance = info->instance;
	}
	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    osl_find_rsdp_via_efi_by_keyword
 *
 * PARAMETERS:  keyword         - Character string indicating ACPI GUID version
 *                                in the EFI table
 *
 * RETURN:      RSDP address if found
 *
 * DESCRIPTION: Find RSDP address via EFI using keyword indicating the ACPI
 *              GUID version.
 *
 *****************************************************************************/

static acpi_physical_address
osl_find_rsdp_via_efi_by_keyword(FILE * file, const char *keyword)
{
	char buffer[80];
	unsigned long long address = 0;
	char format[32];

	snprintf(format, 32, "%s=%s", keyword, "%llx");
	fseek(file, 0, SEEK_SET);
	while (fgets(buffer, 80, file)) {
		if (sscanf(buffer, format, &address) == 1) {
			break;
		}
	}

	return ((acpi_physical_address) (address));
}

/******************************************************************************
 *
 * FUNCTION:    osl_find_rsdp_via_efi
 *
 * PARAMETERS:  None
 *
 * RETURN:      RSDP address if found
 *
 * DESCRIPTION: Find RSDP address via EFI.
 *
 *****************************************************************************/

static acpi_physical_address osl_find_rsdp_via_efi(void)
{
	FILE *file;
	acpi_physical_address address = 0;

	file = fopen(EFI_SYSTAB, "r");
	if (file) {
		address = osl_find_rsdp_via_efi_by_keyword(file, "ACPI20");
		if (!address) {
			address =
			    osl_find_rsdp_via_efi_by_keyword(file, "ACPI");
		}
		fclose(file);
	}

	return (address);
}

/******************************************************************************
 *
 * FUNCTION:    osl_load_rsdp
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Scan and load RSDP.
 *
 *****************************************************************************/

static acpi_status osl_load_rsdp(void)
{
	struct acpi_table_header *mapped_table;
	u8 *rsdp_address;
	acpi_physical_address rsdp_base;
	acpi_size rsdp_size;

	/* Get RSDP from memory */

	rsdp_size = sizeof(struct acpi_table_rsdp);
	if (gbl_rsdp_base) {
		rsdp_base = gbl_rsdp_base;
	} else {
		rsdp_base = osl_find_rsdp_via_efi();
	}

	if (!rsdp_base) {
		rsdp_base = ACPI_HI_RSDP_WINDOW_BASE;
		rsdp_size = ACPI_HI_RSDP_WINDOW_SIZE;
	}

	rsdp_address = acpi_os_map_memory(rsdp_base, rsdp_size);
	if (!rsdp_address) {
		return (osl_get_last_status(AE_BAD_ADDRESS));
	}

	/* Search low memory for the RSDP */

	mapped_table = ACPI_CAST_PTR(struct acpi_table_header,
				     acpi_tb_scan_memory_for_rsdp(rsdp_address,
								  rsdp_size));
	if (!mapped_table) {
		acpi_os_unmap_memory(rsdp_address, rsdp_size);
		return (AE_NOT_FOUND);
	}

	gbl_rsdp_address =
	    rsdp_base + (ACPI_CAST8(mapped_table) - rsdp_address);

	memcpy(&gbl_rsdp, mapped_table, sizeof(struct acpi_table_rsdp));
	acpi_os_unmap_memory(rsdp_address, rsdp_size);

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    osl_can_use_xsdt
 *
 * PARAMETERS:  None
 *
 * RETURN:      TRUE if XSDT is allowed to be used.
 *
 * DESCRIPTION: This function collects logic that can be used to determine if
 *              XSDT should be used instead of RSDT.
 *
 *****************************************************************************/

static u8 osl_can_use_xsdt(void)
{
	if (gbl_revision && !acpi_gbl_do_not_use_xsdt) {
		return (TRUE);
	} else {
		return (FALSE);
	}
}

/******************************************************************************
 *
 * FUNCTION:    osl_table_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize ACPI table data. Get and store main ACPI tables to
 *              local variables. Main ACPI tables include RSDT, FADT, RSDT,
 *              and/or XSDT.
 *
 *****************************************************************************/

static acpi_status osl_table_initialize(void)
{
	acpi_status status;
	acpi_physical_address address;

	if (gbl_table_list_initialized) {
		return (AE_OK);
	}

	if (!gbl_dump_customized_tables) {

		/* Get RSDP from memory */

		status = osl_load_rsdp();
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		/* Get XSDT from memory */

		if (gbl_rsdp.revision && !gbl_do_not_dump_xsdt) {
			if (gbl_xsdt) {
				free(gbl_xsdt);
				gbl_xsdt = NULL;
			}

			gbl_revision = 2;
			status = osl_get_bios_table(ACPI_SIG_XSDT, 0,
						    ACPI_CAST_PTR(struct
								  acpi_table_header
								  *, &gbl_xsdt),
						    &address);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
		}

		/* Get RSDT from memory */

		if (gbl_rsdp.rsdt_physical_address) {
			if (gbl_rsdt) {
				free(gbl_rsdt);
				gbl_rsdt = NULL;
			}

			status = osl_get_bios_table(ACPI_SIG_RSDT, 0,
						    ACPI_CAST_PTR(struct
								  acpi_table_header
								  *, &gbl_rsdt),
						    &address);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
		}

		/* Get FADT from memory */

		if (gbl_fadt) {
			free(gbl_fadt);
			gbl_fadt = NULL;
		}

		status = osl_get_bios_table(ACPI_SIG_FADT, 0,
					    ACPI_CAST_PTR(struct
							  acpi_table_header *,
							  &gbl_fadt),
					    &gbl_fadt_address);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		/* Add mandatory tables to global table list first */

		status = osl_add_table_to_list(ACPI_RSDP_NAME, 0);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		status = osl_add_table_to_list(ACPI_SIG_RSDT, 0);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		if (gbl_revision == 2) {
			status = osl_add_table_to_list(ACPI_SIG_XSDT, 0);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
		}

		status = osl_add_table_to_list(ACPI_SIG_DSDT, 0);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		status = osl_add_table_to_list(ACPI_SIG_FACS, 0);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		/* Add all tables found in the memory */

		status = osl_list_bios_tables();
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	} else {
		/* Add all tables found in the static directory */

		status = osl_list_customized_tables(STATIC_TABLE_DIR);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	}

	if (gbl_dump_dynamic_tables) {

		/* Add all dynamically loaded tables in the dynamic directory */

		status = osl_list_customized_tables(DYNAMIC_TABLE_DIR);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	}

	gbl_table_list_initialized = TRUE;
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    osl_list_bios_tables
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status; Table list is initialized if AE_OK.
 *
 * DESCRIPTION: Add ACPI tables to the table list from memory.
 *
 * NOTE:        This works on Linux as table customization does not modify the
 *              addresses stored in RSDP/RSDT/XSDT/FADT.
 *
 *****************************************************************************/

static acpi_status osl_list_bios_tables(void)
{
	struct acpi_table_header *mapped_table = NULL;
	u8 *table_data;
	u8 number_of_tables;
	u8 item_size;
	acpi_physical_address table_address = 0;
	acpi_status status = AE_OK;
	u32 i;

	if (osl_can_use_xsdt()) {
		item_size = sizeof(u64);
		table_data =
		    ACPI_CAST8(gbl_xsdt) + sizeof(struct acpi_table_header);
		number_of_tables =
		    (u8)((gbl_xsdt->header.length -
			  sizeof(struct acpi_table_header))
			 / item_size);
	} else {		/* Use RSDT if XSDT is not available */

		item_size = sizeof(u32);
		table_data =
		    ACPI_CAST8(gbl_rsdt) + sizeof(struct acpi_table_header);
		number_of_tables =
		    (u8)((gbl_rsdt->header.length -
			  sizeof(struct acpi_table_header))
			 / item_size);
	}

	/* Search RSDT/XSDT for the requested table */

	for (i = 0; i < number_of_tables; ++i, table_data += item_size) {
		if (osl_can_use_xsdt()) {
			table_address =
			    (acpi_physical_address) (*ACPI_CAST64(table_data));
		} else {
			table_address =
			    (acpi_physical_address) (*ACPI_CAST32(table_data));
		}

		/* Skip NULL entries in RSDT/XSDT */

		if (!table_address) {
			continue;
		}

		status = osl_map_table(table_address, NULL, &mapped_table);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		osl_add_table_to_list(mapped_table->signature, 0);
		osl_unmap_table(mapped_table);
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    osl_get_bios_table
 *
 * PARAMETERS:  signature       - ACPI Signature for common table. Must be
 *                                a null terminated 4-character string.
 *              instance        - Multiple table support for SSDT/UEFI (0...n)
 *                                Must be 0 for other tables.
 *              table           - Where a pointer to the table is returned
 *              address         - Where the table physical address is returned
 *
 * RETURN:      Status; Table buffer and physical address returned if AE_OK.
 *              AE_LIMIT: Instance is beyond valid limit
 *              AE_NOT_FOUND: A table with the signature was not found
 *
 * DESCRIPTION: Get a BIOS provided ACPI table
 *
 * NOTE:        Assumes the input signature is uppercase.
 *
 *****************************************************************************/

static acpi_status
osl_get_bios_table(char *signature,
		   u32 instance,
		   struct acpi_table_header **table,
		   acpi_physical_address * address)
{
	struct acpi_table_header *local_table = NULL;
	struct acpi_table_header *mapped_table = NULL;
	u8 *table_data;
	u8 number_of_tables;
	u8 item_size;
	u32 current_instance = 0;
	acpi_physical_address table_address = 0;
	u32 table_length = 0;
	acpi_status status = AE_OK;
	u32 i;

	/* Handle special tables whose addresses are not in RSDT/XSDT */

	if (ACPI_COMPARE_NAME(signature, ACPI_RSDP_NAME) ||
	    ACPI_COMPARE_NAME(signature, ACPI_SIG_RSDT) ||
	    ACPI_COMPARE_NAME(signature, ACPI_SIG_XSDT) ||
	    ACPI_COMPARE_NAME(signature, ACPI_SIG_DSDT) ||
	    ACPI_COMPARE_NAME(signature, ACPI_SIG_FACS)) {
		if (instance > 0) {
			return (AE_LIMIT);
		}

		/*
		 * Get the appropriate address, either 32-bit or 64-bit. Be very
		 * careful about the FADT length and validate table addresses.
		 * Note: The 64-bit addresses have priority.
		 */
		if (ACPI_COMPARE_NAME(signature, ACPI_SIG_DSDT)) {
			if ((gbl_fadt->header.length >= MIN_FADT_FOR_XDSDT) &&
			    gbl_fadt->Xdsdt) {
				table_address =
				    (acpi_physical_address) gbl_fadt->Xdsdt;
			} else
			    if ((gbl_fadt->header.length >= MIN_FADT_FOR_DSDT)
				&& gbl_fadt->dsdt) {
				table_address =
				    (acpi_physical_address) gbl_fadt->dsdt;
			}
		} else if (ACPI_COMPARE_NAME(signature, ACPI_SIG_FACS)) {
			if ((gbl_fadt->header.length >= MIN_FADT_FOR_XFACS) &&
			    gbl_fadt->Xfacs) {
				table_address =
				    (acpi_physical_address) gbl_fadt->Xfacs;
			} else
			    if ((gbl_fadt->header.length >= MIN_FADT_FOR_FACS)
				&& gbl_fadt->facs) {
				table_address =
				    (acpi_physical_address) gbl_fadt->facs;
			}
		} else if (ACPI_COMPARE_NAME(signature, ACPI_SIG_XSDT)) {
			if (!gbl_revision) {
				return (AE_BAD_SIGNATURE);
			}
			table_address =
			    (acpi_physical_address) gbl_rsdp.
			    xsdt_physical_address;
		} else if (ACPI_COMPARE_NAME(signature, ACPI_SIG_RSDT)) {
			table_address =
			    (acpi_physical_address) gbl_rsdp.
			    rsdt_physical_address;
		} else {
			table_address =
			    (acpi_physical_address) gbl_rsdp_address;
			signature = ACPI_SIG_RSDP;
		}

		/* Now we can get the requested special table */

		status = osl_map_table(table_address, signature, &mapped_table);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		table_length = ap_get_table_length(mapped_table);
	} else {		/* Case for a normal ACPI table */

		if (osl_can_use_xsdt()) {
			item_size = sizeof(u64);
			table_data =
			    ACPI_CAST8(gbl_xsdt) +
			    sizeof(struct acpi_table_header);
			number_of_tables =
			    (u8)((gbl_xsdt->header.length -
				  sizeof(struct acpi_table_header))
				 / item_size);
		} else {	/* Use RSDT if XSDT is not available */

			item_size = sizeof(u32);
			table_data =
			    ACPI_CAST8(gbl_rsdt) +
			    sizeof(struct acpi_table_header);
			number_of_tables =
			    (u8)((gbl_rsdt->header.length -
				  sizeof(struct acpi_table_header))
				 / item_size);
		}

		/* Search RSDT/XSDT for the requested table */

		for (i = 0; i < number_of_tables; ++i, table_data += item_size) {
			if (osl_can_use_xsdt()) {
				table_address =
				    (acpi_physical_address) (*ACPI_CAST64
							     (table_data));
			} else {
				table_address =
				    (acpi_physical_address) (*ACPI_CAST32
							     (table_data));
			}

			/* Skip NULL entries in RSDT/XSDT */

			if (!table_address) {
				continue;
			}

			status =
			    osl_map_table(table_address, NULL, &mapped_table);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
			table_length = mapped_table->length;

			/* Does this table match the requested signature? */

			if (!ACPI_COMPARE_NAME
			    (mapped_table->signature, signature)) {
				osl_unmap_table(mapped_table);
				mapped_table = NULL;
				continue;
			}

			/* Match table instance (for SSDT/UEFI tables) */

			if (current_instance != instance) {
				osl_unmap_table(mapped_table);
				mapped_table = NULL;
				current_instance++;
				continue;
			}

			break;
		}
	}

	if (!mapped_table) {
		return (AE_LIMIT);
	}

	if (table_length == 0) {
		status = AE_BAD_HEADER;
		goto exit;
	}

	/* Copy table to local buffer and return it */

	local_table = calloc(1, table_length);
	if (!local_table) {
		status = AE_NO_MEMORY;
		goto exit;
	}

	memcpy(local_table, mapped_table, table_length);
	*address = table_address;
	*table = local_table;

exit:
	osl_unmap_table(mapped_table);
	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    osl_list_customized_tables
 *
 * PARAMETERS:  directory           - Directory that contains the tables
 *
 * RETURN:      Status; Table list is initialized if AE_OK.
 *
 * DESCRIPTION: Add ACPI tables to the table list from a directory.
 *
 *****************************************************************************/

static acpi_status osl_list_customized_tables(char *directory)
{
	void *table_dir;
	u32 instance;
	char temp_name[ACPI_NAME_SIZE];
	char *filename;
	acpi_status status = AE_OK;

	/* Open the requested directory */

	table_dir = acpi_os_open_directory(directory, "*", REQUEST_FILE_ONLY);
	if (!table_dir) {
		return (osl_get_last_status(AE_NOT_FOUND));
	}

	/* Examine all entries in this directory */

	while ((filename = acpi_os_get_next_filename(table_dir))) {

		/* Extract table name and instance number */

		status =
		    osl_table_name_from_file(filename, temp_name, &instance);

		/* Ignore meaningless files */

		if (ACPI_FAILURE(status)) {
			continue;
		}

		/* Add new info node to global table list */

		status = osl_add_table_to_list(temp_name, instance);
		if (ACPI_FAILURE(status)) {
			break;
		}
	}

	acpi_os_close_directory(table_dir);
	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    osl_map_table
 *
 * PARAMETERS:  address             - Address of the table in memory
 *              signature           - Optional ACPI Signature for desired table.
 *                                    Null terminated 4-character string.
 *              table               - Where a pointer to the mapped table is
 *                                    returned
 *
 * RETURN:      Status; Mapped table is returned if AE_OK.
 *              AE_NOT_FOUND: A valid table was not found at the address
 *
 * DESCRIPTION: Map entire ACPI table into caller's address space.
 *
 *****************************************************************************/

static acpi_status
osl_map_table(acpi_size address,
	      char *signature, struct acpi_table_header **table)
{
	struct acpi_table_header *mapped_table;
	u32 length;

	if (!address) {
		return (AE_BAD_ADDRESS);
	}

	/*
	 * Map the header so we can get the table length.
	 * Use sizeof (struct acpi_table_header) as:
	 * 1. it is bigger than 24 to include RSDP->Length
	 * 2. it is smaller than sizeof (struct acpi_table_rsdp)
	 */
	mapped_table =
	    acpi_os_map_memory(address, sizeof(struct acpi_table_header));
	if (!mapped_table) {
		fprintf(stderr, "Could not map table header at 0x%8.8X%8.8X\n",
			ACPI_FORMAT_UINT64(address));
		return (osl_get_last_status(AE_BAD_ADDRESS));
	}

	/* If specified, signature must match */

	if (signature) {
		if (ACPI_VALIDATE_RSDP_SIG(signature)) {
			if (!ACPI_VALIDATE_RSDP_SIG(mapped_table->signature)) {
				acpi_os_unmap_memory(mapped_table,
						     sizeof(struct
							    acpi_table_header));
				return (AE_BAD_SIGNATURE);
			}
		} else
		    if (!ACPI_COMPARE_NAME(signature, mapped_table->signature))
		{
			acpi_os_unmap_memory(mapped_table,
					     sizeof(struct acpi_table_header));
			return (AE_BAD_SIGNATURE);
		}
	}

	/* Map the entire table */

	length = ap_get_table_length(mapped_table);
	acpi_os_unmap_memory(mapped_table, sizeof(struct acpi_table_header));
	if (length == 0) {
		return (AE_BAD_HEADER);
	}

	mapped_table = acpi_os_map_memory(address, length);
	if (!mapped_table) {
		fprintf(stderr,
			"Could not map table at 0x%8.8X%8.8X length %8.8X\n",
			ACPI_FORMAT_UINT64(address), length);
		return (osl_get_last_status(AE_INVALID_TABLE_LENGTH));
	}

	(void)ap_is_valid_checksum(mapped_table);

	*table = mapped_table;
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    osl_unmap_table
 *
 * PARAMETERS:  table               - A pointer to the mapped table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Unmap entire ACPI table.
 *
 *****************************************************************************/

static void osl_unmap_table(struct acpi_table_header *table)
{
	if (table) {
		acpi_os_unmap_memory(table, ap_get_table_length(table));
	}
}

/******************************************************************************
 *
 * FUNCTION:    osl_table_name_from_file
 *
 * PARAMETERS:  filename            - File that contains the desired table
 *              signature           - Pointer to 4-character buffer to store
 *                                    extracted table signature.
 *              instance            - Pointer to integer to store extracted
 *                                    table instance number.
 *
 * RETURN:      Status; Table name is extracted if AE_OK.
 *
 * DESCRIPTION: Extract table signature and instance number from a table file
 *              name.
 *
 *****************************************************************************/

static acpi_status
osl_table_name_from_file(char *filename, char *signature, u32 *instance)
{

	/* Ignore meaningless files */

	if (strlen(filename) < ACPI_NAME_SIZE) {
		return (AE_BAD_SIGNATURE);
	}

	/* Extract instance number */

	if (isdigit((int)filename[ACPI_NAME_SIZE])) {
		sscanf(&filename[ACPI_NAME_SIZE], "%u", instance);
	} else if (strlen(filename) != ACPI_NAME_SIZE) {
		return (AE_BAD_SIGNATURE);
	} else {
		*instance = 0;
	}

	/* Extract signature */

	ACPI_MOVE_NAME(signature, filename);
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    osl_read_table_from_file
 *
 * PARAMETERS:  filename            - File that contains the desired table
 *              file_offset         - Offset of the table in file
 *              signature           - Optional ACPI Signature for desired table.
 *                                    A null terminated 4-character string.
 *              table               - Where a pointer to the table is returned
 *
 * RETURN:      Status; Table buffer is returned if AE_OK.
 *
 * DESCRIPTION: Read a ACPI table from a file.
 *
 *****************************************************************************/

static acpi_status
osl_read_table_from_file(char *filename,
			 acpi_size file_offset,
			 char *signature, struct acpi_table_header **table)
{
	FILE *table_file;
	struct acpi_table_header header;
	struct acpi_table_header *local_table = NULL;
	u32 table_length;
	s32 count;
	acpi_status status = AE_OK;

	/* Open the file */

	table_file = fopen(filename, "rb");
	if (table_file == NULL) {
		fprintf(stderr, "Could not open table file: %s\n", filename);
		return (osl_get_last_status(AE_NOT_FOUND));
	}

	fseek(table_file, file_offset, SEEK_SET);

	/* Read the Table header to get the table length */

	count = fread(&header, 1, sizeof(struct acpi_table_header), table_file);
	if (count != sizeof(struct acpi_table_header)) {
		fprintf(stderr, "Could not read table header: %s\n", filename);
		status = AE_BAD_HEADER;
		goto exit;
	}

	/* If signature is specified, it must match the table */

	if (signature) {
		if (ACPI_VALIDATE_RSDP_SIG(signature)) {
			if (!ACPI_VALIDATE_RSDP_SIG(header.signature)) {
				fprintf(stderr,
					"Incorrect RSDP signature: found %8.8s\n",
					header.signature);
				status = AE_BAD_SIGNATURE;
				goto exit;
			}
		} else if (!ACPI_COMPARE_NAME(signature, header.signature)) {
			fprintf(stderr,
				"Incorrect signature: Expecting %4.4s, found %4.4s\n",
				signature, header.signature);
			status = AE_BAD_SIGNATURE;
			goto exit;
		}
	}

	table_length = ap_get_table_length(&header);
	if (table_length == 0) {
		status = AE_BAD_HEADER;
		goto exit;
	}

	/* Read the entire table into a local buffer */

	local_table = calloc(1, table_length);
	if (!local_table) {
		fprintf(stderr,
			"%4.4s: Could not allocate buffer for table of length %X\n",
			header.signature, table_length);
		status = AE_NO_MEMORY;
		goto exit;
	}

	fseek(table_file, file_offset, SEEK_SET);

	count = fread(local_table, 1, table_length, table_file);
	if (count != table_length) {
		fprintf(stderr, "%4.4s: Could not read table content\n",
			header.signature);
		status = AE_INVALID_TABLE_LENGTH;
		goto exit;
	}

	/* Validate checksum */

	(void)ap_is_valid_checksum(local_table);

exit:
	fclose(table_file);
	*table = local_table;
	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    osl_get_customized_table
 *
 * PARAMETERS:  pathname        - Directory to find Linux customized table
 *              signature       - ACPI Signature for desired table. Must be
 *                                a null terminated 4-character string.
 *              instance        - Multiple table support for SSDT/UEFI (0...n)
 *                                Must be 0 for other tables.
 *              table           - Where a pointer to the table is returned
 *              address         - Where the table physical address is returned
 *
 * RETURN:      Status; Table buffer is returned if AE_OK.
 *              AE_LIMIT: Instance is beyond valid limit
 *              AE_NOT_FOUND: A table with the signature was not found
 *
 * DESCRIPTION: Get an OS customized table.
 *
 *****************************************************************************/

static acpi_status
osl_get_customized_table(char *pathname,
			 char *signature,
			 u32 instance,
			 struct acpi_table_header **table,
			 acpi_physical_address * address)
{
	void *table_dir;
	u32 current_instance = 0;
	char temp_name[ACPI_NAME_SIZE];
	char table_filename[PATH_MAX];
	char *filename;
	acpi_status status;

	/* Open the directory for customized tables */

	table_dir = acpi_os_open_directory(pathname, "*", REQUEST_FILE_ONLY);
	if (!table_dir) {
		return (osl_get_last_status(AE_NOT_FOUND));
	}

	/* Attempt to find the table in the directory */

	while ((filename = acpi_os_get_next_filename(table_dir))) {

		/* Ignore meaningless files */

		if (!ACPI_COMPARE_NAME(filename, signature)) {
			continue;
		}

		/* Extract table name and instance number */

		status =
		    osl_table_name_from_file(filename, temp_name,
					     &current_instance);

		/* Ignore meaningless files */

		if (ACPI_FAILURE(status) || current_instance != instance) {
			continue;
		}

		/* Create the table pathname */

		if (instance != 0) {
			sprintf(table_filename, "%s/%4.4s%d", pathname,
				temp_name, instance);
		} else {
			sprintf(table_filename, "%s/%4.4s", pathname,
				temp_name);
		}
		break;
	}

	acpi_os_close_directory(table_dir);

	if (!filename) {
		return (AE_LIMIT);
	}

	/* There is no physical address saved for customized tables, use zero */

	*address = 0;
	status = osl_read_table_from_file(table_filename, 0, NULL, table);

	return (status);
}
