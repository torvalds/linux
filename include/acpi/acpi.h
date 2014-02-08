/******************************************************************************
 *
 * Name: acpi.h - Master public include file used to interface to ACPICA
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2014, Intel Corp.
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

#ifndef __ACPI_H__
#define __ACPI_H__

/*
 * Public include files for use by code that will interface to ACPICA.
 *
 * Information includes the ACPICA data types, names, exceptions, and
 * external interface prototypes. Also included are the definitions for
 * all ACPI tables (FADT, MADT, etc.)
 *
 * Note: The order of these include files is important.
 */
#include <acpi/platform/acenv.h>	/* Environment-specific items */
#include <acpi/acnames.h>		/* Common ACPI names and strings */
#include <acpi/actypes.h>		/* ACPICA data types and structures */
#include <acpi/acexcep.h>		/* ACPICA exceptions */
#include <acpi/actbl.h>		/* ACPI table definitions */
#include <acpi/acoutput.h>		/* Error output and Debug macros */
#include <acpi/acrestyp.h>		/* Resource Descriptor structs */
#include <acpi/acpiosxf.h>		/* OSL interfaces (ACPICA-to-OS) */
#include <acpi/acpixf.h>		/* ACPI core subsystem external interfaces */
#ifdef ACPI_NATIVE_INTERFACE_HEADER
#include ACPI_NATIVE_INTERFACE_HEADER
#endif

#endif				/* __ACPI_H__ */
