// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: osunixdir - Unix directory access interfaces
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

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
