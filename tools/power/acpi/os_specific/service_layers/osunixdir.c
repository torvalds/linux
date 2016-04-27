/******************************************************************************
 *
 * Module Name: osunixdir - Unix directory access interfaces
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

#include <acpi/acpi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>
#include <ctype.h>
#include <sys/stat.h>

/*
 * Allocated structure returned from os_open_directory
 */
typedef struct external_find_info {
	char *dir_pathname;
	DIR *dir_ptr;
	char temp_buffer[256];
	char *wildcard_spec;
	char requested_file_type;

} external_find_info;

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_open_directory
 *
 * PARAMETERS:  dir_pathname        - Full pathname to the directory
 *              wildcard_spec       - string of the form "*.c", etc.
 *
 * RETURN:      A directory "handle" to be used in subsequent search operations.
 *              NULL returned on failure.
 *
 * DESCRIPTION: Open a directory in preparation for a wildcard search
 *
 ******************************************************************************/

void *acpi_os_open_directory(char *dir_pathname,
			     char *wildcard_spec, char requested_file_type)
{
	struct external_find_info *external_info;
	DIR *dir;

	/* Allocate the info struct that will be returned to the caller */

	external_info = calloc(1, sizeof(struct external_find_info));
	if (!external_info) {
		return (NULL);
	}

	/* Get the directory stream */

	dir = opendir(dir_pathname);
	if (!dir) {
		fprintf(stderr, "Cannot open directory - %s\n", dir_pathname);
		free(external_info);
		return (NULL);
	}

	/* Save the info in the return structure */

	external_info->wildcard_spec = wildcard_spec;
	external_info->requested_file_type = requested_file_type;
	external_info->dir_pathname = dir_pathname;
	external_info->dir_ptr = dir;
	return (external_info);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_get_next_filename
 *
 * PARAMETERS:  dir_handle          - Created via acpi_os_open_directory
 *
 * RETURN:      Next filename matched. NULL if no more matches.
 *
 * DESCRIPTION: Get the next file in the directory that matches the wildcard
 *              specification.
 *
 ******************************************************************************/

char *acpi_os_get_next_filename(void *dir_handle)
{
	struct external_find_info *external_info = dir_handle;
	struct dirent *dir_entry;
	char *temp_str;
	int str_len;
	struct stat temp_stat;
	int err;

	while ((dir_entry = readdir(external_info->dir_ptr))) {
		if (!fnmatch
		    (external_info->wildcard_spec, dir_entry->d_name, 0)) {
			if (dir_entry->d_name[0] == '.') {
				continue;
			}

			str_len = strlen(dir_entry->d_name) +
			    strlen(external_info->dir_pathname) + 2;

			temp_str = calloc(str_len, 1);
			if (!temp_str) {
				fprintf(stderr,
					"Could not allocate buffer for temporary string\n");
				return (NULL);
			}

			strcpy(temp_str, external_info->dir_pathname);
			strcat(temp_str, "/");
			strcat(temp_str, dir_entry->d_name);

			err = stat(temp_str, &temp_stat);
			if (err == -1) {
				fprintf(stderr,
					"Cannot stat file (should not happen) - %s\n",
					temp_str);
				free(temp_str);
				return (NULL);
			}

			free(temp_str);

			if ((S_ISDIR(temp_stat.st_mode)
			     && (external_info->requested_file_type ==
				 REQUEST_DIR_ONLY))
			    || ((!S_ISDIR(temp_stat.st_mode)
				 && external_info->requested_file_type ==
				 REQUEST_FILE_ONLY))) {

				/* copy to a temp buffer because dir_entry struct is on the stack */

				strcpy(external_info->temp_buffer,
				       dir_entry->d_name);
				return (external_info->temp_buffer);
			}
		}
	}

	return (NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_close_directory
 *
 * PARAMETERS:  dir_handle          - Created via acpi_os_open_directory
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Close the open directory and cleanup.
 *
 ******************************************************************************/

void acpi_os_close_directory(void *dir_handle)
{
	struct external_find_info *external_info = dir_handle;

	/* Close the directory and free allocations */

	closedir(external_info->dir_ptr);
	free(dir_handle);
}
