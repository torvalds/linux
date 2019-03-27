/******************************************************************************
 *
 * Module Name: dmtbinfo3 - Table info for non-AML tables
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
        ACPI_MODULE_NAME    ("dmtbinfo3")

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
 * SLIC - Software Licensing Description Table. This table contains the standard
 * ACPI header followed by proprietary data structures
 *
 ******************************************************************************/

/* Single subtable, a proprietary format, so treat it as a buffer */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSlic[] =
{
    {ACPI_DMT_RAW_BUFFER, 0,                                        "Software Licensing Structure", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * SLIT - System Locality Information Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoSlit[] =
{
    {ACPI_DMT_UINT64,   ACPI_SLIT_OFFSET (LocalityCount),           "Localities", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * SPCR - Serial Port Console Redirection table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoSpcr[] =
{
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (InterfaceType),           "Interface Type", 0},
    {ACPI_DMT_UINT24,   ACPI_SPCR_OFFSET (Reserved[0]),             "Reserved", 0},
    {ACPI_DMT_GAS,      ACPI_SPCR_OFFSET (SerialPort),              "Serial Port Register", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (InterruptType),           "Interrupt Type", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (PcInterrupt),             "PCAT-compatible IRQ", 0},
    {ACPI_DMT_UINT32,   ACPI_SPCR_OFFSET (Interrupt),               "Interrupt", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (BaudRate),                "Baud Rate", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (Parity),                  "Parity", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (StopBits),                "Stop Bits", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (FlowControl),             "Flow Control", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (TerminalType),            "Terminal Type", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (Reserved2),               "Reserved", 0},
    {ACPI_DMT_UINT16,   ACPI_SPCR_OFFSET (PciDeviceId),             "PCI Device ID", 0},
    {ACPI_DMT_UINT16,   ACPI_SPCR_OFFSET (PciVendorId),             "PCI Vendor ID", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (PciBus),                  "PCI Bus", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (PciDevice),               "PCI Device", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (PciFunction),             "PCI Function", 0},
    {ACPI_DMT_UINT32,   ACPI_SPCR_OFFSET (PciFlags),                "PCI Flags", 0},
    {ACPI_DMT_UINT8,    ACPI_SPCR_OFFSET (PciSegment),              "PCI Segment", 0},
    {ACPI_DMT_UINT32,   ACPI_SPCR_OFFSET (Reserved2),               "Reserved", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * SPMI - Server Platform Management Interface table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoSpmi[] =
{
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (InterfaceType),           "Interface Type", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (Reserved),                "Reserved", DT_NON_ZERO}, /* Value must be 1 */
    {ACPI_DMT_UINT16,   ACPI_SPMI_OFFSET (SpecRevision),            "IPMI Spec Version", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (InterruptType),           "Interrupt Type", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (GpeNumber),               "GPE Number", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (Reserved1),               "Reserved", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (PciDeviceFlag),           "PCI Device Flag", 0},
    {ACPI_DMT_UINT32,   ACPI_SPMI_OFFSET (Interrupt),               "Interrupt", 0},
    {ACPI_DMT_GAS,      ACPI_SPMI_OFFSET (IpmiRegister),            "IPMI Register", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (PciSegment),              "PCI Segment", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (PciBus),                  "PCI Bus", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (PciDevice),               "PCI Device", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (PciFunction),             "PCI Function", 0},
    {ACPI_DMT_UINT8,    ACPI_SPMI_OFFSET (Reserved2),               "Reserved", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * SRAT - System Resource Affinity Table and Subtables
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoSrat[] =
{
    {ACPI_DMT_UINT32,   ACPI_SRAT_OFFSET (TableRevision),           "Table Revision", 0},
    {ACPI_DMT_UINT64,   ACPI_SRAT_OFFSET (Reserved),                "Reserved", 0},
    ACPI_DMT_TERMINATOR
};

/* Common Subtable header (one per Subtable) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSratHdr[] =
{
    {ACPI_DMT_SRAT,     ACPI_SRATH_OFFSET (Type),                   "Subtable Type", 0},
    {ACPI_DMT_UINT8,    ACPI_SRATH_OFFSET (Length),                 "Length", DT_LENGTH},
    ACPI_DMT_TERMINATOR
};

/* SRAT Subtables */

/* 0: Processor Local APIC/SAPIC Affinity */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSrat0[] =
{
    {ACPI_DMT_UINT8,    ACPI_SRAT0_OFFSET (ProximityDomainLo),      "Proximity Domain Low(8)", 0},
    {ACPI_DMT_UINT8,    ACPI_SRAT0_OFFSET (ApicId),                 "Apic ID", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT0_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_SRAT0_FLAG_OFFSET (Flags,0),           "Enabled", 0},
    {ACPI_DMT_UINT8,    ACPI_SRAT0_OFFSET (LocalSapicEid),          "Local Sapic EID", 0},
    {ACPI_DMT_UINT24,   ACPI_SRAT0_OFFSET (ProximityDomainHi[0]),   "Proximity Domain High(24)", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT0_OFFSET (ClockDomain),            "Clock Domain", 0},
    ACPI_DMT_TERMINATOR
};

/* 1: Memory Affinity */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSrat1[] =
{
    {ACPI_DMT_UINT32,   ACPI_SRAT1_OFFSET (ProximityDomain),        "Proximity Domain", 0},
    {ACPI_DMT_UINT16,   ACPI_SRAT1_OFFSET (Reserved),               "Reserved1", 0},
    {ACPI_DMT_UINT64,   ACPI_SRAT1_OFFSET (BaseAddress),            "Base Address", 0},
    {ACPI_DMT_UINT64,   ACPI_SRAT1_OFFSET (Length),                 "Address Length", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT1_OFFSET (Reserved1),              "Reserved2", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT1_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_SRAT1_FLAG_OFFSET (Flags,0),           "Enabled", 0},
    {ACPI_DMT_FLAG1,    ACPI_SRAT1_FLAG_OFFSET (Flags,0),           "Hot Pluggable", 0},
    {ACPI_DMT_FLAG2,    ACPI_SRAT1_FLAG_OFFSET (Flags,0),           "Non-Volatile", 0},
    {ACPI_DMT_UINT64,   ACPI_SRAT1_OFFSET (Reserved2),              "Reserved3", 0},
    ACPI_DMT_TERMINATOR
};

/* 2: Processor Local X2_APIC Affinity (ACPI 4.0) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSrat2[] =
{
    {ACPI_DMT_UINT16,   ACPI_SRAT2_OFFSET (Reserved),               "Reserved1", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT2_OFFSET (ProximityDomain),        "Proximity Domain", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT2_OFFSET (ApicId),                 "Apic ID", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT2_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_SRAT2_FLAG_OFFSET (Flags,0),           "Enabled", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT2_OFFSET (ClockDomain),            "Clock Domain", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT2_OFFSET (Reserved2),              "Reserved2", 0},
    ACPI_DMT_TERMINATOR
};

/* 3: GICC Affinity (ACPI 5.1) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSrat3[] =
{
    {ACPI_DMT_UINT32,   ACPI_SRAT3_OFFSET (ProximityDomain),        "Proximity Domain", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT3_OFFSET (AcpiProcessorUid),       "Acpi Processor UID", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT3_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_SRAT3_FLAG_OFFSET (Flags,0),           "Enabled", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT3_OFFSET (ClockDomain),            "Clock Domain", 0},
    ACPI_DMT_TERMINATOR
};

/* 4: GCC ITS Affinity (ACPI 6.2) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSrat4[] =
{
    {ACPI_DMT_UINT32,   ACPI_SRAT4_OFFSET (ProximityDomain),        "Proximity Domain", 0},
    {ACPI_DMT_UINT16,   ACPI_SRAT4_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT4_OFFSET (ItsId),                  "ITS ID", 0},
    ACPI_DMT_TERMINATOR
};

/* 5: Generic Initiator Affinity Structure (ACPI 6.3) */

ACPI_DMTABLE_INFO           AcpiDmTableInfoSrat5[] =
{
    {ACPI_DMT_UINT8,    ACPI_SRAT5_OFFSET (Reserved),               "Reserved1", 0},
    {ACPI_DMT_UINT8,    ACPI_SRAT5_OFFSET (DeviceHandleType),       "Device Handle Type", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT5_OFFSET (ProximityDomain),        "Proximity Domain", 0},
    {ACPI_DMT_BUF16,    ACPI_SRAT5_OFFSET (DeviceHandle),           "Device Handle", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT5_OFFSET (Flags),                  "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_SRAT5_FLAG_OFFSET (Flags,0),           "Enabled", 0},
    {ACPI_DMT_UINT32,   ACPI_SRAT5_OFFSET (Reserved1),              "Reserved2", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * STAO - Status Override Table (_STA override) - ACPI 6.0
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoStao[] =
{
    {ACPI_DMT_UINT8,    ACPI_STAO_OFFSET (IgnoreUart),              "Ignore UART", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoStaoStr[] =
{
    {ACPI_DMT_STRING,   0,                                          "Namepath", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * TCPA - Trusted Computing Platform Alliance table (Client)
 *
 * NOTE: There are two versions of the table with the same signature --
 * the client version and the server version. The common PlatformClass
 * field is used to differentiate the two types of tables.
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoTcpaHdr[] =
{
    {ACPI_DMT_UINT16,   ACPI_TCPA_OFFSET (PlatformClass),           "Platform Class", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoTcpaClient[] =
{
    {ACPI_DMT_UINT32,   ACPI_TCPA_CLIENT_OFFSET (MinimumLogLength), "Min Event Log Length", 0},
    {ACPI_DMT_UINT64,   ACPI_TCPA_CLIENT_OFFSET (LogAddress),       "Event Log Address", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoTcpaServer[] =
{
    {ACPI_DMT_UINT16,   ACPI_TCPA_SERVER_OFFSET (Reserved),         "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_TCPA_SERVER_OFFSET (MinimumLogLength), "Min Event Log Length", 0},
    {ACPI_DMT_UINT64,   ACPI_TCPA_SERVER_OFFSET (LogAddress),       "Event Log Address", 0},
    {ACPI_DMT_UINT16,   ACPI_TCPA_SERVER_OFFSET (SpecRevision),     "Specification Revision", 0},
    {ACPI_DMT_UINT8,    ACPI_TCPA_SERVER_OFFSET (DeviceFlags),      "Device Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_TCPA_SERVER_OFFSET (DeviceFlags),      "Pci Device", 0},
    {ACPI_DMT_FLAG1,    ACPI_TCPA_SERVER_OFFSET (DeviceFlags),      "Bus is Pnp", 0},
    {ACPI_DMT_FLAG2,    ACPI_TCPA_SERVER_OFFSET (DeviceFlags),      "Address Valid", 0},
    {ACPI_DMT_UINT8,    ACPI_TCPA_SERVER_OFFSET (InterruptFlags),   "Interrupt Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_TCPA_SERVER_OFFSET (InterruptFlags),   "Mode", 0},
    {ACPI_DMT_FLAG1,    ACPI_TCPA_SERVER_OFFSET (InterruptFlags),   "Polarity", 0},
    {ACPI_DMT_FLAG2,    ACPI_TCPA_SERVER_OFFSET (InterruptFlags),   "GPE SCI Triggered", 0},
    {ACPI_DMT_FLAG3,    ACPI_TCPA_SERVER_OFFSET (InterruptFlags),   "Global System Interrupt", 0},
    {ACPI_DMT_UINT8,    ACPI_TCPA_SERVER_OFFSET (GpeNumber),        "Gpe Number", 0},
    {ACPI_DMT_UINT24,   ACPI_TCPA_SERVER_OFFSET (Reserved2[0]),     "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_TCPA_SERVER_OFFSET (GlobalInterrupt),  "Global Interrupt", 0},
    {ACPI_DMT_GAS,      ACPI_TCPA_SERVER_OFFSET (Address),          "Address", 0},
    {ACPI_DMT_UINT32,   ACPI_TCPA_SERVER_OFFSET (Reserved3),        "Reserved", 0},
    {ACPI_DMT_GAS,      ACPI_TCPA_SERVER_OFFSET (ConfigAddress),    "Configuration Address", 0},
    {ACPI_DMT_UINT8,    ACPI_TCPA_SERVER_OFFSET (Group),            "Pci Group", 0},
    {ACPI_DMT_UINT8,    ACPI_TCPA_SERVER_OFFSET (Bus),              "Pci Bus", 0},
    {ACPI_DMT_UINT8,    ACPI_TCPA_SERVER_OFFSET (Device),           "Pci Device", 0},
    {ACPI_DMT_UINT8,    ACPI_TCPA_SERVER_OFFSET (Function),         "Pci Function", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * TPM2 - Trusted Platform Module (TPM) 2.0 Hardware Interface Table
 *
 ******************************************************************************/

/* TPM2 revision 3 */

ACPI_DMTABLE_INFO           AcpiDmTableInfoTpm23[] =
{
    {ACPI_DMT_UINT32,   ACPI_TPM23_OFFSET (Reserved),           "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_TPM23_OFFSET (ControlAddress),     "Control Address", 0},
    {ACPI_DMT_UINT32,   ACPI_TPM23_OFFSET (StartMethod),        "Start Method", 0},
    ACPI_DMT_TERMINATOR
};

/* Trailer in the case that StartMethod == 2 */

ACPI_DMTABLE_INFO           AcpiDmTableInfoTpm23a[] =
{
    {ACPI_DMT_UINT32,   ACPI_TPM23A_OFFSET (Reserved),          "Reserved", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

/* TPM2 revision 4 */

ACPI_DMTABLE_INFO           AcpiDmTableInfoTpm2[] =
{
    {ACPI_DMT_UINT16,   ACPI_TPM2_OFFSET (PlatformClass),           "Platform Class", 0},
    {ACPI_DMT_UINT16,   ACPI_TPM2_OFFSET (Reserved),                "Reserved", 0},
    {ACPI_DMT_UINT64,   ACPI_TPM2_OFFSET (ControlAddress),          "Control Address", 0},
    {ACPI_DMT_TPM2,     ACPI_TPM2_OFFSET (StartMethod),             "Start Method", 0},
    ACPI_DMT_TERMINATOR
};

/* Optional trailer. LogLength and LogAddress are additionally optional */

ACPI_DMTABLE_INFO           AcpiDmTableInfoTpm2a[] =
{
    {ACPI_DMT_BUF12,    ACPI_TPM2A_OFFSET (MethodParameters),       "Method Parameters", DT_OPTIONAL},
    {ACPI_DMT_UINT32,   ACPI_TPM2A_OFFSET (MinimumLogLength),       "Minimum Log Length", DT_OPTIONAL},
    {ACPI_DMT_UINT64,   ACPI_TPM2A_OFFSET (LogAddress),             "Log Address", DT_OPTIONAL},
    ACPI_DMT_TERMINATOR
};

/* 11: Start Method for ARM SMC */

ACPI_DMTABLE_INFO           AcpiDmTableInfoTpm211[] =
{
    {ACPI_DMT_UINT32,   ACPI_TPM211_OFFSET (GlobalInterrupt),       "Global Interrupt", 0},
    {ACPI_DMT_UINT8,    ACPI_TPM211_OFFSET (InterruptFlags),        "Interrupt Flags", 0},
    {ACPI_DMT_UINT8,    ACPI_TPM211_OFFSET (OperationFlags),        "Operation Flags", 0},
    {ACPI_DMT_UINT16,   ACPI_TPM211_OFFSET (Reserved),              "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_TPM211_OFFSET (FunctionId),            "Function ID", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * UEFI - UEFI Boot optimization Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoUefi[] =
{
    {ACPI_DMT_UUID,     ACPI_UEFI_OFFSET (Identifier[0]),           "UUID Identifier", 0},
    {ACPI_DMT_UINT16,   ACPI_UEFI_OFFSET (DataOffset),              "Data Offset", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * VRTC - Virtual Real Time Clock Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoVrtc[] =
{
    ACPI_DMT_TERMINATOR
};

/* VRTC Subtables - VRTC Entry */

ACPI_DMTABLE_INFO           AcpiDmTableInfoVrtc0[] =
{
    {ACPI_DMT_GAS,      ACPI_VRTC0_OFFSET (PhysicalAddress),        "PhysicalAddress", 0},
    {ACPI_DMT_UINT32,   ACPI_VRTC0_OFFSET (Irq),                    "IRQ", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * WAET - Windows ACPI Emulated devices Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoWaet[] =
{
    {ACPI_DMT_UINT32,   ACPI_WAET_OFFSET (Flags),                   "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_WAET_OFFSET (Flags),                   "RTC needs no INT ack", 0},
    {ACPI_DMT_FLAG1,    ACPI_WAET_OFFSET (Flags),                   "PM timer, one read only", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * WDAT - Watchdog Action Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoWdat[] =
{
    {ACPI_DMT_UINT32,   ACPI_WDAT_OFFSET (HeaderLength),            "Header Length", DT_LENGTH},
    {ACPI_DMT_UINT16,   ACPI_WDAT_OFFSET (PciSegment),              "PCI Segment", 0},
    {ACPI_DMT_UINT8,    ACPI_WDAT_OFFSET (PciBus),                  "PCI Bus", 0},
    {ACPI_DMT_UINT8,    ACPI_WDAT_OFFSET (PciDevice),               "PCI Device", 0},
    {ACPI_DMT_UINT8,    ACPI_WDAT_OFFSET (PciFunction),             "PCI Function", 0},
    {ACPI_DMT_UINT24,   ACPI_WDAT_OFFSET (Reserved[0]),             "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_WDAT_OFFSET (TimerPeriod),             "Timer Period", 0},
    {ACPI_DMT_UINT32,   ACPI_WDAT_OFFSET (MaxCount),                "Max Count", 0},
    {ACPI_DMT_UINT32,   ACPI_WDAT_OFFSET (MinCount),                "Min Count", 0},
    {ACPI_DMT_UINT8,    ACPI_WDAT_OFFSET (Flags),                   "Flags (decoded below)", DT_FLAG},
    {ACPI_DMT_FLAG0,    ACPI_WDAT_OFFSET (Flags),                   "Enabled", 0},
    {ACPI_DMT_FLAG7,    ACPI_WDAT_OFFSET (Flags),                   "Stopped When Asleep", 0},
    {ACPI_DMT_UINT24,   ACPI_WDAT_OFFSET (Reserved2[0]),            "Reserved", 0},
    {ACPI_DMT_UINT32,   ACPI_WDAT_OFFSET (Entries),                 "Watchdog Entry Count", 0},
    ACPI_DMT_TERMINATOR
};

/* WDAT Subtables - Watchdog Instruction Entries */

ACPI_DMTABLE_INFO           AcpiDmTableInfoWdat0[] =
{
    {ACPI_DMT_UINT8,    ACPI_WDAT0_OFFSET (Action),                 "Watchdog Action", 0},
    {ACPI_DMT_UINT8,    ACPI_WDAT0_OFFSET (Instruction),            "Instruction", 0},
    {ACPI_DMT_UINT16,   ACPI_WDAT0_OFFSET (Reserved),               "Reserved", 0},
    {ACPI_DMT_GAS,      ACPI_WDAT0_OFFSET (RegisterRegion),         "Register Region", 0},
    {ACPI_DMT_UINT32,   ACPI_WDAT0_OFFSET (Value),                  "Value", 0},
    {ACPI_DMT_UINT32,   ACPI_WDAT0_OFFSET (Mask),                   "Register Mask", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * WDDT - Watchdog Description Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoWddt[] =
{
    {ACPI_DMT_UINT16,   ACPI_WDDT_OFFSET (SpecVersion),             "Specification Version", 0},
    {ACPI_DMT_UINT16,   ACPI_WDDT_OFFSET (TableVersion),            "Table Version", 0},
    {ACPI_DMT_UINT16,   ACPI_WDDT_OFFSET (PciVendorId),             "PCI Vendor ID", 0},
    {ACPI_DMT_GAS,      ACPI_WDDT_OFFSET (Address),                 "Timer Register", 0},
    {ACPI_DMT_UINT16,   ACPI_WDDT_OFFSET (MaxCount),                "Max Count", 0},
    {ACPI_DMT_UINT16,   ACPI_WDDT_OFFSET (MinCount),                "Min Count", 0},
    {ACPI_DMT_UINT16,   ACPI_WDDT_OFFSET (Period),                  "Period", 0},
    {ACPI_DMT_UINT16,   ACPI_WDDT_OFFSET (Status),                  "Status (decoded below)", 0},

    /* Status Flags byte 0 */

    {ACPI_DMT_FLAG0,    ACPI_WDDT_FLAG_OFFSET (Status,0),           "Available", 0},
    {ACPI_DMT_FLAG1,    ACPI_WDDT_FLAG_OFFSET (Status,0),           "Active", 0},
    {ACPI_DMT_FLAG2,    ACPI_WDDT_FLAG_OFFSET (Status,0),           "OS Owns", 0},

    /* Status Flags byte 1 */

    {ACPI_DMT_FLAG3,    ACPI_WDDT_FLAG_OFFSET (Status,1),           "User Reset", 0},
    {ACPI_DMT_FLAG4,    ACPI_WDDT_FLAG_OFFSET (Status,1),           "Timeout Reset", 0},
    {ACPI_DMT_FLAG5,    ACPI_WDDT_FLAG_OFFSET (Status,1),           "Power Fail Reset", 0},
    {ACPI_DMT_FLAG6,    ACPI_WDDT_FLAG_OFFSET (Status,1),           "Unknown Reset", 0},

    {ACPI_DMT_UINT16,   ACPI_WDDT_OFFSET (Capability),              "Capability (decoded below)", 0},

    /* Capability Flags byte 0 */

    {ACPI_DMT_FLAG0,    ACPI_WDDT_FLAG_OFFSET (Capability,0),       "Auto Reset", 0},
    {ACPI_DMT_FLAG1,    ACPI_WDDT_FLAG_OFFSET (Capability,0),       "Timeout Alert", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * WDRT - Watchdog Resource Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoWdrt[] =
{
    {ACPI_DMT_GAS,      ACPI_WDRT_OFFSET (ControlRegister),         "Control Register", 0},
    {ACPI_DMT_GAS,      ACPI_WDRT_OFFSET (CountRegister),           "Count Register", 0},
    {ACPI_DMT_UINT16,   ACPI_WDRT_OFFSET (PciDeviceId),             "PCI Device ID", 0},
    {ACPI_DMT_UINT16,   ACPI_WDRT_OFFSET (PciVendorId),             "PCI Vendor ID", 0},
    {ACPI_DMT_UINT8,    ACPI_WDRT_OFFSET (PciBus),                  "PCI Bus", 0},
    {ACPI_DMT_UINT8,    ACPI_WDRT_OFFSET (PciDevice),               "PCI Device", 0},
    {ACPI_DMT_UINT8,    ACPI_WDRT_OFFSET (PciFunction),             "PCI Function", 0},
    {ACPI_DMT_UINT8,    ACPI_WDRT_OFFSET (PciSegment),              "PCI Segment", 0},
    {ACPI_DMT_UINT16,   ACPI_WDRT_OFFSET (MaxCount),                "Max Count", 0},
    {ACPI_DMT_UINT8,    ACPI_WDRT_OFFSET (Units),                   "Counter Units", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * WPBT - Windows Platform Environment Table (ACPI 6.0)
 *        Version 1
 *
 * Conforms to "Windows Platform Binary Table (WPBT)" 29 November 2011
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoWpbt[] =
{
    {ACPI_DMT_UINT32,      ACPI_WPBT_OFFSET (HandoffSize),          "Handoff Size", 0},
    {ACPI_DMT_UINT64,      ACPI_WPBT_OFFSET (HandoffAddress),       "Handoff Address", 0},
    {ACPI_DMT_UINT8,       ACPI_WPBT_OFFSET (Layout),               "Layout", 0},
    {ACPI_DMT_UINT8,       ACPI_WPBT_OFFSET (Type),                 "Type", 0},
    {ACPI_DMT_UINT16,      ACPI_WPBT_OFFSET (ArgumentsLength),      "Arguments Length", 0},
    ACPI_DMT_TERMINATOR
};

ACPI_DMTABLE_INFO           AcpiDmTableInfoWpbt0[] =
{
    {ACPI_DMT_UNICODE,     sizeof (ACPI_TABLE_WPBT),                "Command-line Arguments", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * WSMT - Windows SMM Security Migrations Table
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoWsmt[] =
{
    {ACPI_DMT_UINT32,   ACPI_WSMT_OFFSET (ProtectionFlags),         "Protection Flags", 0},
    {ACPI_DMT_FLAG0,    ACPI_WSMT_FLAG_OFFSET (ProtectionFlags,0),  "FIXED_COMM_BUFFERS", 0},
    {ACPI_DMT_FLAG1,    ACPI_WSMT_FLAG_OFFSET (ProtectionFlags,0),  "COMM_BUFFER_NESTED_PTR_PROTECTION", 0},
    {ACPI_DMT_FLAG2,    ACPI_WSMT_FLAG_OFFSET (ProtectionFlags,0),  "SYSTEM_RESOURCE_PROTECTION", 0},
    ACPI_DMT_TERMINATOR
};


/*******************************************************************************
 *
 * XENV -  Xen Environment table (ACPI 6.0)
 *
 ******************************************************************************/

ACPI_DMTABLE_INFO           AcpiDmTableInfoXenv[] =
{
    {ACPI_DMT_UINT64,   ACPI_XENV_OFFSET (GrantTableAddress),       "Grant Table Address", 0},
    {ACPI_DMT_UINT64,   ACPI_XENV_OFFSET (GrantTableSize),          "Grant Table Size", 0},
    {ACPI_DMT_UINT32,   ACPI_XENV_OFFSET (EventInterrupt),          "Event Interrupt", 0},
    {ACPI_DMT_UINT8,    ACPI_XENV_OFFSET (EventFlags),              "Event Flags", 0},
    ACPI_DMT_TERMINATOR
};


/*! [Begin] no source code translation */

/*
 * Generic types (used in UEFI and custom tables)
 *
 * Examples:
 *
 *     Buffer : cc 04 ff bb
 *      UINT8 : 11
 *     UINT16 : 1122
 *     UINT24 : 112233
 *     UINT32 : 11223344
 *     UINT56 : 11223344556677
 *     UINT64 : 1122334455667788
 *
 *     String : "This is string"
 *    Unicode : "This string encoded to Unicode"
 *
 *       GUID : 11223344-5566-7788-99aa-bbccddeeff00
 * DevicePath : "\PciRoot(0)\Pci(0x1f,1)\Usb(0,0)"
 */

#define ACPI_DM_GENERIC_ENTRY(FieldType, FieldName) \
    {{FieldType, 0, FieldName, 0}, ACPI_DMT_TERMINATOR}

ACPI_DMTABLE_INFO           AcpiDmTableInfoGeneric[][2] =
{
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UINT8,      "UINT8"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UINT16,     "UINT16"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UINT24,     "UINT24"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UINT32,     "UINT32"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UINT40,     "UINT40"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UINT48,     "UINT48"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UINT56,     "UINT56"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UINT64,     "UINT64"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_STRING,     "String"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UNICODE,    "Unicode"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_BUFFER,     "Buffer"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_UUID,       "GUID"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_STRING,     "DevicePath"),
    ACPI_DM_GENERIC_ENTRY (ACPI_DMT_LABEL,      "Label"),
    {ACPI_DMT_TERMINATOR}
};
/*! [End] no source code translation !*/
