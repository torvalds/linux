/******************************************************************************
 *
 * Module Name: apfiles - File-related functions for acpidump utility
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

#include "acpidump.h"

/* Local prototypes */

static int ap_is_existing_file(char *pathname);

/******************************************************************************
 *
 * FUNCTION:    ap_is_existing_file
 *
 * PARAMETERS:  pathname            - Output filename
 *
 * RETURN:      0 on success
 *
 * DESCRIPTION: Query for file overwrite if it already exists.
 *
 ******************************************************************************/

static int ap_is_existing_file(char *pathname)
{
#ifndef _GNU_EFI
	struct stat stat_info;

	if (!stat(pathname, &stat_info)) {
		fprintf(stderr,
			"Target path already exists, overwrite? [y|n] ");

		if (getchar() != 'y') {
			return (-1);
		}
	}
#endif

	return 0;
}

/******************************************************************************
 *
 * FUNCTION:    ap_open_output_file
 *
 * PARAMETERS:  pathname            - Output filename
 *
 * RETURN:      Open file handle
 *
 * DESCRIPTION: Open a text output file for acpidump. Checks if file already
 *              exists.
 *
 ******************************************************************************/

int ap_open_output_file(char *pathname)
{
	ACPI_FILE file;

	/* If file exists, prompt for overwrite */

	if (ap_is_existing_file(pathname) != 0) {
		return (-1);
	}

	/* Point stdout to the file */

	file = fopen(pathname, "w");
	if (!file) {
		fprintf(stderr, "Could not open output file: %s\n", pathname);
		return (-1);
	}

	/* Save the file and path */

	gbl_output_file = file;
	gbl_output_filename = pathname;
	return (0);
}

/******************************************************************************
 *
 * FUNCTION:    ap_write_to_binary_file
 *
 * PARAMETERS:  table               - ACPI table to be written
 *              instance            - ACPI table instance no. to be written
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write an ACPI table to a binary file. Builds the output
 *              filename from the table signature.
 *
 ******************************************************************************/

int ap_write_to_binary_file(struct acpi_table_header *table, u32 instance)
{
	char filename[ACPI_NAME_SIZE + 16];
	char instance_str[16];
	ACPI_FILE file;
	acpi_size actual;
	u32 table_length;

	/* Obtain table length */

	table_length = ap_get_table_length(table);

	/* Construct lower-case filename from the table local signature */

	if (ACPI_VALIDATE_RSDP_SIG(table->signature)) {
		ACPI_MOVE_NAME(filename, ACPI_RSDP_NAME);
	} else {
		ACPI_MOVE_NAME(filename, table->signature);
	}

	filename[0] = (char)tolower((int)filename[0]);
	filename[1] = (char)tolower((int)filename[1]);
	filename[2] = (char)tolower((int)filename[2]);
	filename[3] = (char)tolower((int)filename[3]);
	filename[ACPI_NAME_SIZE] = 0;

	/* Handle multiple SSDts - create different filenames for each */

	if (instance > 0) {
		snprintf(instance_str, sizeof(instance_str), "%u", instance);
		strcat(filename, instance_str);
	}

	strcat(filename, FILE_SUFFIX_BINARY_TABLE);

	if (gbl_verbose_mode) {
		fprintf(stderr,
			"Writing [%4.4s] to binary file: %s 0x%X (%u) bytes\n",
			table->signature, filename, table->length,
			table->length);
	}

	/* Open the file and dump the entire table in binary mode */

	file = fopen(filename, "wb");
	if (!file) {
		fprintf(stderr, "Could not open output file: %s\n", filename);
		return (-1);
	}

	actual = fwrite(table, 1, table_length, file);
	if (actual != table_length) {
		fprintf(stderr, "Error writing binary output file: %s\n",
			filename);
		fclose(file);
		return (-1);
	}

	fclose(file);
	return (0);
}

/******************************************************************************
 *
 * FUNCTION:    ap_get_table_from_file
 *
 * PARAMETERS:  pathname            - File containing the binary ACPI table
 *              out_file_size       - Where the file size is returned
 *
 * RETURN:      Buffer containing the ACPI table. NULL on error.
 *
 * DESCRIPTION: Open a file and read it entirely into a new buffer
 *
 ******************************************************************************/

struct acpi_table_header *ap_get_table_from_file(char *pathname,
						 u32 *out_file_size)
{
	struct acpi_table_header *buffer = NULL;
	ACPI_FILE file;
	u32 file_size;
	acpi_size actual;

	/* Must use binary mode */

	file = fopen(pathname, "rb");
	if (!file) {
		fprintf(stderr, "Could not open input file: %s\n", pathname);
		return (NULL);
	}

	/* Need file size to allocate a buffer */

	file_size = cm_get_file_size(file);
	if (file_size == ACPI_UINT32_MAX) {
		fprintf(stderr,
			"Could not get input file size: %s\n", pathname);
		goto cleanup;
	}

	/* Allocate a buffer for the entire file */

	buffer = ACPI_ALLOCATE_ZEROED(file_size);
	if (!buffer) {
		fprintf(stderr,
			"Could not allocate file buffer of size: %u\n",
			file_size);
		goto cleanup;
	}

	/* Read the entire file */

	actual = fread(buffer, 1, file_size, file);
	if (actual != file_size) {
		fprintf(stderr, "Could not read input file: %s\n", pathname);
		ACPI_FREE(buffer);
		buffer = NULL;
		goto cleanup;
	}

	*out_file_size = file_size;

cleanup:
	fclose(file);
	return (buffer);
}
