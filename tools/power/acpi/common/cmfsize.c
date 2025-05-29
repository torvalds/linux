// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: cmfsize - Common get file size function
 *
 * Copyright (C) 2000 - 2025, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acapps.h"

#define _COMPONENT          ACPI_TOOLS
ACPI_MODULE_NAME("cmfsize")

/*******************************************************************************
 *
 * FUNCTION:    cm_get_file_size
 *
 * PARAMETERS:  file                    - Open file descriptor
 *
 * RETURN:      File Size. On error, -1 (ACPI_UINT32_MAX)
 *
 * DESCRIPTION: Get the size of a file. Uses seek-to-EOF. File must be open.
 *              Does not disturb the current file pointer.
 *
 ******************************************************************************/
u32 cm_get_file_size(ACPI_FILE file)
{
	long file_size;
	long current_offset;
	acpi_status status;

	/* Save the current file pointer, seek to EOF to obtain file size */

	current_offset = ftell(file);
	if (current_offset < 0) {
		goto offset_error;
	}

	status = fseek(file, 0, SEEK_END);
	if (ACPI_FAILURE(status)) {
		goto seek_error;
	}

	file_size = ftell(file);
	if (file_size < 0) {
		goto offset_error;
	}

	/* Restore original file pointer */

	status = fseek(file, current_offset, SEEK_SET);
	if (ACPI_FAILURE(status)) {
		goto seek_error;
	}

	return ((u32)file_size);

offset_error:
	fprintf(stderr, "Could not get file offset\n");
	return (ACPI_UINT32_MAX);

seek_error:
	fprintf(stderr, "Could not set file offset\n");
	return (ACPI_UINT32_MAX);
}
