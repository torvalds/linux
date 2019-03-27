/*-
 * Copyright (c) 2002 Mitsaru Iwasaki
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * ACPI Table interfaces
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/linker.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/aclocal.h>
#include <contrib/dev/acpica/include/actables.h>

#undef _COMPONENT
#define	_COMPONENT      ACPI_TABLES

static char acpi_osname[128];
TUNABLE_STR("hw.acpi.osname", acpi_osname, sizeof(acpi_osname));

ACPI_STATUS
AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *InitVal,
    ACPI_STRING *NewVal)
{

	if (InitVal == NULL || NewVal == NULL)
		return (AE_BAD_PARAMETER);

	*NewVal = NULL;
	if (ACPI_COMPARE_NAME(InitVal->Name, "_OS_") &&
	    InitVal->Type == ACPI_TYPE_STRING && strlen(acpi_osname) > 0) {
		printf("ACPI: Overriding _OS definition with \"%s\"\n",
		    acpi_osname);
		*NewVal = acpi_osname;
	}
	return (AE_OK);
}

ACPI_STATUS
AcpiOsTableOverride(ACPI_TABLE_HEADER *ExistingTable,
    ACPI_TABLE_HEADER **NewTable)
{
	char modname[] = "acpi_dsdt";
	caddr_t acpi_table;
	ACPI_TABLE_HEADER *hdr;
	size_t sz;

	if (ExistingTable == NULL || NewTable == NULL)
		return (AE_BAD_PARAMETER);

	*NewTable = NULL;
	if (!ACPI_COMPARE_NAME(ExistingTable->Signature, ACPI_SIG_DSDT))
#ifdef notyet
		for (int i = 0; i < ACPI_NAME_SIZE; i++)
			modname[i + 5] = tolower(ExistingTable->Signature[i]);
#else
		return (AE_SUPPORT);
#endif
	acpi_table = preload_search_by_type(modname);
	if (acpi_table == NULL)
		return (AE_NOT_FOUND);
	hdr = preload_fetch_addr(acpi_table);
	sz = preload_fetch_size(acpi_table);
	if (hdr == NULL || sz == 0)
		return (AE_ERROR);
#ifndef notyet
	/* Assume SSDT is installed with DSDT. */
	AcpiGbl_DisableSsdtTableInstall = TRUE;
#endif
	*NewTable = hdr;
	return (AE_OK);
}

ACPI_STATUS
AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *ExistingTable,
    ACPI_PHYSICAL_ADDRESS *NewAddress, UINT32 *NewTableLength)
{

	return (AE_SUPPORT);
}
