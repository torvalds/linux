/******************************************************************************
 *
 * Module Name: ahtable - Table of known ACPI tables with descriptions
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


/* Local prototypes */

const AH_TABLE *
AcpiAhGetTableInfo (
    char                    *Signature);

extern const AH_TABLE      AcpiGbl_SupportedTables[];


/*******************************************************************************
 *
 * FUNCTION:    AcpiAhGetTableInfo
 *
 * PARAMETERS:  Signature           - ACPI signature (4 chars) to match
 *
 * RETURN:      Pointer to a valid AH_TABLE. Null if no match found.
 *
 * DESCRIPTION: Find a match in the "help" table of supported ACPI tables
 *
 ******************************************************************************/

const AH_TABLE *
AcpiAhGetTableInfo (
    char                    *Signature)
{
    const AH_TABLE      *Info;


    for (Info = AcpiGbl_SupportedTables; Info->Signature; Info++)
    {
        if (ACPI_COMPARE_NAME (Signature, Info->Signature))
        {
            return (Info);
        }
    }

    return (NULL);
}


/*
 * Note: Any tables added here should be duplicated within AcpiDmTableData
 * in the file common/dmtable.c
 */
const AH_TABLE      AcpiGbl_SupportedTables[] =
{
    {ACPI_SIG_ASF,  "Alert Standard Format table"},
    {ACPI_SIG_BERT, "Boot Error Record Table"},
    {ACPI_SIG_BGRT, "Boot Graphics Resource Table"},
    {ACPI_SIG_BOOT, "Simple Boot Flag Table"},
    {ACPI_SIG_CPEP, "Corrected Platform Error Polling table"},
    {ACPI_SIG_CSRT, "Core System Resource Table"},
    {ACPI_SIG_DBG2, "Debug Port table type 2"},
    {ACPI_SIG_DBGP, "Debug Port table"},
    {ACPI_SIG_DMAR, "DMA Remapping table"},
    {ACPI_SIG_DRTM, "Dynamic Root of Trust for Measurement table"},
    {ACPI_SIG_DSDT, "Differentiated System Description Table (AML table)"},
    {ACPI_SIG_ECDT, "Embedded Controller Boot Resources Table"},
    {ACPI_SIG_EINJ, "Error Injection table"},
    {ACPI_SIG_ERST, "Error Record Serialization Table"},
    {ACPI_SIG_FACS, "Firmware ACPI Control Structure"},
    {ACPI_SIG_FADT, "Fixed ACPI Description Table (FADT)"},
    {ACPI_SIG_FPDT, "Firmware Performance Data Table"},
    {ACPI_SIG_GTDT, "Generic Timer Description Table"},
    {ACPI_SIG_HEST, "Hardware Error Source Table"},
    {ACPI_SIG_HMAT, "Heterogeneous Memory Attributes Table"},
    {ACPI_SIG_HPET, "High Precision Event Timer table"},
    {ACPI_SIG_IORT, "IO Remapping Table"},
    {ACPI_SIG_IVRS, "I/O Virtualization Reporting Structure"},
    {ACPI_SIG_LPIT, "Low Power Idle Table"},
    {ACPI_SIG_MADT, "Multiple APIC Description Table (MADT)"},
    {ACPI_SIG_MCFG, "Memory Mapped Configuration table"},
    {ACPI_SIG_MCHI, "Management Controller Host Interface table"},
    {ACPI_SIG_MPST, "Memory Power State Table"},
    {ACPI_SIG_MSCT, "Maximum System Characteristics Table"},
    {ACPI_SIG_MSDM, "Microsoft Data Management table"},
    {ACPI_SIG_MTMR, "MID Timer Table"},
    {ACPI_SIG_NFIT, "NVDIMM Firmware Interface Table"},
    {ACPI_SIG_PCCT, "Platform Communications Channel Table"},
    {ACPI_SIG_PDTT, "Platform Debug Trigger Table"},
    {ACPI_SIG_PMTT, "Platform Memory Topology Table"},
    {ACPI_SIG_PPTT, "Processor Properties Topology Table"},
    {ACPI_SIG_RASF, "RAS Features Table"},
    {ACPI_RSDP_NAME,"Root System Description Pointer"},
    {ACPI_SIG_RSDT, "Root System Description Table"},
    {ACPI_SIG_S3PT, "S3 Performance Table"},
    {ACPI_SIG_SBST, "Smart Battery Specification Table"},
    {ACPI_SIG_SDEI, "Software Delegated Exception Interface Table"},
    {ACPI_SIG_SDEV, "Secure Devices table"},
    {ACPI_SIG_SLIC, "Software Licensing Description Table"},
    {ACPI_SIG_SLIT, "System Locality Information Table"},
    {ACPI_SIG_SPCR, "Serial Port Console Redirection table"},
    {ACPI_SIG_SPMI, "Server Platform Management Interface table"},
    {ACPI_SIG_SRAT, "System Resource Affinity Table"},
    {ACPI_SIG_SSDT, "Secondary System Description Table (AML table)"},
    {ACPI_SIG_STAO, "Status Override table"},
    {ACPI_SIG_TCPA, "Trusted Computing Platform Alliance table"},
    {ACPI_SIG_TPM2, "Trusted Platform Module hardware interface table"},
    {ACPI_SIG_UEFI, "UEFI Boot Optimization Table"},
    {ACPI_SIG_VRTC, "Virtual Real-Time Clock Table"},
    {ACPI_SIG_WAET, "Windows ACPI Emulated Devices Table"},
    {ACPI_SIG_WDAT, "Watchdog Action Table"},
    {ACPI_SIG_WDDT, "Watchdog Description Table"},
    {ACPI_SIG_WDRT, "Watchdog Resource Table"},
    {ACPI_SIG_WPBT, "Windows Platform Binary Table"},
    {ACPI_SIG_WSMT, "Windows SMM Security Migrations Table"},
    {ACPI_SIG_XENV, "Xen Environment table"},
    {ACPI_SIG_XSDT, "Extended System Description Table"},
    {NULL,          NULL}
};
