/******************************************************************************
 *
 * Name: acpi.h - Master include file, Publics and external data.
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
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
 * Common includes for all ACPI driver files
 * We put them here because we don't want to duplicate them
 * in the rest of the source code again and again.
 */
#include "acnames.h"		/* Global ACPI names and strings */
#include "acconfig.h"		/* Configuration constants */
#include "platform/acenv.h"	/* Target environment specific items */
#include "actypes.h"		/* Fundamental common data types */
#include "acexcep.h"		/* ACPI exception codes */
#include "acmacros.h"		/* C macros */
#include "actbl.h"		/* ACPI table definitions */
#include "aclocal.h"		/* Internal data types */
#include "acoutput.h"		/* Error output and Debug macros */
#include "acpiosxf.h"		/* Interfaces to the ACPI-to-OS layer */
#include "acpixf.h"		/* ACPI core subsystem external interfaces */
#include "acobject.h"		/* ACPI internal object */
#include "acstruct.h"		/* Common structures */
#include "acglobal.h"		/* All global variables */
#include "achware.h"		/* Hardware defines and interfaces */
#include "acutils.h"		/* Utility interfaces */

#endif				/* __ACPI_H__ */
