/******************************************************************************
 *
 * Module Name: dmtbdump3 - Dump ACPI data tables that contain no AML code
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
#include <contrib/dev/acpica/include/actables.h>

/* This module used for application-level code only */

#define _COMPONENT          ACPI_CA_DISASSEMBLER
        ACPI_MODULE_NAME    ("dmtbdump3")


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpSlic
 *
 * PARAMETERS:  Table               - A SLIC table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a SLIC
 *
 ******************************************************************************/

void
AcpiDmDumpSlic (
    ACPI_TABLE_HEADER       *Table)
{

    (void) AcpiDmDumpTable (Table->Length, sizeof (ACPI_TABLE_HEADER), Table,
        Table->Length - sizeof (*Table), AcpiDmTableInfoSlic);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpSlit
 *
 * PARAMETERS:  Table               - An SLIT
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a SLIT
 *
 ******************************************************************************/

void
AcpiDmDumpSlit (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset;
    UINT8                   *Row;
    UINT32                  Localities;
    UINT32                  i;
    UINT32                  j;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoSlit);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Display the Locality NxN Matrix */

    Localities = (UINT32) ACPI_CAST_PTR (ACPI_TABLE_SLIT, Table)->LocalityCount;
    Offset = ACPI_OFFSET (ACPI_TABLE_SLIT, Entry[0]);
    Row = (UINT8 *) ACPI_CAST_PTR (ACPI_TABLE_SLIT, Table)->Entry;

    for (i = 0; i < Localities; i++)
    {
        /* Display one row of the matrix */

        AcpiDmLineHeader2 (Offset, Localities, "Locality", i);
        for  (j = 0; j < Localities; j++)
        {
            /* Check for beyond EOT */

            if (Offset >= Table->Length)
            {
                AcpiOsPrintf (
                    "\n**** Not enough room in table for all localities\n");
                return;
            }

            AcpiOsPrintf ("%2.2X", Row[j]);
            Offset++;

            /* Display up to 16 bytes per output row */

            if ((j+1) < Localities)
            {
                AcpiOsPrintf (" ");

                if (j && (((j+1) % 16) == 0))
                {
                    AcpiOsPrintf ("\\\n"); /* With line continuation char */
                    AcpiDmLineHeader (Offset, 0, NULL);
                }
            }
        }

        /* Point to next row */

        AcpiOsPrintf ("\n");
        Row += Localities;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpSrat
 *
 * PARAMETERS:  Table               - A SRAT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a SRAT
 *
 ******************************************************************************/

void
AcpiDmDumpSrat (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_SRAT);
    ACPI_SUBTABLE_HEADER    *Subtable;
    ACPI_DMTABLE_INFO       *InfoTable;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoSrat);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_SUBTABLE_HEADER, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, AcpiDmTableInfoSratHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        switch (Subtable->Type)
        {
        case ACPI_SRAT_TYPE_CPU_AFFINITY:

            InfoTable = AcpiDmTableInfoSrat0;
            break;

        case ACPI_SRAT_TYPE_MEMORY_AFFINITY:

            InfoTable = AcpiDmTableInfoSrat1;
            break;

        case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY:

            InfoTable = AcpiDmTableInfoSrat2;
            break;

        case ACPI_SRAT_TYPE_GICC_AFFINITY:

            InfoTable = AcpiDmTableInfoSrat3;
            break;

        case ACPI_SRAT_TYPE_GIC_ITS_AFFINITY:

            InfoTable = AcpiDmTableInfoSrat4;
            break;

        case ACPI_SRAT_TYPE_GENERIC_AFFINITY:

            InfoTable = AcpiDmTableInfoSrat5;
            break;

        default:
            AcpiOsPrintf ("\n**** Unknown SRAT subtable type 0x%X\n",
                Subtable->Type);

            /* Attempt to continue */

            if (!Subtable->Length)
            {
                AcpiOsPrintf ("Invalid zero length subtable\n");
                return;
            }
            goto NextSubtable;
        }

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

NextSubtable:
        /* Point to next subtable */

        Offset += Subtable->Length;
        Subtable = ACPI_ADD_PTR (ACPI_SUBTABLE_HEADER, Subtable,
            Subtable->Length);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpStao
 *
 * PARAMETERS:  Table               - A STAO table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a STAO. This is a variable-length
 *              table that contains an open-ended number of ASCII strings
 *              at the end of the table.
 *
 ******************************************************************************/

void
AcpiDmDumpStao (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    char                    *Namepath;
    UINT32                  Length = Table->Length;
    UINT32                  StringLength;
    UINT32                  Offset = sizeof (ACPI_TABLE_STAO);


    /* Main table */

    Status = AcpiDmDumpTable (Length, 0, Table, 0, AcpiDmTableInfoStao);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* The rest of the table consists of Namepath strings */

    while (Offset < Table->Length)
    {
        Namepath = ACPI_ADD_PTR (char, Table, Offset);
        StringLength = strlen (Namepath) + 1;

        AcpiDmLineHeader (Offset, StringLength, "Namestring");
        AcpiOsPrintf ("\"%s\"\n", Namepath);

        /* Point to next namepath */

        Offset += StringLength;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpTcpa
 *
 * PARAMETERS:  Table               - A TCPA table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a TCPA.
 *
 * NOTE:        There are two versions of the table with the same signature:
 *              the client version and the server version. The common
 *              PlatformClass field is used to differentiate the two types of
 *              tables.
 *
 ******************************************************************************/

void
AcpiDmDumpTcpa (
    ACPI_TABLE_HEADER       *Table)
{
    UINT32                  Offset = sizeof (ACPI_TABLE_TCPA_HDR);
    ACPI_TABLE_TCPA_HDR     *CommonHeader = ACPI_CAST_PTR (
                                ACPI_TABLE_TCPA_HDR, Table);
    ACPI_TABLE_TCPA_HDR     *Subtable = ACPI_ADD_PTR (
                                ACPI_TABLE_TCPA_HDR, Table, Offset);
    ACPI_STATUS             Status;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table,
        0, AcpiDmTableInfoTcpaHdr);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /*
     * Examine the PlatformClass field to determine the table type.
     * Either a client or server table. Only one.
     */
    switch (CommonHeader->PlatformClass)
    {
    case ACPI_TCPA_CLIENT_TABLE:

        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Table->Length - Offset, AcpiDmTableInfoTcpaClient);
        break;

    case ACPI_TCPA_SERVER_TABLE:

        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Table->Length - Offset, AcpiDmTableInfoTcpaServer);
        break;

    default:

        AcpiOsPrintf ("\n**** Unknown TCPA Platform Class 0x%X\n",
            CommonHeader->PlatformClass);
        Status = AE_ERROR;
        break;
    }

    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("\n**** Cannot disassemble TCPA table\n");
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpTpm2
 *
 * PARAMETERS:  Table               - A TPM2 table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a TPM2.
 *
 ******************************************************************************/
static void
AcpiDmDumpTpm2Rev3 (
    ACPI_TABLE_HEADER       *Table)
{
    UINT32                  Offset = sizeof (ACPI_TABLE_TPM23);
    ACPI_TABLE_TPM23        *CommonHeader = ACPI_CAST_PTR (ACPI_TABLE_TPM23, Table);
    ACPI_TPM23_TRAILER      *Subtable = ACPI_ADD_PTR (ACPI_TPM23_TRAILER, Table, Offset);
    ACPI_STATUS             Status;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoTpm23);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Optional subtable if start method is ACPI start method */

    switch (CommonHeader->StartMethod)
    {
    case ACPI_TPM23_ACPI_START_METHOD:

        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Table->Length - Offset, AcpiDmTableInfoTpm23a);
        break;

    default:
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpTpm2
 *
 * PARAMETERS:  Table               - A TPM2 table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a TPM2.
 *
 ******************************************************************************/

void
AcpiDmDumpTpm2 (
    ACPI_TABLE_HEADER       *Table)
{
    UINT32                  Offset = sizeof (ACPI_TABLE_TPM2);
    ACPI_TABLE_TPM2         *CommonHeader = ACPI_CAST_PTR (ACPI_TABLE_TPM2, Table);
    ACPI_TPM2_TRAILER       *Subtable = ACPI_ADD_PTR (ACPI_TPM2_TRAILER, Table, Offset);
    ACPI_TPM2_ARM_SMC       *ArmSubtable;
    ACPI_STATUS             Status;


    if (Table->Revision == 3)
    {
        AcpiDmDumpTpm2Rev3(Table);
        return;
    }

    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoTpm2);

    if (ACPI_FAILURE (Status))
    {
        return;
    }

    AcpiOsPrintf ("\n");
    Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
        Table->Length - Offset, AcpiDmTableInfoTpm2a);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    switch (CommonHeader->StartMethod)
    {
    case ACPI_TPM2_COMMAND_BUFFER_WITH_ARM_SMC:

        ArmSubtable = ACPI_ADD_PTR (ACPI_TPM2_ARM_SMC, Subtable,
            sizeof (ACPI_TPM2_TRAILER));
        Offset += sizeof (ACPI_TPM2_TRAILER);

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, ArmSubtable,
            Table->Length - Offset, AcpiDmTableInfoTpm211);
        break;

    default:
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpVrtc
 *
 * PARAMETERS:  Table               - A VRTC table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a VRTC
 *
 ******************************************************************************/

void
AcpiDmDumpVrtc (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_VRTC);
    ACPI_VRTC_ENTRY         *Subtable;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoVrtc);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_VRTC_ENTRY, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            sizeof (ACPI_VRTC_ENTRY), AcpiDmTableInfoVrtc0);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Point to next subtable */

        Offset += sizeof (ACPI_VRTC_ENTRY);
        Subtable = ACPI_ADD_PTR (ACPI_VRTC_ENTRY, Subtable,
            sizeof (ACPI_VRTC_ENTRY));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpWdat
 *
 * PARAMETERS:  Table               - A WDAT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a WDAT
 *
 ******************************************************************************/

void
AcpiDmDumpWdat (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_WDAT);
    ACPI_WDAT_ENTRY         *Subtable;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoWdat);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_WDAT_ENTRY, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            sizeof (ACPI_WDAT_ENTRY), AcpiDmTableInfoWdat0);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Point to next subtable */

        Offset += sizeof (ACPI_WDAT_ENTRY);
        Subtable = ACPI_ADD_PTR (ACPI_WDAT_ENTRY, Subtable,
            sizeof (ACPI_WDAT_ENTRY));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpWpbt
 *
 * PARAMETERS:  Table               - A WPBT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a WPBT. This table type consists
 *              of an open-ended arguments buffer at the end of the table.
 *
 ******************************************************************************/

void
AcpiDmDumpWpbt (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    ACPI_TABLE_WPBT         *Subtable;
    UINT32                  Length = Table->Length;
    UINT16                  ArgumentsLength;


    /* Dump the main table */

    Status = AcpiDmDumpTable (Length, 0, Table, 0, AcpiDmTableInfoWpbt);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Extract the arguments buffer length from the main table */

    Subtable = ACPI_CAST_PTR (ACPI_TABLE_WPBT, Table);
    ArgumentsLength = Subtable->ArgumentsLength;

    /* Dump the arguments buffer */

    (void) AcpiDmDumpTable (Table->Length, 0, Table, ArgumentsLength,
        AcpiDmTableInfoWpbt0);
}
