// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: osunixmap - Unix OSL for file mappings
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#include "acpidump.h"
#include <unistd.h>
#include <sys/mman.h>
#ifdef _free_BSD
#include <sys/param.h>
#endif

#define _COMPONENT          ACPI_OS_SERVICES
ACPI_MODULE_NAME("osunixmap")

#ifndef O_BINARY
#define O_BINARY 0
#endif
#if defined(_dragon_fly) || defined(_free_BSD) || defined(_QNX)
#define MMAP_FLAGS          MAP_SHARED
#else
#define MMAP_FLAGS          MAP_PRIVATE
#endif
#define SYSTEM_MEMORY       "/dev/mem"
/*******************************************************************************
 *
 * FUNCTION:    acpi_os_get_page_size
 *
 * PARAMETERS:  None
 *
 * RETURN:      Page size of the platform.
 *
 * DESCRIPTION: Obtain page size of the platform.
 *
 ******************************************************************************/
static acpi_size acpi_os_get_page_size(void)
{

#ifdef PAGE_SIZE
	return PAGE_SIZE;
#else
	return sysconf(_SC_PAGESIZE);
#endif
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_map_memory
 *
 * PARAMETERS:  where               - Physical address of memory to be mapped
 *              length              - How much memory to map
 *
 * RETURN:      Pointer to mapped memory. Null on error.
 *
 * DESCRIPTION: Map physical memory into local address space.
 *
 *****************************************************************************/

void *acpi_os_map_memory(acpi_physical_address where, acpi_size length)
{
	u8 *mapped_memory;
	acpi_physical_address offset;
	acpi_size page_size;
	int fd;

	fd = open(SYSTEM_MEMORY, O_RDONLY | O_BINARY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s\n", SYSTEM_MEMORY);
		return (NULL);
	}

	/* Align the offset to use mmap */

	page_size = acpi_os_get_page_size();
	offset = where % page_size;

	/* Map the table header to get the length of the full table */

	mapped_memory = mmap(NULL, (length + offset), PROT_READ, MMAP_FLAGS,
			     fd, (where - offset));
	if (mapped_memory == MAP_FAILED) {
		fprintf(stderr, "Cannot map %s\n", SYSTEM_MEMORY);
		close(fd);
		return (NULL);
	}

	close(fd);
	return (ACPI_CAST8(mapped_memory + offset));
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_unmap_memory
 *
 * PARAMETERS:  where               - Logical address of memory to be unmapped
 *              length              - How much memory to unmap
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete a previously created mapping. Where and Length must
 *              correspond to a previous mapping exactly.
 *
 *****************************************************************************/

void acpi_os_unmap_memory(void *where, acpi_size length)
{
	acpi_physical_address offset;
	acpi_size page_size;

	page_size = acpi_os_get_page_size();
	offset = ACPI_TO_INTEGER(where) % page_size;
	munmap((u8 *)where - offset, (length + offset));
}
