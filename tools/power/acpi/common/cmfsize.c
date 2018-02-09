/******************************************************************************
 *
 * Module Name: cfsize - Common get file size function
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
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
