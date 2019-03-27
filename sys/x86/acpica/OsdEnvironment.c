/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000,2001 Michael Smith
 * Copyright (c) 2000 BSDi
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/aclocal.h>
#include <contrib/dev/acpica/include/actables.h>

static u_long acpi_root_phys;

SYSCTL_ULONG(_machdep, OID_AUTO, acpi_root, CTLFLAG_RD, &acpi_root_phys, 0,
    "The physical address of the RSDP");

ACPI_STATUS
AcpiOsInitialize(void)
{

	return (AE_OK);
}

ACPI_STATUS
AcpiOsTerminate(void)
{

	return (AE_OK);
}

static u_long
acpi_get_root_from_loader(void)
{
	long acpi_root;

	if (TUNABLE_ULONG_FETCH("acpi.rsdp", &acpi_root))
		return (acpi_root);

	/*
	 * The hints mechanism is unreliable (it fails if anybody ever
	 * compiled in hints to the kernel). It has been replaced
	 * by the tunable method, but is used here as a fallback to
	 * retain maximum compatibility between old loaders and new
	 * kernels. It can be removed after 11.0R.
	 */
	if (resource_long_value("acpi", 0, "rsdp", &acpi_root) == 0)
		return (acpi_root);

	return (0);
}

static u_long
acpi_get_root_from_memory(void)
{
	ACPI_PHYSICAL_ADDRESS acpi_root;

	if (ACPI_SUCCESS(AcpiFindRootPointer(&acpi_root)))
		return (acpi_root);

	return (0);
}

ACPI_PHYSICAL_ADDRESS
AcpiOsGetRootPointer(void)
{

	if (acpi_root_phys == 0) {
		acpi_root_phys = acpi_get_root_from_loader();
		if (acpi_root_phys == 0)
			acpi_root_phys = acpi_get_root_from_memory();
	}

	return (acpi_root_phys);
}
