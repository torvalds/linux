/******************************************************************************
 *
 * Module Name: tbprint - Table output utilities
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
#include <contrib/dev/acpica/include/actables.h>

#define _COMPONENT          ACPI_TABLES
        ACPI_MODULE_NAME    ("tbprint")


/* Local prototypes */

static void
AcpiTbFixString (
    char                    *String,
    ACPI_SIZE               Length);

static void
AcpiTbCleanupTableHeader (
    ACPI_TABLE_HEADER       *OutHeader,
    ACPI_TABLE_HEADER       *Header);


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbFixString
 *
 * PARAMETERS:  String              - String to be repaired
 *              Length              - Maximum length
 *
 * RETURN:      None
 *
 * DESCRIPTION: Replace every non-printable or non-ascii byte in the string
 *              with a question mark '?'.
 *
 ******************************************************************************/

static void
AcpiTbFixString (
    char                    *String,
    ACPI_SIZE               Length)
{

    while (Length && *String)
    {
        if (!isprint ((int) *String))
        {
            *String = '?';
        }

        String++;
        Length--;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbCleanupTableHeader
 *
 * PARAMETERS:  OutHeader           - Where the cleaned header is returned
 *              Header              - Input ACPI table header
 *
 * RETURN:      Returns the cleaned header in OutHeader
 *
 * DESCRIPTION: Copy the table header and ensure that all "string" fields in
 *              the header consist of printable characters.
 *
 ******************************************************************************/

static void
AcpiTbCleanupTableHeader (
    ACPI_TABLE_HEADER       *OutHeader,
    ACPI_TABLE_HEADER       *Header)
{

    memcpy (OutHeader, Header, sizeof (ACPI_TABLE_HEADER));

    AcpiTbFixString (OutHeader->Signature, ACPI_NAME_SIZE);
    AcpiTbFixString (OutHeader->OemId, ACPI_OEM_ID_SIZE);
    AcpiTbFixString (OutHeader->OemTableId, ACPI_OEM_TABLE_ID_SIZE);
    AcpiTbFixString (OutHeader->AslCompilerId, ACPI_NAME_SIZE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbPrintTableHeader
 *
 * PARAMETERS:  Address             - Table physical address
 *              Header              - Table header
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print an ACPI table header. Special cases for FACS and RSDP.
 *
 ******************************************************************************/

void
AcpiTbPrintTableHeader (
    ACPI_PHYSICAL_ADDRESS   Address,
    ACPI_TABLE_HEADER       *Header)
{
    ACPI_TABLE_HEADER       LocalHeader;


    if (ACPI_COMPARE_NAME (Header->Signature, ACPI_SIG_FACS))
    {
        /* FACS only has signature and length fields */

        ACPI_INFO (("%-4.4s 0x%8.8X%8.8X %06X",
            Header->Signature, ACPI_FORMAT_UINT64 (Address),
            Header->Length));
    }
    else if (ACPI_VALIDATE_RSDP_SIG (Header->Signature))
    {
        /* RSDP has no common fields */

        memcpy (LocalHeader.OemId, ACPI_CAST_PTR (ACPI_TABLE_RSDP,
            Header)->OemId, ACPI_OEM_ID_SIZE);
        AcpiTbFixString (LocalHeader.OemId, ACPI_OEM_ID_SIZE);

        ACPI_INFO (("RSDP 0x%8.8X%8.8X %06X (v%.2d %-6.6s)",
            ACPI_FORMAT_UINT64 (Address),
            (ACPI_CAST_PTR (ACPI_TABLE_RSDP, Header)->Revision > 0) ?
                ACPI_CAST_PTR (ACPI_TABLE_RSDP, Header)->Length : 20,
            ACPI_CAST_PTR (ACPI_TABLE_RSDP, Header)->Revision,
            LocalHeader.OemId));
    }
    else
    {
        /* Standard ACPI table with full common header */

        AcpiTbCleanupTableHeader (&LocalHeader, Header);

        ACPI_INFO ((
            "%-4.4s 0x%8.8X%8.8X"
            " %06X (v%.2d %-6.6s %-8.8s %08X %-4.4s %08X)",
            LocalHeader.Signature, ACPI_FORMAT_UINT64 (Address),
            LocalHeader.Length, LocalHeader.Revision, LocalHeader.OemId,
            LocalHeader.OemTableId, LocalHeader.OemRevision,
            LocalHeader.AslCompilerId, LocalHeader.AslCompilerRevision));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbValidateChecksum
 *
 * PARAMETERS:  Table               - ACPI table to verify
 *              Length              - Length of entire table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Verifies that the table checksums to zero. Optionally returns
 *              exception on bad checksum.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiTbVerifyChecksum (
    ACPI_TABLE_HEADER       *Table,
    UINT32                  Length)
{
    UINT8                   Checksum;


    /*
     * FACS/S3PT:
     * They are the odd tables, have no standard ACPI header and no checksum
     */

    if (ACPI_COMPARE_NAME (Table->Signature, ACPI_SIG_S3PT) ||
        ACPI_COMPARE_NAME (Table->Signature, ACPI_SIG_FACS))
    {
        return (AE_OK);
    }

    /* Compute the checksum on the table */

    Checksum = AcpiTbChecksum (ACPI_CAST_PTR (UINT8, Table), Length);

    /* Checksum ok? (should be zero) */

    if (Checksum)
    {
        ACPI_BIOS_WARNING ((AE_INFO,
            "Incorrect checksum in table [%4.4s] - 0x%2.2X, "
            "should be 0x%2.2X",
            Table->Signature, Table->Checksum,
            (UINT8) (Table->Checksum - Checksum)));

#if (ACPI_CHECKSUM_ABORT)
        return (AE_BAD_CHECKSUM);
#endif
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbChecksum
 *
 * PARAMETERS:  Buffer          - Pointer to memory region to be checked
 *              Length          - Length of this memory region
 *
 * RETURN:      Checksum (UINT8)
 *
 * DESCRIPTION: Calculates circular checksum of memory region.
 *
 ******************************************************************************/

UINT8
AcpiTbChecksum (
    UINT8                   *Buffer,
    UINT32                  Length)
{
    UINT8                   Sum = 0;
    UINT8                   *End = Buffer + Length;


    while (Buffer < End)
    {
        Sum = (UINT8) (Sum + *(Buffer++));
    }

    return (Sum);
}
