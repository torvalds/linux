/******************************************************************************
 *
 * Name: acnames.h - Global names and strings
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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

#ifndef __ACNAMES_H__
#define __ACNAMES_H__

/* Method names - these methods can appear anywhere in the namespace */

#define METHOD_NAME__ADR        "_ADR"
#define METHOD_NAME__AEI        "_AEI"
#define METHOD_NAME__BBN        "_BBN"
#define METHOD_NAME__CBA        "_CBA"
#define METHOD_NAME__CID        "_CID"
#define METHOD_NAME__CLS        "_CLS"
#define METHOD_NAME__CRS        "_CRS"
#define METHOD_NAME__DDN        "_DDN"
#define METHOD_NAME__DMA        "_DMA"
#define METHOD_NAME__HID        "_HID"
#define METHOD_NAME__INI        "_INI"
#define METHOD_NAME__PLD        "_PLD"
#define METHOD_NAME__DSD        "_DSD"
#define METHOD_NAME__PRS        "_PRS"
#define METHOD_NAME__PRT        "_PRT"
#define METHOD_NAME__PRW        "_PRW"
#define METHOD_NAME__PS0        "_PS0"
#define METHOD_NAME__PS1        "_PS1"
#define METHOD_NAME__PS2        "_PS2"
#define METHOD_NAME__PS3        "_PS3"
#define METHOD_NAME__REG        "_REG"
#define METHOD_NAME__SB_        "_SB_"
#define METHOD_NAME__SEG        "_SEG"
#define METHOD_NAME__SRS        "_SRS"
#define METHOD_NAME__STA        "_STA"
#define METHOD_NAME__SUB        "_SUB"
#define METHOD_NAME__UID        "_UID"

/* Method names - these methods must appear at the namespace root */

#define METHOD_PATHNAME__PTS    "\\_PTS"
#define METHOD_PATHNAME__SST    "\\_SI._SST"
#define METHOD_PATHNAME__WAK    "\\_WAK"

/* Definitions of the predefined namespace names  */

#define ACPI_UNKNOWN_NAME       (u32) 0x3F3F3F3F	/* Unknown name is "????" */
#define ACPI_ROOT_NAME          (u32) 0x5F5F5F5C	/* Root name is    "\___" */

#define ACPI_PREFIX_MIXED       (u32) 0x69706341	/* "Acpi" */
#define ACPI_PREFIX_LOWER       (u32) 0x69706361	/* "acpi" */

#define ACPI_NS_ROOT_PATH       "\\"

#endif				/* __ACNAMES_H__  */
