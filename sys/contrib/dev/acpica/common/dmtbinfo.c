/******************************************************************************
 *
 * Module Name: dmtbinfo - Table info for non-AML tables
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
        ACPI_MODULE_NAME    ("dmtbinfo")

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
 * ACPI Table Information, used to dump formatted ACPI tables
 *
 * Each entry is of the form:  <Field Type, Field Offset, Field Name>
 */

/*******************************************************************************
 *
 * Common ACPI table header
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoHeader[] =
{
    {ACPI_DMT_SIG,      ACPI_HDR_OFFSET (Signature[0]),             "Signature", 0},
    {ACPI_DMT_UINT32,   ACPI_HDR_OFFSET (Length),                   "Table Length", DT_LENGTH},
    {ACPI_DMT_UINT8,    ACPI_HDR_OFFSET (Revision),                 "Revision", 0},
    {ACPI_DMT_CHKSUM,   ACPI_HDR_OFFSET (Checksum),                 "Checksum", 0},
    {ACPI_DMT_NAME6,    ACPI_HDR_OFFSET (OemId[0]),                 "Oem ID", 0},
    {ACPI_DMT_NAME8,    ACPI_HDR_OFFSET (OemTableId[0]),            "Oem Table ID", 0},
    {ACPI_DMT_UINT32,   ACPI_HDR_OFFSET (OemRevision),              "Oem Revision", 0},
    {ACPI_DMT_NAME4,    ACPI_HDR_OFFSET (AslCompilerId[0]),         "Asl Compiler ID", 0},
    {ACPI_DMT_UINT32,   ACPI_HDR_OFFSET (AslCompilerRevision),      "Asl Compiler Revision", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * GAS - Generic Address Structure
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoGas[] =
{
    {ACPI_DMT_SPACEID,  ACPI_GAS_OFFSET (SpaceId),                  "Space ID", 0},
    {ACPI_DMT_UINT8,    ACPI_GAS_OFFSET (BitWidth),                 "Bit Width", 0},
    {ACPI_DMT_UINT8,    ACPI_GAS_OFFSET (BitOffset),                "Bit Offset", 0},
    {ACPI_DMT_ACCWIDTH, ACPI_GAS_OFFSET (AccessWidth),              "Encoded Access Width", 0},
    {ACPI_DMT_UINT64,   ACPI_GAS_OFFSET (Address),                  "Address", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * RSDP - Root System Description Pointer (Signature is "RSD PTR ")
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoRsdp1[] =
{
    {ACPI_DMT_NAME8,    ACPI_RSDP_OFFSET (Signature[0]),            "Signature", 0},
    {ACPI_DMT_UINT8,    ACPI_RSDP_OFFSET (Checksum),                "Checksum", 0},
    {ACPI_DMT_NAME6,    ACPI_RSDP_OFFSET (OemId[0]),                "Oem ID", 0},
    {ACPI_DMT_UINT8,    ACPI_RSDP_OFFSET (Revision),                "Revision", 0},
    {ACPI_DMT_UINT32,   ACPI_RSDP_OFFSET (RsdtPhysicalAddress),     "RSDT Address", 0},
    ACPI_DMT_TERMINATOR
};

/* ACPI 2.0+ Extensions */

ACPI_DMTABLE_INFO           AcpiDmTableInfoRsdp2[] =
{
    {ACPI_DMT_UINT32,   ACPI_RSDP_OFFSET (Length),                  "Length", DT_LENGTH},
    {ACPI_DMT_UINT64,   ACPI_RSDP_OFFSET (XsdtPhysicalAddress),     "XSDT Address", 0},
    {ACPI_DMT_UINT8,    ACPI_RSDP_OFFSET (ExtendedChecksum),        "Extended Checksum", 0},
    {ACPI_DMT_UINT24,   ACPI_RSDP_OFFSET (Reserved[0]),             "Reserved", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * FACS - Firmware ACPI Control Structure
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoFacs[] =
{
    {ACPI_DMT_NAME4,    ACPI_FACS_OFFSET (Signature[0]),            "Signature", 0},
    {ACPI_DMT_UINT32,   ACPI_FACS_OFFSET (Length),                  "Length", DT_LENGTH},
    {ACPI_DMT_UINT32,   ACPI_FACS_OFFSET (HardwareSignature),       "Hardware Signature", 0},
    {ACPI_DMT_UINT32,   ACPI_FACS_OFFSET (FirmwareWakingVector),    "32 Firmware Waking Vector", 0},
    {ACPI_DMT_UINT32,   ACPI_FACS_OFFSET (GlobalLock),              "Global Lock", 0},
    {ACPI_DMT_UINT32,   ACPI_FACS_OFFSET (Flags),                   "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_FACS_FLAG_OFFSET (Flags,0),            "S4BIOS Support Present", 0},
    {ACPI_DMT_FLAG1,    ACPI_FACS_FLAG_OFFSET (Flags,0),            "64-bit Wake Supported (V2)", 0},
    {ACPI_DMT_UINT64,   ACPI_FACS_OFFSET (XFirmwareWakingVector),   "64 Firmware Waking Vector", 0},
    {ACPI_DMT_UINT8,    ACPI_FACS_OFFSET (Version),                 "Version", 0},
    {ACPI_DMT_UINT24,   ACPI_FACS_OFFSET (Reserved[0]),             "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_FACS_OFFSET (OspmFlags),               "OspmFlags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_FACS_FLAG_OFFSET (OspmFlags,0),        "64-bit Wake Env Required (V2)", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * FADT - Fixed ACPI Description Table (Signature is FACP)
 *
 ******************************************************************************/

/* ACPI 1.0 FADT (Version 1) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoFadt1[] =
{
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Facs),                    "FACS Address", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Dsdt),                    "DSDT Address", DT_NON_ZERO},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (Model),                   "Model", 0},
    {ACPI_DMT_FADTPM,   ACPI_FADT_OFFSET (PreferredProfile),        "PM Profile", 0},
    {ACPI_DMT_UINT16,   ACPI_FADT_OFFSET (SciInterrupt),            "SCI Interrupt", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (SmiCommand),              "SMI Command Port", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (AcpiEnable),              "ACPI Enable Value", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (AcpiDisable),             "ACPI Disable Value", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (S4BiosRequest),           "S4BIOS Command", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (PstateControl),           "P-State Control", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Pm1aEventBlock),          "PM1A Event Block Address", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Pm1bEventBlock),          "PM1B Event Block Address", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Pm1aControlBlock),        "PM1A Control Block Address", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Pm1bControlBlock),        "PM1B Control Block Address", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Pm2ControlBlock),         "PM2 Control Block Address", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (PmTimerBlock),            "PM Timer Block Address", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Gpe0Block),               "GPE0 Block Address", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Gpe1Block),               "GPE1 Block Address", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (Pm1EventLength),          "PM1 Event Block Length", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (Pm1ControlLength),        "PM1 Control Block Length", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (Pm2ControlLength),        "PM2 Control Block Length", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (PmTimerLength),           "PM Timer Block Length", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (Gpe0BlockLength),         "GPE0 Block Length", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (Gpe1BlockLength),         "GPE1 Block Length", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (Gpe1Base),                "GPE1 Base Offset", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (CstControl),              "_CST Support", 0},
    {ACPI_DMT_UINT16,   ACPI_FADT_OFFSET (C2Latency),               "C2 Latency", 0},
    {ACPI_DMT_UINT16,   ACPI_FADT_OFFSET (C3Latency),               "C3 Latency", 0},
    {ACPI_DMT_UINT16,   ACPI_FADT_OFFSET (FlushSize),               "CPU Cache Size", 0},
    {ACPI_DMT_UINT16,   ACPI_FADT_OFFSET (FlushStride),             "Cache Flush Stride", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (DutyOffset),              "Duty Cycle Offset", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (DutyWidth),               "Duty Cycle Width", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (DayAlarm),                "RTC Day Alarm Index", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (MonthAlarm),              "RTC Month Alarm Index", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (Century),                 "RTC Century Index", 0},
    {ACPI_DMT_UINT16,   ACPI_FADT_OFFSET (BootFlags),               "Boot Flags (decoded below)", DT_FLAG},

    /* Boot Architecture Flags byte 0 */

    {ACPI_DMT_FLAG0,    ACPI_FADT_FLAG_OFFSET (BootFlags,0),        "Legacy Devices Supported (V2)", 0},
    {ACPI_DMT_FLAG1,    ACPI_FADT_FLAG_OFFSET (BootFlags,0),        "8042 Present on ports 60/64 (V2)", 0},
    {ACPI_DMT_FLAG2,    ACPI_FADT_FLAG_OFFSET (BootFlags,0),        "VGA Not Present (V4)", 0},
    {ACPI_DMT_FLAG3,    ACPI_FADT_FLAG_OFFSET (BootFlags,0),        "MSI Not Supported (V4)", 0},
    {ACPI_DMT_FLAG4,    ACPI_FADT_FLAG_OFFSET (BootFlags,0),        "PCIe ASPM Not Supported (V4)", 0},
    {ACPI_DMT_FLAG5,    ACPI_FADT_FLAG_OFFSET (BootFlags,0),        "CMOS RTC Not Present (V5)", 0},

    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (Reserved),                "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_FADT_OFFSET (Flags),                   "Flags (decoded below)", DT_FLAG},

    /* Flags byte 0 */

    {ACPI_DMT_FLAG0,    ACPI_FADT_FLAG_OFFSET (Flags,0),            "WBINVD instruction is operational (V1)", 0},
    {ACPI_DMT_FLAG1,    ACPI_FADT_FLAG_OFFSET (Flags,0),            "WBINVD flushes all caches (V1)", 0},
    {ACPI_DMT_FLAG2,    ACPI_FADT_FLAG_OFFSET (Flags,0),            "All CPUs support C1 (V1)", 0},
    {ACPI_DMT_FLAG3,    ACPI_FADT_FLAG_OFFSET (Flags,0),            "C2 works on MP system (V1)", 0},
    {ACPI_DMT_FLAG4,    ACPI_FADT_FLAG_OFFSET (Flags,0),            "Control Method Power Button (V1)", 0},
    {ACPI_DMT_FLAG5,    ACPI_FADT_FLAG_OFFSET (Flags,0),            "Control Method Sleep Button (V1)", 0},
    {ACPI_DMT_FLAG6,    ACPI_FADT_FLAG_OFFSET (Flags,0),            "RTC wake not in fixed reg space (V1)", 0},
    {ACPI_DMT_FLAG7,    ACPI_FADT_FLAG_OFFSET (Flags,0),            "RTC can wake system from S4 (V1)", 0},

    /* Flags byte 1 */

    {ACPI_DMT_FLAG0,    ACPI_FADT_FLAG_OFFSET (Flags,1),            "32-bit PM Timer (V1)", 0},
    {ACPI_DMT_FLAG1,    ACPI_FADT_FLAG_OFFSET (Flags,1),            "Docking Supported (V1)", 0},
    {ACPI_DMT_FLAG2,    ACPI_FADT_FLAG_OFFSET (Flags,1),            "Reset Register Supported (V2)", 0},
    {ACPI_DMT_FLAG3,    ACPI_FADT_FLAG_OFFSET (Flags,1),            "Sealed Case (V3)", 0},
    {ACPI_DMT_FLAG4,    ACPI_FADT_FLAG_OFFSET (Flags,1),            "Headless - No Video (V3)", 0},
    {ACPI_DMT_FLAG5,    ACPI_FADT_FLAG_OFFSET (Flags,1),            "Use native instr after SLP_TYPx (V3)", 0},
    {ACPI_DMT_FLAG6,    ACPI_FADT_FLAG_OFFSET (Flags,1),            "PCIEXP_WAK Bits Supported (V4)", 0},
    {ACPI_DMT_FLAG7,    ACPI_FADT_FLAG_OFFSET (Flags,1),            "Use Platform Timer (V4)", 0},

    /* Flags byte 2 */

    {ACPI_DMT_FLAG0,    ACPI_FADT_FLAG_OFFSET (Flags,2),            "RTC_STS valid on S4 wake (V4)", 0},
    {ACPI_DMT_FLAG1,    ACPI_FADT_FLAG_OFFSET (Flags,2),            "Remote Power-on capable (V4)", 0},
    {ACPI_DMT_FLAG2,    ACPI_FADT_FLAG_OFFSET (Flags,2),            "Use APIC Cluster Model (V4)", 0},
    {ACPI_DMT_FLAG3,    ACPI_FADT_FLAG_OFFSET (Flags,2),            "Use APIC Physical Destination Mode (V4)", 0},
    {ACPI_DMT_FLAG4,    ACPI_FADT_FLAG_OFFSET (Flags,2),            "Hardware Reduced (V5)", 0},
    {ACPI_DMT_FLAG5,    ACPI_FADT_FLAG_OFFSET (Flags,2),            "Low Power S0 Idle (V5)", 0},
    ACPI_DMT_TERMINATOR
};

/* ACPI 1.0 MS Extensions (FADT version 2) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoFadt2[] =
{
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (ResetRegister),           "Reset Register", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (ResetValue),              "Value to cause reset", 0},
    {ACPI_DMT_UINT16,   ACPI_FADT_OFFSET (ArmBootFlags),            "Reserved", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (MinorRevision),           "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* ACPI 2.0+ Extensions (FADT version 3, 4, and 5) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoFadt3[] =
{
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (ResetRegister),           "Reset Register", 0},
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (ResetValue),              "Value to cause reset", 0},
    {ACPI_DMT_UINT16,   ACPI_FADT_OFFSET (ArmBootFlags),            "ARM Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_FADT_FLAG_OFFSET(ArmBootFlags,0),      "PSCI Compliant", 0},
    {ACPI_DMT_FLAG1,    ACPI_FADT_FLAG_OFFSET(ArmBootFlags,0),      "Must use HVC for PSCI", 0},
    ACPI_DMT_NEW_LINE,
    {ACPI_DMT_UINT8,    ACPI_FADT_OFFSET (MinorRevision),           "FADT Minor Revision", 0},
    {ACPI_DMT_UINT64,   ACPI_FADT_OFFSET (XFacs),                   "FACS Address", 0},
    {ACPI_DMT_UINT64,   ACPI_FADT_OFFSET (XDsdt),                   "DSDT Address", 0},
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (XPm1aEventBlock),         "PM1A Event Block", 0},
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (XPm1bEventBlock),         "PM1B Event Block", 0},
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (XPm1aControlBlock),       "PM1A Control Block", 0},
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (XPm1bControlBlock),       "PM1B Control Block", 0},
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (XPm2ControlBlock),        "PM2 Control Block", 0},
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (XPmTimerBlock),           "PM Timer Block", 0},
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (XGpe0Block),              "GPE0 Block", 0},
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (XGpe1Block),              "GPE1 Block", 0},
    ACPI_DMT_TERMINATOR
};

/* ACPI 5.0 Extensions (FADT version 5) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoFadt5[] =
{
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (SleepControl),            "Sleep Control Register", 0},
    {ACPI_DMT_GAS,      ACPI_FADT_OFFSET (SleepStatus),             "Sleep Status Register", 0},
    ACPI_DMT_TERMINATOR
};

/* ACPI 6.0 Extensions (FADT version 6) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoFadt6[] =
{
    {ACPI_DMT_UINT64,   ACPI_FADT_OFFSET (HypervisorId),            "Hypervisor ID", 0},
    ACPI_DMT_TERMINATOR
};
