/******************************************************************************
 *
 * Module Name: dmtbinfo2 - Table info for non-AML tables
 *
 *****************************************************************************/

/******************************************************************************
 *
 * 1. Copyright Notice
 *
 * Some or all of this work - Copyright (c) 1999 - 2019, Intel Corp.
 * All rights reserved.
 *
 * 2. License
 *
 * 2.1. This is your license from Intel Corp. under its intellectual property
 * rights. You may have additional license terms from the party that provided
 * you this software, covering your right to use that party's intellectual
 * property rights.
 *
 * 2.2. Intel grants, free of charge, to any person ("Licensee") obtaining a
 * copy of the source code appearing in this file ("Covered Code") an
 * irrevocable, perpetual, worldwide license under Intel's copyrights in the
 * base code distributed originally by Intel ("Original Intel Code") to copy,
 * make derivatives, distribute, use and display any portion of the Covered
 * Code in any form, with the right to sublicense such rights; and
 *
 * 2.3. Intel grants Licensee a non-exclusive and non-transferable patent
 * license (with the right to sublicense), under only those claims of Intel
 * patents that are infringed by the Original Intel Code, to make, use, sell,
 * offer to sell, and import the Covered Code and derivative works thereof
 * solely to the minimum extent necessary to exercise the above copyright
 * license, and in no event shall the patent license extend to any additions
 * to or modifications of the Original Intel Code. No other license or right
 * is granted directly or by implication, estoppel or otherwise;
 *
 * The above copyright and patent license is granted only if the following
 * conditions are met:
 *
 * 3. Conditions
 *
 * 3.1. Redistribution of Source with Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification with rights to further distribute source must include
 * the above Copyright Notice, the above License, this list of Conditions,
 * and the following Disclaimer and Export Compliance provision. In addition,
 * Licensee must cause all Covered Code to which Licensee contributes to
 * contain a file documenting the changes Licensee made to create that Covered
 * Code and the date of any change. Licensee must include in that file the
 * documentation of any changes made by any predecessor Licensee. Licensee
 * must include a prominent statement that the modification is derived,
 * directly or indirectly, from Original Intel Code.
 *
 * 3.2. Redistribution of Source with no Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification without rights to further distribute source must
 * include the following Disclaimer and Export Compliance provision in the
 * documentation and/or other materials provided with distribution. In
 * addition, Licensee may not authorize further sublicense of source of any
 * portion of the Covered Code, and must include terms to the effect that the
 * license from Licensee to its licensee is limited to the intellectual
 * property embodied in the software Licensee provides to its licensee, and
 * not to intellectual property embodied in modifications its licensee may
 * make.
 *
 * 3.3. Redistribution of Executable. Redistribution in executable form of any
 * substantial portion of the Covered Code or modification must reproduce the
 * above Copyright Notice, and the following Disclaimer and Export Compliance
 * provision in the documentation and/or other materials provided with the
 * distribution.
 *
 * 3.4. Intel retains all right, title, and interest in and to the Original
 * Intel Code.
 *
 * 3.5. Neither the name Intel nor any other trademark owned or controlled by
 * Intel shall be used in advertising or otherwise to promote the sale, use or
 * other dealings in products derived from or relating to the Covered Code
 * without prior written authorization from Intel.
 *
 * 4. Disclaimer and Export Compliance
 *
 * 4.1. INTEL MAKES NO WARRANTY OF ANY KIND REGARDING ANY SOFTWARE PROVIDED
 * HERE. ANY SOFTWARE ORIGINATING FROM INTEL OR DERIVED FROM INTEL SOFTWARE
 * IS PROVIDED "AS IS," AND INTEL WILL NOT PROVIDE ANY SUPPORT, ASSISTANCE,
 * INSTALLATION, TRAINING OR OTHER SERVICES. INTEL WILL NOT PROVIDE ANY
 * UPDATES, ENHANCEMENTS OR EXTENSIONS. INTEL SPECIFICALLY DISCLAIMS ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * 4.2. IN NO EVENT SHALL INTEL HAVE ANY LIABILITY TO LICENSEE, ITS LICENSEES
 * OR ANY OTHER THIRD PARTY, FOR ANY LOST PROFITS, LOST DATA, LOSS OF USE OR
 * COSTS OF PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, OR FOR ANY INDIRECT,
 * SPECIAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THIS AGREEMENT, UNDER ANY
 * CAUSE OF ACTION OR THEORY OF LIABILITY, AND IRRESPECTIVE OF WHETHER INTEL
 * HAS ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES. THESE LIMITATIONS
 * SHALL APPLY NOTWITHSTANDING THE FAILURE OF THE ESSENTIAL PURPOSE OF ANY
 * LIMITED REMEDY.
 *
 * 4.3. Licensee shall not export, either directly or indirectly, any of this
 * software or system incorporating such software without first obtaining any
 * required license or other approval from the U. S. Department of Commerce or
 * any other agency or department of the United States Government. In the
 * event Licensee exports any such software from the United States or
 * re-exports any such software from a foreign destination, Licensee shall
 * ensure that the distribution and export/re-export of the software is in
 * compliance with all laws, regulations, orders, or other restrictions of the
 * U.S. Export Administration Regulations. Licensee agrees that neither it nor
 * any of its subsidiaries will export/re-export any technical data, process,
 * software, or service, directly or indirectly, to any country for which the
 * United States government or any agency thereof requires an export license,
 * other governmental approval, or letter of assurance, without first obtaining
 * such license, approval or letter.
 *
 *****************************************************************************
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * following license:
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 *****************************************************************************/

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acdisasm.h>
#include <contrib/dev/acpica/include/actbinfo.h>

/* This module used for application-level code only */

#define _COMPONENT          ACPI_CA_DISASSEMBLER
        ACPI_MODULE_NAME    ("dmtbinfo2")

/*
 * How to add a new table:
 *
 * - Add the C table definition to the actbl1.h or actbl2.h header.
 * - Add ACPI_xxxx_OFFSET macro(s) for the table (and subtables) to list below.
 * - Define the table in this file (for the disassembler). If any
 *   new data types are required (ACPI_DMT_*), see below.
 * - Add an external declaration for the new table definition (AcpiDmTableInfo*)
 *     in acdisam.h
 * - Add new table definition to the dispatch table in dmtable.c (AcpiDmTableData)
 *     If a simple table (with no subtables), no disassembly code is needed.
 *     Otherwise, create the AcpiDmDump* function for to disassemble the table
 *     and add it to the dmtbdump.c file.
 * - Add an external declaration for the new AcpiDmDump* function in acdisasm.h
 * - Add the new AcpiDmDump* function to the dispatch table in dmtable.c
 * - Create a template for the new table
 * - Add data table compiler support
 *
 * How to add a new data type (ACPI_DMT_*):
 *
 * - Add new type at the end of the ACPI_DMT list in acdisasm.h
 * - Add length and implementation cases in dmtable.c  (disassembler)
 * - Add type and length cases in dtutils.c (DT compiler)
 */

/*
 * Remaining tables are not consumed directly by the ACPICA subsystem
 */


/*******************************************************************************
 *
 * IORT - IO Remapping Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort[] =
{
    {ACPI_DMT_UINT32,   ACPI_IORT_OFFSET (NodeCount),               "Node Count", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT_OFFSET (NodeOffset),              "Node Offset", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT_OFFSET (Reserved),                "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* Optional padding field */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIortPad[] =
{
    {ACPI_DMT_RAW_BUFFER, 0,                                        "Optional Padding", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

/* Common Subtable header (one per Subtable) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIortHdr[] =
{
    {ACPI_DMT_UINT8,    ACPI_IORTH_OFFSET (Type),                   "Type", 0},
    {ACPI_DMT_UINT16,   ACPI_IORTH_OFFSET (Length),                 "Length", DT_LENGTH},
    {ACPI_DMT_UINT8,    ACPI_IORTH_OFFSET (Revision),               "Revision", 0},
    {ACPI_DMT_UINT32,   ACPI_IORTH_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_IORTH_OFFSET (MappingCount),           "Mapping Count", 0},
    {ACPI_DMT_UINT32,   ACPI_IORTH_OFFSET (MappingOffset),          "Mapping Offset", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoIortMap[] =
{
    {ACPI_DMT_UINT32,   ACPI_IORTM_OFFSET (InputBase),              "Input base", DT_OPTIONAL},
    {ACPI_DMT_UINT32,   ACPI_IORTM_OFFSET (IdCount),                "ID Count", 0},
    {ACPI_DMT_UINT32,   ACPI_IORTM_OFFSET (OutputBase),             "Output Base", 0},
    {ACPI_DMT_UINT32,   ACPI_IORTM_OFFSET (OutputReference),        "Output Reference", 0},
    {ACPI_DMT_UINT32,   ACPI_IORTM_OFFSET (Flags),                  "Flags (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_IORTM_FLAG_OFFSET (Flags, 0),          "Single Mapping", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoIortAcc[] =
{
    {ACPI_DMT_UINT32,   ACPI_IORTA_OFFSET (CacheCoherency),         "Cache Coherency", 0},
    {ACPI_DMT_UINT8,    ACPI_IORTA_OFFSET (Hints),                  "Hints (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_IORTA_FLAG_OFFSET (Hints, 0),          "Transient", 0},
    {ACPI_DMT_FLAG1,    ACPI_IORTA_FLAG_OFFSET (Hints, 0),          "Write Allocate", 0},
    {ACPI_DMT_FLAG2,    ACPI_IORTA_FLAG_OFFSET (Hints, 0),          "Read Allocate", 0},
    {ACPI_DMT_FLAG3,    ACPI_IORTA_FLAG_OFFSET (Hints, 0),          "Override", 0},
    {ACPI_DMT_UINT16,   ACPI_IORTA_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT8,    ACPI_IORTA_OFFSET (MemoryFlags),            "Memory Flags (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_IORTA_FLAG_OFFSET (MemoryFlags, 0),    "Coherency", 0},
    {ACPI_DMT_FLAG1,    ACPI_IORTA_FLAG_OFFSET (MemoryFlags, 0),    "Device Attribute", 0},
    ACPI_DMT_TERMINATOR
};

/* IORT subtables */

/* 0x00: ITS Group */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort0[] =
{
    {ACPI_DMT_UINT32,   ACPI_IORT0_OFFSET (ItsCount),               "ItsCount", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort0a[] =
{
    {ACPI_DMT_UINT32,   0,                                          "Identifiers", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

/* 0x01: Named Component */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort1[] =
{
    {ACPI_DMT_UINT32,   ACPI_IORT1_OFFSET (NodeFlags),              "Node Flags", 0},
    {ACPI_DMT_IORTMEM,  ACPI_IORT1_OFFSET (MemoryProperties),       "Memory Properties", 0},
    {ACPI_DMT_UINT8,    ACPI_IORT1_OFFSET (MemoryAddressLimit),     "Memory Size Limit", 0},
    {ACPI_DMT_STRING,   ACPI_IORT1_OFFSET (DeviceName[0]),          "Device Name", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort1a[] =
{
    {ACPI_DMT_RAW_BUFFER, 0,                                        "Padding", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

/* 0x02: PCI Root Complex */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort2[] =
{
    {ACPI_DMT_IORTMEM,  ACPI_IORT2_OFFSET (MemoryProperties),       "Memory Properties", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT2_OFFSET (AtsAttribute),           "ATS Attribute", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT2_OFFSET (PciSegmentNumber),       "PCI Segment Number", 0},
    {ACPI_DMT_UINT8,    ACPI_IORT2_OFFSET (MemoryAddressLimit),     "Memory Size Limit", 0},
    {ACPI_DMT_UINT24,   ACPI_IORT2_OFFSET (Reserved[0]),            "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* 0x03: SMMUv1/2 */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort3[] =
{
    {ACPI_DMT_UINT64,   ACPI_IORT3_OFFSET (BaseAddress),            "Base Address", 0},
    {ACPI_DMT_UINT64,   ACPI_IORT3_OFFSET (Span),                   "Span", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3_OFFSET (Model),                  "Model", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3_OFFSET (Flags),                  "Flags (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_IORT3_FLAG_OFFSET (Flags, 0),          "DVM Supported", 0},
    {ACPI_DMT_FLAG1,    ACPI_IORT3_FLAG_OFFSET (Flags, 0),          "Coherent Walk", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3_OFFSET (GlobalInterruptOffset),  "Global Interrupt Offset", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3_OFFSET (ContextInterruptCount),  "Context Interrupt Count", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3_OFFSET (ContextInterruptOffset), "Context Interrupt Offset", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3_OFFSET (PmuInterruptCount),      "PMU Interrupt Count", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3_OFFSET (PmuInterruptOffset),     "PMU Interrupt Offset", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort3a[] =
{
    {ACPI_DMT_UINT32,   ACPI_IORT3A_OFFSET (NSgIrpt),                   "NSgIrpt", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3A_OFFSET (NSgIrptFlags),              "NSgIrpt Flags (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_IORT3a_FLAG_OFFSET (NSgIrptFlags, 0),      "Edge Triggered", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3A_OFFSET (NSgCfgIrpt),                "NSgCfgIrpt", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT3A_OFFSET (NSgCfgIrptFlags),           "NSgCfgIrpt Flags (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_IORT3a_FLAG_OFFSET (NSgCfgIrptFlags, 0),   "Edge Triggered", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort3b[] =
{
    {ACPI_DMT_UINT64,   0,                                          "Context Interrupt", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort3c[] =
{
    {ACPI_DMT_UINT64,   0,                                          "PMU Interrupt", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

/* 0x04: SMMUv3 */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort4[] =
{
    {ACPI_DMT_UINT64,   ACPI_IORT4_OFFSET (BaseAddress),            "Base Address", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT4_OFFSET (Flags),                  "Flags (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_IORT4_FLAG_OFFSET (Flags, 0),          "COHACC Override", 0},
    {ACPI_DMT_FLAG1,    ACPI_IORT4_FLAG_OFFSET (Flags, 0),          "HTTU Override", 0},
    {ACPI_DMT_FLAG3,    ACPI_IORT4_FLAG_OFFSET (Flags, 0),          "Proximity Domain Valid", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT4_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_IORT4_OFFSET (VatosAddress),           "VATOS Address", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT4_OFFSET (Model),                  "Model", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT4_OFFSET (EventGsiv),              "Event GSIV", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT4_OFFSET (PriGsiv),                "PRI GSIV", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT4_OFFSET (GerrGsiv),               "GERR GSIV", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT4_OFFSET (SyncGsiv),               "Sync GSIV", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT4_OFFSET (Pxm),                    "Proximity Domain", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT4_OFFSET (IdMappingIndex),         "Device ID Mapping Index", 0},
    ACPI_DMT_TERMINATOR
};

/* 0x05: PMCG */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIort5[] =
{
    {ACPI_DMT_UINT64,   ACPI_IORT5_OFFSET (Page0BaseAddress),       "Page 0 Base Address", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT5_OFFSET (OverflowGsiv),           "Overflow Interrupt GSIV", 0},
    {ACPI_DMT_UINT32,   ACPI_IORT5_OFFSET (NodeReference),          "Node Reference", 0},
    {ACPI_DMT_UINT64,   ACPI_IORT5_OFFSET (Page1BaseAddress),       "Page 1 Base Address", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * IVRS - I/O Virtualization Reporting Structure
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoIvrs[] =
{
    {ACPI_DMT_UINT32,   ACPI_IVRS_OFFSET (Info),                    "Virtualization Info", 0},
    {ACPI_DMT_UINT64,   ACPI_IVRS_OFFSET (Reserved),                "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* Common Subtable header (one per Subtable) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIvrsHdr[] =
{
    {ACPI_DMT_IVRS,     ACPI_IVRSH_OFFSET (Type),                   "Subtable Type", 0},
    {ACPI_DMT_UINT8,    ACPI_IVRSH_OFFSET (Flags),                  "Flags", 0},
    {ACPI_DMT_UINT16,   ACPI_IVRSH_OFFSET (Length),                 "Length", DT_LENGTH},
    {ACPI_DMT_UINT16,   ACPI_IVRSH_OFFSET (DeviceId),               "DeviceId", 0},
    ACPI_DMT_TERMINATOR
};

/* IVRS subtables */

/* 0x10: I/O Virtualization Hardware Definition (IVHD) Block */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIvrs0[] =
{
    {ACPI_DMT_UINT16,   ACPI_IVRS0_OFFSET (CapabilityOffset),       "Capability Offset", 0},
    {ACPI_DMT_UINT64,   ACPI_IVRS0_OFFSET (BaseAddress),            "Base Address", 0},
    {ACPI_DMT_UINT16,   ACPI_IVRS0_OFFSET (PciSegmentGroup),        "PCI Segment Group", 0},
    {ACPI_DMT_UINT16,   ACPI_IVRS0_OFFSET (Info),                   "Virtualization Info", 0},
    {ACPI_DMT_UINT32,   ACPI_IVRS0_OFFSET (Reserved),               "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* 0x20, 0x21, 0x22: I/O Virtualization Memory Definition (IVMD) Block */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIvrs1[] =
{
    {ACPI_DMT_UINT16,   ACPI_IVRS1_OFFSET (AuxData),                "Auxiliary Data", 0},
    {ACPI_DMT_UINT64,   ACPI_IVRS1_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_IVRS1_OFFSET (StartAddress),           "Start Address", 0},
    {ACPI_DMT_UINT64,   ACPI_IVRS1_OFFSET (MemoryLength),           "Memory Length", 0},
    ACPI_DMT_TERMINATOR
};

/* Device entry header for IVHD block */

#define ACPI_DMT_IVRS_DE_HEADER \
    {ACPI_DMT_UINT8,    ACPI_IVRSD_OFFSET (Type),                   "Entry Type", 0}, \
    {ACPI_DMT_UINT16,   ACPI_IVRSD_OFFSET (Id),                     "Device ID", 0}, \
    {ACPI_DMT_UINT8,    ACPI_IVRSD_OFFSET (DataSetting),            "Data Setting", 0}

/* 4-byte device entry */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIvrs4[] =
{
    ACPI_DMT_IVRS_DE_HEADER,
    {ACPI_DMT_EXIT,     0,                                          NULL, 0},
};

/* 8-byte device entry */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIvrs8a[] =
{
    ACPI_DMT_IVRS_DE_HEADER,
    {ACPI_DMT_UINT8,    ACPI_IVRS8A_OFFSET (Reserved1),             "Reserved", 0},
    {ACPI_DMT_UINT16,   ACPI_IVRS8A_OFFSET (UsedId),                "Source Used Device ID", 0},
    {ACPI_DMT_UINT8,    ACPI_IVRS8A_OFFSET (Reserved2),             "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* 8-byte device entry */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIvrs8b[] =
{
    ACPI_DMT_IVRS_DE_HEADER,
    {ACPI_DMT_UINT32,   ACPI_IVRS8B_OFFSET (ExtendedData),          "Extended Data", 0},
    ACPI_DMT_TERMINATOR
};

/* 8-byte device entry */

ACPI_DMTABLE_INFO           AcpiDmTableInfoIvrs8c[] =
{
    ACPI_DMT_IVRS_DE_HEADER,
    {ACPI_DMT_UINT8,    ACPI_IVRS8C_OFFSET (Handle),                "Handle", 0},
    {ACPI_DMT_UINT16,   ACPI_IVRS8C_OFFSET (UsedId),                "Source Used Device ID", 0},
    {ACPI_DMT_UINT8,    ACPI_IVRS8C_OFFSET (Variety),               "Variety", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * LPIT - Low Power Idle Table
 *
 ******************************************************************************/

/* Main table consists only of the standard ACPI table header */

/* Common Subtable header (one per Subtable) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoLpitHdr[] =
{
    {ACPI_DMT_LPIT,     ACPI_LPITH_OFFSET (Type),                   "Subtable Type", 0},
    {ACPI_DMT_UINT32,   ACPI_LPITH_OFFSET (Length),                 "Length", DT_LENGTH},
    {ACPI_DMT_UINT16,   ACPI_LPITH_OFFSET (UniqueId),               "Unique ID", 0},
    {ACPI_DMT_UINT16,   ACPI_LPITH_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_LPITH_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_LPITH_FLAG_OFFSET (Flags, 0),          "State Disabled", 0},
    {ACPI_DMT_FLAG1,    ACPI_LPITH_FLAG_OFFSET (Flags, 0),          "No Counter", 0},
    ACPI_DMT_TERMINATOR
};

/* LPIT Subtables */

/* 0: Native C-state */

ACPI_DMTABLE_INFO           AcpiDmTableInfoLpit0[] =
{
    {ACPI_DMT_GAS,      ACPI_LPIT0_OFFSET (EntryTrigger),           "Entry Trigger", 0},
    {ACPI_DMT_UINT32,   ACPI_LPIT0_OFFSET (Residency),              "Residency", 0},
    {ACPI_DMT_UINT32,   ACPI_LPIT0_OFFSET (Latency),                "Latency", 0},
    {ACPI_DMT_GAS,      ACPI_LPIT0_OFFSET (ResidencyCounter),       "Residency Counter", 0},
    {ACPI_DMT_UINT64,   ACPI_LPIT0_OFFSET (CounterFrequency),       "Counter Frequency", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * MADT - Multiple APIC Description Table and subtables
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt[] =
{
    {ACPI_DMT_UINT32,   ACPI_MADT_OFFSET (Address),                 "Local Apic Address", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT_OFFSET (Flags),                   "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_MADT_FLAG_OFFSET (Flags,0),            "PC-AT Compatibility", 0},
    ACPI_DMT_TERMINATOR
};

/* Common Subtable header (one per Subtable) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadtHdr[] =
{
    {ACPI_DMT_MADT,     ACPI_MADTH_OFFSET (Type),                   "Subtable Type", 0},
    {ACPI_DMT_UINT8,    ACPI_MADTH_OFFSET (Length),                 "Length", DT_LENGTH},
    ACPI_DMT_TERMINATOR
};

/* MADT Subtables */

/* 0: processor APIC */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt0[] =
{
    {ACPI_DMT_UINT8,    ACPI_MADT0_OFFSET (ProcessorId),            "Processor ID", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT0_OFFSET (Id),                     "Local Apic ID", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT0_OFFSET (LapicFlags),             "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_MADT0_FLAG_OFFSET (LapicFlags,0),      "Processor Enabled", 0},
    {ACPI_DMT_FLAG1,    ACPI_MADT0_FLAG_OFFSET (LapicFlags,0),      "Runtime Online Capable", 0},
    ACPI_DMT_TERMINATOR
};

/* 1: IO APIC */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt1[] =
{
    {ACPI_DMT_UINT8,    ACPI_MADT1_OFFSET (Id),                     "I/O Apic ID", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT1_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT1_OFFSET (Address),                "Address", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT1_OFFSET (GlobalIrqBase),          "Interrupt", 0},
    ACPI_DMT_TERMINATOR
};

/* 2: Interrupt Override */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt2[] =
{
    {ACPI_DMT_UINT8,    ACPI_MADT2_OFFSET (Bus),                    "Bus", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT2_OFFSET (SourceIrq),              "Source", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT2_OFFSET (GlobalIrq),              "Interrupt", 0},
    {ACPI_DMT_UINT16,   ACPI_MADT2_OFFSET (IntiFlags),              "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAGS0,   ACPI_MADT2_FLAG_OFFSET (IntiFlags,0),       "Polarity", 0},
    {ACPI_DMT_FLAGS2,   ACPI_MADT2_FLAG_OFFSET (IntiFlags,0),       "Trigger Mode", 0},
    ACPI_DMT_TERMINATOR
};

/* 3: NMI Sources */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt3[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT3_OFFSET (IntiFlags),              "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAGS0,   ACPI_MADT3_FLAG_OFFSET (IntiFlags,0),       "Polarity", 0},
    {ACPI_DMT_FLAGS2,   ACPI_MADT3_FLAG_OFFSET (IntiFlags,0),       "Trigger Mode", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT3_OFFSET (GlobalIrq),              "Interrupt", 0},
    ACPI_DMT_TERMINATOR
};

/* 4: Local APIC NMI */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt4[] =
{
    {ACPI_DMT_UINT8,    ACPI_MADT4_OFFSET (ProcessorId),            "Processor ID", 0},
    {ACPI_DMT_UINT16,   ACPI_MADT4_OFFSET (IntiFlags),              "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAGS0,   ACPI_MADT4_FLAG_OFFSET (IntiFlags,0),       "Polarity", 0},
    {ACPI_DMT_FLAGS2,   ACPI_MADT4_FLAG_OFFSET (IntiFlags,0),       "Trigger Mode", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT4_OFFSET (Lint),                   "Interrupt Input LINT", 0},
    ACPI_DMT_TERMINATOR
};

/* 5: Address Override */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt5[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT5_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT5_OFFSET (Address),                "APIC Address", 0},
    ACPI_DMT_TERMINATOR
};

/* 6: I/O Sapic */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt6[] =
{
    {ACPI_DMT_UINT8,    ACPI_MADT6_OFFSET (Id),                     "I/O Sapic ID", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT6_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT6_OFFSET (GlobalIrqBase),          "Interrupt Base", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT6_OFFSET (Address),                "Address", 0},
    ACPI_DMT_TERMINATOR
};

/* 7: Local Sapic */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt7[] =
{
    {ACPI_DMT_UINT8,    ACPI_MADT7_OFFSET (ProcessorId),            "Processor ID", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT7_OFFSET (Id),                     "Local Sapic ID", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT7_OFFSET (Eid),                    "Local Sapic EID", 0},
    {ACPI_DMT_UINT24,   ACPI_MADT7_OFFSET (Reserved[0]),            "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT7_OFFSET (LapicFlags),             "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_MADT7_FLAG_OFFSET (LapicFlags,0),      "Processor Enabled", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT7_OFFSET (Uid),                    "Processor UID", 0},
    {ACPI_DMT_STRING,   ACPI_MADT7_OFFSET (UidString[0]),           "Processor UID String", 0},
    ACPI_DMT_TERMINATOR
};

/* 8: Platform Interrupt Source */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt8[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT8_OFFSET (IntiFlags),              "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAGS0,   ACPI_MADT8_FLAG_OFFSET (IntiFlags,0),       "Polarity", 0},
    {ACPI_DMT_FLAGS2,   ACPI_MADT8_FLAG_OFFSET (IntiFlags,0),       "Trigger Mode", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT8_OFFSET (Type),                   "InterruptType", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT8_OFFSET (Id),                     "Processor ID", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT8_OFFSET (Eid),                    "Processor EID", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT8_OFFSET (IoSapicVector),          "I/O Sapic Vector", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT8_OFFSET (GlobalIrq),              "Interrupt", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT8_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_MADT8_OFFSET (Flags),                  "CPEI Override", 0},
    ACPI_DMT_TERMINATOR
};

/* 9: Processor Local X2_APIC (ACPI 4.0) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt9[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT9_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT9_OFFSET (LocalApicId),            "Processor x2Apic ID", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT9_OFFSET (LapicFlags),             "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_MADT9_FLAG_OFFSET (LapicFlags,0),      "Processor Enabled", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT9_OFFSET (Uid),                    "Processor UID", 0},
    ACPI_DMT_TERMINATOR
};

/* 10: Local X2_APIC NMI (ACPI 4.0) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt10[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT10_OFFSET (IntiFlags),             "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAGS0,   ACPI_MADT10_FLAG_OFFSET (IntiFlags,0),      "Polarity", 0},
    {ACPI_DMT_FLAGS2,   ACPI_MADT10_FLAG_OFFSET (IntiFlags,0),      "Trigger Mode", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT10_OFFSET (Uid),                   "Processor UID", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT10_OFFSET (Lint),                  "Interrupt Input LINT", 0},
    {ACPI_DMT_UINT24,   ACPI_MADT10_OFFSET (Reserved[0]),           "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* 11: Generic Interrupt Controller (ACPI 5.0) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt11[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT11_OFFSET (Reserved),              "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT11_OFFSET (CpuInterfaceNumber),    "CPU Interface Number", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT11_OFFSET (Uid),                   "Processor UID", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT11_OFFSET (Flags),                 "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_MADT11_FLAG_OFFSET (Flags,0),          "Processor Enabled", 0},
    {ACPI_DMT_FLAG1,    ACPI_MADT11_FLAG_OFFSET (Flags,0),          "Performance Interrupt Trigger Mode", 0},
    {ACPI_DMT_FLAG2,    ACPI_MADT11_FLAG_OFFSET (Flags,0),          "Virtual GIC Interrupt Trigger Mode", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT11_OFFSET (ParkingVersion),        "Parking Protocol Version", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT11_OFFSET (PerformanceInterrupt),  "Performance Interrupt", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT11_OFFSET (ParkedAddress),         "Parked Address", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT11_OFFSET (BaseAddress),           "Base Address", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT11_OFFSET (GicvBaseAddress),       "Virtual GIC Base Address", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT11_OFFSET (GichBaseAddress),       "Hypervisor GIC Base Address", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT11_OFFSET (VgicInterrupt),         "Virtual GIC Interrupt", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT11_OFFSET (GicrBaseAddress),       "Redistributor Base Address", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT11_OFFSET (ArmMpidr),              "ARM MPIDR", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT11_OFFSET (EfficiencyClass),       "Efficiency Class", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT11_OFFSET (Reserved2[0]),          "Reserved", 0},
    {ACPI_DMT_UINT16,   ACPI_MADT11_OFFSET (SpeInterrupt),          "SPE Overflow Interrupt", 0},
    ACPI_DMT_TERMINATOR
};

/* 12: Generic Interrupt Distributor (ACPI 5.0) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt12[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT12_OFFSET (Reserved),              "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT12_OFFSET (GicId),                 "Local GIC Hardware ID", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT12_OFFSET (BaseAddress),           "Base Address", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT12_OFFSET (GlobalIrqBase),         "Interrupt Base", 0},
    {ACPI_DMT_UINT8,    ACPI_MADT12_OFFSET (Version),               "Version", 0},
    {ACPI_DMT_UINT24,   ACPI_MADT12_OFFSET (Reserved2[0]),          "Reserved", 0},
   ACPI_DMT_TERMINATOR
};

/* 13: Generic MSI Frame (ACPI 5.1) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt13[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT13_OFFSET (Reserved),              "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT13_OFFSET (MsiFrameId),            "MSI Frame ID", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT13_OFFSET (BaseAddress),           "Base Address", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT13_OFFSET (Flags),                 "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_MADT13_FLAG_OFFSET (Flags,0),          "Select SPI", 0},
    {ACPI_DMT_UINT16,   ACPI_MADT13_OFFSET (SpiCount),              "SPI Count", 0},
    {ACPI_DMT_UINT16,   ACPI_MADT13_OFFSET (SpiBase),               "SPI Base", 0},
   ACPI_DMT_TERMINATOR
};

/* 14: Generic Redistributor (ACPI 5.1) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt14[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT14_OFFSET (Reserved),              "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT14_OFFSET (BaseAddress),           "Base Address", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT14_OFFSET (Length),                "Length", 0},
   ACPI_DMT_TERMINATOR
};

/* 15: Generic Translator (ACPI 6.0) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMadt15[] =
{
    {ACPI_DMT_UINT16,   ACPI_MADT15_OFFSET (Reserved),              "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT15_OFFSET (TranslationId),         "Translation ID", 0},
    {ACPI_DMT_UINT64,   ACPI_MADT15_OFFSET (BaseAddress),           "Base Address", 0},
    {ACPI_DMT_UINT32,   ACPI_MADT15_OFFSET (Reserved2),             "Reserved", 0},
   ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * MCFG - PCI Memory Mapped Configuration table and Subtable
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoMcfg[] =
{
    {ACPI_DMT_UINT64,   ACPI_MCFG_OFFSET (Reserved[0]),             "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoMcfg0[] =
{
    {ACPI_DMT_UINT64,   ACPI_MCFG0_OFFSET (Address),                "Base Address", 0},
    {ACPI_DMT_UINT16,   ACPI_MCFG0_OFFSET (PciSegment),             "Segment Group Number", 0},
    {ACPI_DMT_UINT8,    ACPI_MCFG0_OFFSET (StartBusNumber),         "Start Bus Number", 0},
    {ACPI_DMT_UINT8,    ACPI_MCFG0_OFFSET (EndBusNumber),           "End Bus Number", 0},
    {ACPI_DMT_UINT32,   ACPI_MCFG0_OFFSET (Reserved),               "Reserved", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * MCHI - Management Controller Host Interface table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoMchi[] =
{
    {ACPI_DMT_UINT8,    ACPI_MCHI_OFFSET (InterfaceType),           "Interface Type", 0},
    {ACPI_DMT_UINT8,    ACPI_MCHI_OFFSET (Protocol),                "Protocol", 0},
    {ACPI_DMT_UINT64,   ACPI_MCHI_OFFSET (ProtocolData),            "Protocol Data", 0},
    {ACPI_DMT_UINT8,    ACPI_MCHI_OFFSET (InterruptType),           "Interrupt Type", 0},
    {ACPI_DMT_UINT8,    ACPI_MCHI_OFFSET (Gpe),                     "Gpe", 0},
    {ACPI_DMT_UINT8,    ACPI_MCHI_OFFSET (PciDeviceFlag),           "Pci Device Flag", 0},
    {ACPI_DMT_UINT32,   ACPI_MCHI_OFFSET (GlobalInterrupt),         "Global Interrupt", 0},
    {ACPI_DMT_GAS,      ACPI_MCHI_OFFSET (ControlRegister),         "Control Register", 0},
    {ACPI_DMT_UINT8,    ACPI_MCHI_OFFSET (PciSegment),              "Pci Segment", 0},
    {ACPI_DMT_UINT8,    ACPI_MCHI_OFFSET (PciBus),                  "Pci Bus", 0},
    {ACPI_DMT_UINT8,    ACPI_MCHI_OFFSET (PciDevice),               "Pci Device", 0},
    {ACPI_DMT_UINT8,    ACPI_MCHI_OFFSET (PciFunction),             "Pci Function", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * MPST - Memory Power State Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoMpst[] =
{
    {ACPI_DMT_UINT8,    ACPI_MPST_OFFSET (ChannelId),               "Channel ID", 0},
    {ACPI_DMT_UINT24,   ACPI_MPST_OFFSET (Reserved1[0]),            "Reserved", 0},
    {ACPI_DMT_UINT16,   ACPI_MPST_OFFSET (PowerNodeCount),          "Power Node Count", 0},
    {ACPI_DMT_UINT16,   ACPI_MPST_OFFSET (Reserved2),               "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* MPST subtables */

/* 0: Memory Power Node Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMpst0[] =
{
    {ACPI_DMT_UINT8,    ACPI_MPST0_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_MPST0_FLAG_OFFSET (Flags,0),           "Node Enabled", 0},
    {ACPI_DMT_FLAG1,    ACPI_MPST0_FLAG_OFFSET (Flags,0),           "Power Managed", 0},
    {ACPI_DMT_FLAG2,    ACPI_MPST0_FLAG_OFFSET (Flags,0),           "Hot Plug Capable", 0},

    {ACPI_DMT_UINT8,    ACPI_MPST0_OFFSET (Reserved1),              "Reserved", 0},
    {ACPI_DMT_UINT16,   ACPI_MPST0_OFFSET (NodeId),                 "Node ID", 0},
    {ACPI_DMT_UINT32,   ACPI_MPST0_OFFSET (Length),                 "Length", 0},
    {ACPI_DMT_UINT64,   ACPI_MPST0_OFFSET (RangeAddress),           "Range Address", 0},
    {ACPI_DMT_UINT64,   ACPI_MPST0_OFFSET (RangeLength),            "Range Length", 0},
    {ACPI_DMT_UINT32,   ACPI_MPST0_OFFSET (NumPowerStates),         "Num Power States", 0},
    {ACPI_DMT_UINT32,   ACPI_MPST0_OFFSET (NumPhysicalComponents),  "Num Physical Components", 0},
    ACPI_DMT_TERMINATOR
};

/* 0A: Sub-subtable - Memory Power State Structure (follows Memory Power Node above) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMpst0A[] =
{
    {ACPI_DMT_UINT8,    ACPI_MPST0A_OFFSET (PowerState),            "Power State", 0},
    {ACPI_DMT_UINT8,    ACPI_MPST0A_OFFSET (InfoIndex),             "InfoIndex", 0},
    ACPI_DMT_TERMINATOR
};

/* 0B: Sub-subtable - Physical Component ID Structure (follows Memory Power State(s) above) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMpst0B[] =
{
    {ACPI_DMT_UINT16,   ACPI_MPST0B_OFFSET (ComponentId),           "Component Id", 0},
    ACPI_DMT_TERMINATOR
};

/* 01: Power Characteristics Count (follows all Power Node(s) above) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMpst1[] =
{
    {ACPI_DMT_UINT16,   ACPI_MPST1_OFFSET (CharacteristicsCount),   "Characteristics Count", 0},
    {ACPI_DMT_UINT16,   ACPI_MPST1_OFFSET (Reserved),               "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* 02: Memory Power State Characteristics Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMpst2[] =
{
    {ACPI_DMT_UINT8,    ACPI_MPST2_OFFSET (StructureId),            "Structure ID", 0},
    {ACPI_DMT_UINT8,    ACPI_MPST2_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_MPST2_FLAG_OFFSET (Flags,0),           "Memory Preserved", 0},
    {ACPI_DMT_FLAG1,    ACPI_MPST2_FLAG_OFFSET (Flags,0),           "Auto Entry", 0},
    {ACPI_DMT_FLAG2,    ACPI_MPST2_FLAG_OFFSET (Flags,0),           "Auto Exit", 0},

    {ACPI_DMT_UINT16,   ACPI_MPST2_OFFSET (Reserved1),              "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_MPST2_OFFSET (AveragePower),           "Average Power", 0},
    {ACPI_DMT_UINT32,   ACPI_MPST2_OFFSET (PowerSaving),            "Power Saving", 0},
    {ACPI_DMT_UINT64,   ACPI_MPST2_OFFSET (ExitLatency),            "Exit Latency", 0},
    {ACPI_DMT_UINT64,   ACPI_MPST2_OFFSET (Reserved2),              "Reserved", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * MSCT - Maximum System Characteristics Table (ACPI 4.0)
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoMsct[] =
{
    {ACPI_DMT_UINT32,   ACPI_MSCT_OFFSET (ProximityOffset),         "Proximity Offset", 0},
    {ACPI_DMT_UINT32,   ACPI_MSCT_OFFSET (MaxProximityDomains),     "Max Proximity Domains", 0},
    {ACPI_DMT_UINT32,   ACPI_MSCT_OFFSET (MaxClockDomains),         "Max Clock Domains", 0},
    {ACPI_DMT_UINT64,   ACPI_MSCT_OFFSET (MaxAddress),              "Max Physical Address", 0},
    ACPI_DMT_TERMINATOR
};

/* Subtable - Maximum Proximity Domain Information. Version 1 */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMsct0[] =
{
    {ACPI_DMT_UINT8,    ACPI_MSCT0_OFFSET (Revision),               "Revision", 0},
    {ACPI_DMT_UINT8,    ACPI_MSCT0_OFFSET (Length),                 "Length", DT_LENGTH},
    {ACPI_DMT_UINT32,   ACPI_MSCT0_OFFSET (RangeStart),             "Domain Range Start", 0},
    {ACPI_DMT_UINT32,   ACPI_MSCT0_OFFSET (RangeEnd),               "Domain Range End", 0},
    {ACPI_DMT_UINT32,   ACPI_MSCT0_OFFSET (ProcessorCapacity),      "Processor Capacity", 0},
    {ACPI_DMT_UINT64,   ACPI_MSCT0_OFFSET (MemoryCapacity),         "Memory Capacity", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * MTMR - MID Timer Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoMtmr[] =
{
    ACPI_DMT_TERMINATOR
};

/* MTMR Subtables - MTMR Entry */

ACPI_DMTABLE_INFO           AcpiDmTableInfoMtmr0[] =
{
    {ACPI_DMT_GAS,      ACPI_MTMR0_OFFSET (PhysicalAddress),        "PhysicalAddress", 0},
    {ACPI_DMT_UINT32,   ACPI_MTMR0_OFFSET (Frequency),              "Frequency", 0},
    {ACPI_DMT_UINT32,   ACPI_MTMR0_OFFSET (Irq),                    "IRQ", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * NFIT - NVDIMM Firmware Interface Table and Subtables - (ACPI 6.0)
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit[] =
{
    {ACPI_DMT_UINT32,   ACPI_NFIT_OFFSET (Reserved),                "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* Common Subtable header */

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfitHdr[] =
{
    {ACPI_DMT_NFIT,     ACPI_NFITH_OFFSET (Type),                   "Subtable Type", 0},
    {ACPI_DMT_UINT16,   ACPI_NFITH_OFFSET (Length),                 "Length", DT_LENGTH},
    ACPI_DMT_TERMINATOR
};

/* 0: System Physical Address Range Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit0[] =
{
    {ACPI_DMT_UINT16,   ACPI_NFIT0_OFFSET (RangeIndex),             "Range Index", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT0_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_NFIT0_FLAG_OFFSET (Flags,0),           "Add/Online Operation Only", 0},
    {ACPI_DMT_FLAG1,    ACPI_NFIT0_FLAG_OFFSET (Flags,0),           "Proximity Domain Valid", 0},
    {ACPI_DMT_UINT32,   ACPI_NFIT0_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_NFIT0_OFFSET (ProximityDomain),        "Proximity Domain", 0},
    {ACPI_DMT_UUID,     ACPI_NFIT0_OFFSET (RangeGuid[0]),           "Region Type GUID", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT0_OFFSET (Address),                "Address Range Base", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT0_OFFSET (Length),                 "Address Range Length", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT0_OFFSET (MemoryMapping),          "Memory Map Attribute", 0},
    ACPI_DMT_TERMINATOR
};

/* 1: Memory Device to System Address Range Map Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit1[] =
{
    {ACPI_DMT_UINT32,   ACPI_NFIT1_OFFSET (DeviceHandle),           "Device Handle", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT1_OFFSET (PhysicalId),             "Physical Id", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT1_OFFSET (RegionId),               "Region Id", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT1_OFFSET (RangeIndex),             "Range Index", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT1_OFFSET (RegionIndex),            "Control Region Index", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT1_OFFSET (RegionSize),             "Region Size", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT1_OFFSET (RegionOffset),           "Region Offset", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT1_OFFSET (Address),                "Address Region Base", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT1_OFFSET (InterleaveIndex),        "Interleave Index", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT1_OFFSET (InterleaveWays),         "Interleave Ways", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT1_OFFSET (Flags),                  "Flags", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_NFIT1_FLAG_OFFSET (Flags,0),           "Save to device failed", 0},
    {ACPI_DMT_FLAG1,    ACPI_NFIT1_FLAG_OFFSET (Flags,0),           "Restore from device failed", 0},
    {ACPI_DMT_FLAG2,    ACPI_NFIT1_FLAG_OFFSET (Flags,0),           "Platform flush failed", 0},
    {ACPI_DMT_FLAG3,    ACPI_NFIT1_FLAG_OFFSET (Flags,0),           "Device not armed", 0},
    {ACPI_DMT_FLAG4,    ACPI_NFIT1_FLAG_OFFSET (Flags,0),           "Health events observed", 0},
    {ACPI_DMT_FLAG5,    ACPI_NFIT1_FLAG_OFFSET (Flags,0),           "Health events enabled", 0},
    {ACPI_DMT_FLAG6,    ACPI_NFIT1_FLAG_OFFSET (Flags,0),           "Mapping failed", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT1_OFFSET (Reserved),               "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* 2: Interleave Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit2[] =
{
    {ACPI_DMT_UINT16,   ACPI_NFIT2_OFFSET (InterleaveIndex),        "Interleave Index", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT2_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_NFIT2_OFFSET (LineCount),              "Line Count", 0},
    {ACPI_DMT_UINT32,   ACPI_NFIT2_OFFSET (LineSize),               "Line Size", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit2a[] =
{
    {ACPI_DMT_UINT32,   0,                                          "Line Offset", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

/* 3: SMBIOS Management Information Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit3[] =
{
    {ACPI_DMT_UINT32,   ACPI_NFIT3_OFFSET (Reserved),               "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit3a[] =
{
    {ACPI_DMT_RAW_BUFFER, 0,                                        "SMBIOS Table Entries", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

/* 4: NVDIMM Control Region Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit4[] =
{
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (RegionIndex),            "Region Index", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (VendorId),               "Vendor Id", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (DeviceId),               "Device Id", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (RevisionId),             "Revision Id", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (SubsystemVendorId),      "Subsystem Vendor Id", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (SubsystemDeviceId),      "Subsystem Device Id", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (SubsystemRevisionId),    "Subsystem Revision Id", 0},
    {ACPI_DMT_UINT8,    ACPI_NFIT4_OFFSET (ValidFields),            "Valid Fields", 0},
    {ACPI_DMT_UINT8,    ACPI_NFIT4_OFFSET (ManufacturingLocation),  "Manufacturing Location", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (ManufacturingDate),      "Manufacturing Date", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (Reserved[0]),            "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_NFIT4_OFFSET (SerialNumber),           "Serial Number", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (Code),                   "Code", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (Windows),                "Window Count", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT4_OFFSET (WindowSize),             "Window Size", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT4_OFFSET (CommandOffset),          "Command Offset", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT4_OFFSET (CommandSize),            "Command Size", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT4_OFFSET (StatusOffset),           "Status Offset", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT4_OFFSET (StatusSize),             "Status Size", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT4_OFFSET (Flags),                  "Flags", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_NFIT4_FLAG_OFFSET (Flags,0),           "Windows buffered", 0},
    {ACPI_DMT_UINT48,   ACPI_NFIT4_OFFSET (Reserved1[0]),           "Reserved1", 0},
    ACPI_DMT_TERMINATOR
};

/* 5: NVDIMM Block Data Window Region Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit5[] =
{
    {ACPI_DMT_UINT16,   ACPI_NFIT5_OFFSET (RegionIndex),            "Region Index", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT5_OFFSET (Windows),                "Window Count", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT5_OFFSET (Offset),                 "Offset", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT5_OFFSET (Size),                   "Size", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT5_OFFSET (Capacity),               "Capacity", 0},
    {ACPI_DMT_UINT64,   ACPI_NFIT5_OFFSET (StartAddress),           "Start Address", 0},
    ACPI_DMT_TERMINATOR
};

/* 6: Flush Hint Address Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit6[] =
{
    {ACPI_DMT_UINT32,   ACPI_NFIT6_OFFSET (DeviceHandle),           "Device Handle", 0},
    {ACPI_DMT_UINT16,   ACPI_NFIT6_OFFSET (HintCount),              "Hint Count", 0},
    {ACPI_DMT_UINT48,   ACPI_NFIT6_OFFSET (Reserved[0]),            "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit6a[] =
{
    {ACPI_DMT_UINT64,   0,                                          "Hint Address", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoNfit7[] =
{
    {ACPI_DMT_UINT8,    ACPI_NFIT7_OFFSET (HighestCapability),      "Highest Capability", 0},
    {ACPI_DMT_UINT24,   ACPI_NFIT7_OFFSET (Reserved[0]),            "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_NFIT7_OFFSET (Capabilities),           "Capabilities (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_NFIT7_FLAG_OFFSET (Capabilities,0),    "Cache Flush to NVDIMM", 0},
    {ACPI_DMT_FLAG1,    ACPI_NFIT7_FLAG_OFFSET (Capabilities,0),    "Memory Flush to NVDIMM", 0},
    {ACPI_DMT_FLAG2,    ACPI_NFIT7_FLAG_OFFSET (Capabilities,0),    "Memory Mirroring", 0},
    {ACPI_DMT_UINT32,   ACPI_NFIT7_OFFSET (Reserved2),              "Reserved", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * PCCT - Platform Communications Channel Table (ACPI 5.0)
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoPcct[] =
{
    {ACPI_DMT_UINT32,   ACPI_PCCT_OFFSET (Flags),                   "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_PCCT_FLAG_OFFSET (Flags,0),            "Platform", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT_OFFSET (Reserved),                "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* PCCT subtables */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPcctHdr[] =
{
    {ACPI_DMT_PCCT,     ACPI_PCCT0_OFFSET (Header.Type),            "Subtable Type", 0},
    {ACPI_DMT_UINT8,    ACPI_PCCT0_OFFSET (Header.Length),          "Length", DT_LENGTH},
    ACPI_DMT_TERMINATOR
};

/* 0: Generic Communications Subspace */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPcct0[] =
{
    {ACPI_DMT_UINT48,   ACPI_PCCT0_OFFSET (Reserved[0]),            "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT0_OFFSET (BaseAddress),            "Base Address", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT0_OFFSET (Length),                 "Address Length", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT0_OFFSET (DoorbellRegister),       "Doorbell Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT0_OFFSET (PreserveMask),           "Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT0_OFFSET (WriteMask),              "Write Mask", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT0_OFFSET (Latency),                "Command Latency", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT0_OFFSET (MaxAccessRate),          "Maximum Access Rate", 0},
    {ACPI_DMT_UINT16,   ACPI_PCCT0_OFFSET (MinTurnaroundTime),      "Minimum Turnaround Time", 0},
    ACPI_DMT_TERMINATOR
};

/* 1: HW-reduced Communications Subspace (ACPI 5.1) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPcct1[] =
{
    {ACPI_DMT_UINT32,   ACPI_PCCT1_OFFSET (PlatformInterrupt),      "Platform Interrupt", 0},
    {ACPI_DMT_UINT8,    ACPI_PCCT1_OFFSET (Flags),                  "Flags (Decoded Below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_PCCT1_FLAG_OFFSET (Flags,0),           "Polarity", 0},
    {ACPI_DMT_FLAG1,    ACPI_PCCT1_FLAG_OFFSET (Flags,0),           "Mode", 0},
    {ACPI_DMT_UINT8,    ACPI_PCCT1_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT1_OFFSET (BaseAddress),            "Base Address", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT1_OFFSET (Length),                 "Address Length", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT1_OFFSET (DoorbellRegister),       "Doorbell Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT1_OFFSET (PreserveMask),           "Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT1_OFFSET (WriteMask),              "Write Mask", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT1_OFFSET (Latency),                "Command Latency", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT1_OFFSET (MaxAccessRate),          "Maximum Access Rate", 0},
    {ACPI_DMT_UINT16,   ACPI_PCCT1_OFFSET (MinTurnaroundTime),      "Minimum Turnaround Time", 0},
    ACPI_DMT_TERMINATOR
};

/* 2: HW-reduced Communications Subspace Type 2 (ACPI 6.1) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPcct2[] =
{
    {ACPI_DMT_UINT32,   ACPI_PCCT2_OFFSET (PlatformInterrupt),      "Platform Interrupt", 0},
    {ACPI_DMT_UINT8,    ACPI_PCCT2_OFFSET (Flags),                  "Flags (Decoded Below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_PCCT2_FLAG_OFFSET (Flags,0),           "Polarity", 0},
    {ACPI_DMT_FLAG1,    ACPI_PCCT2_FLAG_OFFSET (Flags,0),           "Mode", 0},
    {ACPI_DMT_UINT8,    ACPI_PCCT2_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT2_OFFSET (BaseAddress),            "Base Address", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT2_OFFSET (Length),                 "Address Length", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT2_OFFSET (DoorbellRegister),       "Doorbell Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT2_OFFSET (PreserveMask),           "Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT2_OFFSET (WriteMask),              "Write Mask", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT2_OFFSET (Latency),                "Command Latency", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT2_OFFSET (MaxAccessRate),          "Maximum Access Rate", 0},
    {ACPI_DMT_UINT16,   ACPI_PCCT2_OFFSET (MinTurnaroundTime),      "Minimum Turnaround Time", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT2_OFFSET (PlatformAckRegister),    "Platform ACK Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT2_OFFSET (AckPreserveMask),        "ACK Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT2_OFFSET (AckWriteMask),           "ACK Write Mask", 0},
    ACPI_DMT_TERMINATOR
};

/* 3: Extended PCC Master Subspace Type 3 (ACPI 6.2) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPcct3[] =
{
    {ACPI_DMT_UINT32,   ACPI_PCCT3_OFFSET (PlatformInterrupt),      "Platform Interrupt", 0},
    {ACPI_DMT_UINT8,    ACPI_PCCT3_OFFSET (Flags),                  "Flags (Decoded Below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_PCCT3_FLAG_OFFSET (Flags,0),           "Polarity", 0},
    {ACPI_DMT_FLAG1,    ACPI_PCCT3_FLAG_OFFSET (Flags,0),           "Mode", 0},
    {ACPI_DMT_UINT8,    ACPI_PCCT3_OFFSET (Reserved1),              "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (BaseAddress),            "Base Address", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT3_OFFSET (Length),                 "Address Length", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT3_OFFSET (DoorbellRegister),       "Doorbell Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (PreserveMask),           "Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (WriteMask),              "Write Mask", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT3_OFFSET (Latency),                "Command Latency", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT3_OFFSET (MaxAccessRate),          "Maximum Access Rate", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT3_OFFSET (MinTurnaroundTime),      "Minimum Turnaround Time", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT3_OFFSET (PlatformAckRegister),    "Platform ACK Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (AckPreserveMask),        "ACK Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (AckSetMask),             "ACK Set Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (Reserved2),              "Reserved", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT3_OFFSET (CmdCompleteRegister),    "Command Complete Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (CmdCompleteMask),        "Command Complete Check Mask", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT3_OFFSET (CmdUpdateRegister),      "Command Update Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (CmdUpdatePreserveMask),  "Command Update Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (CmdUpdateSetMask),       "Command Update Set Mask", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT3_OFFSET (ErrorStatusRegister),    "Error Status Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT3_OFFSET (ErrorStatusMask),        "Error Status Mask", 0},
    ACPI_DMT_TERMINATOR
};

/* 4: Extended PCC Slave Subspace Type 4 (ACPI 6.2) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPcct4[] =
{
    {ACPI_DMT_UINT32,   ACPI_PCCT4_OFFSET (PlatformInterrupt),      "Platform Interrupt", 0},
    {ACPI_DMT_UINT8,    ACPI_PCCT4_OFFSET (Flags),                  "Flags (Decoded Below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_PCCT4_FLAG_OFFSET (Flags,0),           "Polarity", 0},
    {ACPI_DMT_FLAG1,    ACPI_PCCT4_FLAG_OFFSET (Flags,0),           "Mode", 0},
    {ACPI_DMT_UINT8,    ACPI_PCCT4_OFFSET (Reserved1),              "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (BaseAddress),            "Base Address", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT4_OFFSET (Length),                 "Address Length", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT4_OFFSET (DoorbellRegister),       "Doorbell Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (PreserveMask),           "Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (WriteMask),              "Write Mask", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT4_OFFSET (Latency),                "Command Latency", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT4_OFFSET (MaxAccessRate),          "Maximum Access Rate", 0},
    {ACPI_DMT_UINT32,   ACPI_PCCT4_OFFSET (MinTurnaroundTime),      "Minimum Turnaround Time", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT4_OFFSET (PlatformAckRegister),    "Platform ACK Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (AckPreserveMask),        "ACK Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (AckSetMask),             "ACK Set Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (Reserved2),              "Reserved", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT4_OFFSET (CmdCompleteRegister),    "Command Complete Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (CmdCompleteMask),        "Command Complete Check Mask", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT4_OFFSET (CmdUpdateRegister),      "Command Update Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (CmdUpdatePreserveMask),  "Command Update Preserve Mask", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (CmdUpdateSetMask),       "Command Update Set Mask", 0},
    {ACPI_DMT_GAS,      ACPI_PCCT4_OFFSET (ErrorStatusRegister),    "Error Status Register", 0},
    {ACPI_DMT_UINT64,   ACPI_PCCT4_OFFSET (ErrorStatusMask),        "Error Status Mask", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * PDTT - Platform Debug Trigger Table (ACPI 6.2)
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoPdtt[] =
{
    {ACPI_DMT_UINT8,    ACPI_PDTT_OFFSET (TriggerCount),            "Trigger Count", 0},
    {ACPI_DMT_UINT24,   ACPI_PDTT_OFFSET (Reserved),                "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_PDTT_OFFSET (ArrayOffset),             "Array Offset", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoPdtt0[] =
{
    {ACPI_DMT_UINT8,    ACPI_PDTT0_OFFSET (SubchannelId),           "Subchannel Id", 0},
    {ACPI_DMT_UINT8,    ACPI_PDTT0_OFFSET (Flags),                  "Flags (Decoded Below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_PDTT0_FLAG_OFFSET (Flags,0),           "Runtime Trigger", 0},
    {ACPI_DMT_FLAG1,    ACPI_PDTT0_FLAG_OFFSET (Flags,0),           "Wait for Completion", 0},
    {ACPI_DMT_FLAG2,    ACPI_PDTT0_FLAG_OFFSET (Flags,0),           "Trigger Order", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * PMTT - Platform Memory Topology Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoPmtt[] =
{
    {ACPI_DMT_UINT32,   ACPI_PMTT_OFFSET (Reserved),                "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* Common Subtable header (one per Subtable) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPmttHdr[] =
{
    {ACPI_DMT_PMTT,     ACPI_PMTTH_OFFSET (Type),                   "Subtable Type", 0},
    {ACPI_DMT_UINT8,    ACPI_PMTTH_OFFSET (Reserved1),              "Reserved", 0},
    {ACPI_DMT_UINT16,   ACPI_PMTTH_OFFSET (Length),                 "Length", DT_LENGTH},
    {ACPI_DMT_UINT16,   ACPI_PMTTH_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_PMTTH_FLAG_OFFSET (Flags,0),           "Top-level Device", 0},
    {ACPI_DMT_FLAG1,    ACPI_PMTTH_FLAG_OFFSET (Flags,0),           "Physical Element", 0},
    {ACPI_DMT_FLAGS2,   ACPI_PMTTH_FLAG_OFFSET (Flags,0),           "Memory Type", 0},
    {ACPI_DMT_UINT16,   ACPI_PMTTH_OFFSET (Reserved2),              "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* PMTT Subtables */

/* 0: Socket */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPmtt0[] =
{
    {ACPI_DMT_UINT16,   ACPI_PMTT0_OFFSET (SocketId),               "Socket ID", 0},
    {ACPI_DMT_UINT16,   ACPI_PMTT0_OFFSET (Reserved),               "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* 1: Memory Controller */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPmtt1[] =
{
    {ACPI_DMT_UINT32,   ACPI_PMTT1_OFFSET (ReadLatency),            "Read Latency", 0},
    {ACPI_DMT_UINT32,   ACPI_PMTT1_OFFSET (WriteLatency),           "Write Latency", 0},
    {ACPI_DMT_UINT32,   ACPI_PMTT1_OFFSET (ReadBandwidth),          "Read Bandwidth", 0},
    {ACPI_DMT_UINT32,   ACPI_PMTT1_OFFSET (WriteBandwidth),         "Write Bandwidth", 0},
    {ACPI_DMT_UINT16,   ACPI_PMTT1_OFFSET (AccessWidth),            "Access Width", 0},
    {ACPI_DMT_UINT16,   ACPI_PMTT1_OFFSET (Alignment),              "Alignment", 0},
    {ACPI_DMT_UINT16,   ACPI_PMTT1_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT16,   ACPI_PMTT1_OFFSET (DomainCount),            "Domain Count", 0},
    ACPI_DMT_TERMINATOR
};

/* 1a: Proximity Domain */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPmtt1a[] =
{
    {ACPI_DMT_UINT32,   ACPI_PMTT1A_OFFSET (ProximityDomain),       "Proximity Domain", 0},
    ACPI_DMT_TERMINATOR
};

/* 2: Physical Component */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPmtt2[] =
{
    {ACPI_DMT_UINT16,   ACPI_PMTT2_OFFSET (ComponentId),            "Component ID", 0},
    {ACPI_DMT_UINT16,   ACPI_PMTT2_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_PMTT2_OFFSET (MemorySize),             "Memory Size", 0},
    {ACPI_DMT_UINT32,   ACPI_PMTT2_OFFSET (BiosHandle),             "Bios Handle", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * PPTT - Processor Properties Topology Table (ACPI 6.2)
 *
 ******************************************************************************/

/* Main table consists of only the standard ACPI header - subtables follow */

/* Common Subtable header (one per Subtable) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPpttHdr[] =
{
    {ACPI_DMT_PPTT,     ACPI_PPTTH_OFFSET (Type),                   "Subtable Type", 0},
    {ACPI_DMT_UINT8,    ACPI_PPTTH_OFFSET (Length),                 "Length", 0},
    ACPI_DMT_TERMINATOR
};

/* 0: Processor hierarchy node */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPptt0[] =
{
    {ACPI_DMT_UINT16,   ACPI_PPTT0_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_PPTT0_OFFSET (Flags),                  "Flags (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_PPTT0_FLAG_OFFSET (Flags,0),           "Physical package", 0},
    {ACPI_DMT_FLAG1,    ACPI_PPTT0_FLAG_OFFSET (Flags,0),           "ACPI Processor ID valid", 0},
    {ACPI_DMT_FLAG2,    ACPI_PPTT0_FLAG_OFFSET (Flags,0),           "Processor is a thread", 0},
    {ACPI_DMT_FLAG3,    ACPI_PPTT0_FLAG_OFFSET (Flags,0),           "Node is a leaf", 0},
    {ACPI_DMT_FLAG4,    ACPI_PPTT0_FLAG_OFFSET (Flags,0),           "Identical Implementation", 0},
    {ACPI_DMT_UINT32,   ACPI_PPTT0_OFFSET (Parent),                 "Parent", 0},
    {ACPI_DMT_UINT32,   ACPI_PPTT0_OFFSET (AcpiProcessorId),        "ACPI Processor ID", 0},
    {ACPI_DMT_UINT32,   ACPI_PPTT0_OFFSET (NumberOfPrivResources),  "Private Resource Number", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoPptt0a[] =
{
    {ACPI_DMT_UINT32,   0,                                          "Private Resource", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

/* 1: Cache type */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPptt1[] =
{
    {ACPI_DMT_UINT16,   ACPI_PPTT1_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_PPTT1_OFFSET (Flags),                  "Flags (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_PPTT1_FLAG_OFFSET (Flags,0),           "Size valid", 0},
    {ACPI_DMT_FLAG1,    ACPI_PPTT1_FLAG_OFFSET (Flags,0),           "Number of Sets valid", 0},
    {ACPI_DMT_FLAG2,    ACPI_PPTT1_FLAG_OFFSET (Flags,0),           "Associativity valid", 0},
    {ACPI_DMT_FLAG3,    ACPI_PPTT1_FLAG_OFFSET (Flags,0),           "Allocation Type valid", 0},
    {ACPI_DMT_FLAG4,    ACPI_PPTT1_FLAG_OFFSET (Flags,0),           "Cache Type valid", 0},
    {ACPI_DMT_FLAG5,    ACPI_PPTT1_FLAG_OFFSET (Flags,0),           "Write Policy valid", 0},
    {ACPI_DMT_FLAG6,    ACPI_PPTT1_FLAG_OFFSET (Flags,0),           "Line Size valid", 0},
    {ACPI_DMT_UINT32,   ACPI_PPTT1_OFFSET (NextLevelOfCache),       "Next Level of Cache", 0},
    {ACPI_DMT_UINT32,   ACPI_PPTT1_OFFSET (Size),                   "Size", 0},
    {ACPI_DMT_UINT32,   ACPI_PPTT1_OFFSET (NumberOfSets),           "Number of Sets", 0},
    {ACPI_DMT_UINT8,    ACPI_PPTT1_OFFSET (Associativity),          "Associativity", 0},
    {ACPI_DMT_UINT8,    ACPI_PPTT1_OFFSET (Attributes),             "Attributes", 0},
    {ACPI_DMT_FLAGS0,   ACPI_PPTT1_OFFSET (Attributes),             "Allocation Type", 0},
    {ACPI_DMT_FLAGS2,   ACPI_PPTT1_OFFSET (Attributes),             "Cache Type", 0},
    {ACPI_DMT_FLAG4,    ACPI_PPTT1_OFFSET (Attributes),             "Write Policy", 0},
    {ACPI_DMT_UINT16,   ACPI_PPTT1_OFFSET (LineSize),               "Line Size", 0},
    ACPI_DMT_TERMINATOR
};

/* 2: ID */

ACPI_DMTABLE_INFO           AcpiDmTableInfoPptt2[] =
{
    {ACPI_DMT_UINT16,   ACPI_PPTT2_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_PPTT2_OFFSET (VendorId),               "Vendor ID", 0},
    {ACPI_DMT_UINT64,   ACPI_PPTT2_OFFSET (Level1Id),               "Level1 ID", 0},
    {ACPI_DMT_UINT64,   ACPI_PPTT2_OFFSET (Level2Id),               "Level2 ID", 0},
    {ACPI_DMT_UINT16,   ACPI_PPTT2_OFFSET (MajorRev),               "Major revision", 0},
    {ACPI_DMT_UINT16,   ACPI_PPTT2_OFFSET (MinorRev),               "Minor revision", 0},
    {ACPI_DMT_UINT16,   ACPI_PPTT2_OFFSET (SpinRev),                "Spin revision", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * RASF -  RAS Feature table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoRasf[] =
{
    {ACPI_DMT_BUF12,    ACPI_RASF_OFFSET (ChannelId[0]),            "Channel ID", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * S3PT - S3 Performance Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoS3pt[] =
{
    {ACPI_DMT_SIG,     ACPI_S3PT_OFFSET (Signature[0]),             "Signature", 0},
    {ACPI_DMT_UINT32,  ACPI_S3PT_OFFSET (Length),                   "Length", DT_LENGTH},
    ACPI_DMT_TERMINATOR
};

/* S3PT subtable header */

ACPI_DMTABLE_INFO           AcpiDmTableInfoS3ptHdr[] =
{
    {ACPI_DMT_UINT16,  ACPI_S3PTH_OFFSET (Type),                    "Type", 0},
    {ACPI_DMT_UINT8,   ACPI_S3PTH_OFFSET (Length),                  "Length", DT_LENGTH},
    {ACPI_DMT_UINT8,   ACPI_S3PTH_OFFSET (Revision),                "Revision", 0},
    ACPI_DMT_TERMINATOR
};

/* 0: Basic S3 Resume Performance Record */

ACPI_DMTABLE_INFO           AcpiDmTableInfoS3pt0[] =
{
    {ACPI_DMT_UINT32,  ACPI_S3PT0_OFFSET (ResumeCount),             "Resume Count", 0},
    {ACPI_DMT_UINT64,  ACPI_S3PT0_OFFSET (FullResume),              "Full Resume", 0},
    {ACPI_DMT_UINT64,  ACPI_S3PT0_OFFSET (AverageResume),           "Average Resume", 0},
    ACPI_DMT_TERMINATOR
};

/* 1: Basic S3 Suspend Performance Record */

ACPI_DMTABLE_INFO           AcpiDmTableInfoS3pt1[] =
{
    {ACPI_DMT_UINT64,  ACPI_S3PT1_OFFSET (SuspendStart),            "Suspend Start", 0},
    {ACPI_DMT_UINT64,  ACPI_S3PT1_OFFSET (SuspendEnd),              "Suspend End", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * SBST - Smart Battery Specification Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoSbst[] =
{
    {ACPI_DMT_UINT32,   ACPI_SBST_OFFSET (WarningLevel),            "Warning Level", 0},
    {ACPI_DMT_UINT32,   ACPI_SBST_OFFSET (LowLevel),                "Low Level", 0},
    {ACPI_DMT_UINT32,   ACPI_SBST_OFFSET (CriticalLevel),           "Critical Level", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * SDEI - Software Delegated Exception Interface Descriptor Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoSdei[] =
{
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * SDEV - Secure Devices Table (ACPI 6.2)
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoSdev[] =
{
    ACPI_DMT_TERMINATOR
};

/* Common Subtable header (one per Subtable) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSdevHdr[] =
{
    {ACPI_DMT_SDEV,     ACPI_SDEVH_OFFSET (Type),                   "Subtable Type", 0},
    {ACPI_DMT_UINT8,    ACPI_SDEVH_OFFSET (Flags),                  "Flags (decoded below)", 0},
    {ACPI_DMT_FLAG0,    ACPI_SDEVH_FLAG_OFFSET (Flags,0),           "Allow handoff to unsecure OS", 0},
    {ACPI_DMT_UINT16,   ACPI_SDEVH_OFFSET (Length),                 "Length", 0},
    ACPI_DMT_TERMINATOR
};

/* SDEV Subtables */

/* 0: Namespace Device Based Secure Device Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSdev0[] =
{
    {ACPI_DMT_UINT16,   ACPI_SDEV0_OFFSET (DeviceIdOffset),         "Device ID Offset", 0},
    {ACPI_DMT_UINT16,   ACPI_SDEV0_OFFSET (DeviceIdLength),         "Device ID Length", 0},
    {ACPI_DMT_UINT16,   ACPI_SDEV0_OFFSET (VendorDataOffset),       "Vendor Data Offset", 0},
    {ACPI_DMT_UINT16,   ACPI_SDEV0_OFFSET (VendorDataLength),       "Vendor Data Length", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoSdev0a[] =
{
    {ACPI_DMT_STRING,   0,                                          "Namepath", 0},
    ACPI_DMT_TERMINATOR
};

/* 1: PCIe Endpoint Device Based Device Structure */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSdev1[] =
{
    {ACPI_DMT_UINT16,   ACPI_SDEV1_OFFSET (Segment),                "Segment", 0},
    {ACPI_DMT_UINT16,   ACPI_SDEV1_OFFSET (StartBus),               "Start Bus", 0},
    {ACPI_DMT_UINT16,   ACPI_SDEV1_OFFSET (PathOffset),             "Path Offset", 0},
    {ACPI_DMT_UINT16,   ACPI_SDEV1_OFFSET (PathLength),             "Path Length", 0},
    {ACPI_DMT_UINT16,   ACPI_SDEV1_OFFSET (VendorDataOffset),       "Vendor Data Offset", 0},
    {ACPI_DMT_UINT16,   ACPI_SDEV1_OFFSET (VendorDataLength),       "Vendor Data Length", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoSdev1a[] =
{
    {ACPI_DMT_UINT8,    ACPI_SDEV1A_OFFSET (Device),                "Device", 0},
    {ACPI_DMT_UINT8,    ACPI_SDEV1A_OFFSET (Function),              "Function", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoSdev1b[] =
{
    {ACPI_DMT_RAW_BUFFER, 0,                                        "Vendor Data", 0}, /*, DT_OPTIONAL}, */
    ACPI_DMT_TERMINATOR
};
/*! [End] no source code translation !*/
