/******************************************************************************
 *
 * Module Name: dtutils.c - Utility routines for the data table compiler
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

#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include <contrib/dev/acpica/include/actables.h>

#define _COMPONENT          DT_COMPILER
        ACPI_MODULE_NAME    ("dtutils")

/* Local prototypes */

static void
DtSum (
    DT_SUBTABLE             *Subtable,
    void                    *Context,
    void                    *ReturnValue);


/******************************************************************************
 *
 * FUNCTION:    DtError
 *
 * PARAMETERS:  Level               - Seriousness (Warning/error, etc.)
 *              MessageId           - Index into global message buffer
 *              Op                  - Parse node where error happened
 *              ExtraMessage        - additional error message
 *
 * RETURN:      None
 *
 * DESCRIPTION: Common error interface for data table compiler
 *
 *****************************************************************************/

void
DtError (
    UINT8                   Level,
    UINT16                  MessageId,
    DT_FIELD                *FieldObject,
    char                    *ExtraMessage)
{

    /* Check if user wants to ignore this exception */

    if (AslIsExceptionIgnored (Level, MessageId))
    {
        return;
    }

    if (FieldObject)
    {
        AslCommonError (Level, MessageId,
            FieldObject->Line,
            FieldObject->Line,
            FieldObject->ByteOffset,
            FieldObject->Column,
            AslGbl_Files[ASL_FILE_INPUT].Filename, ExtraMessage);
    }
    else
    {
        AslCommonError (Level, MessageId, 0,
            0, 0, 0, 0, ExtraMessage);
    }
}


/******************************************************************************
 *
 * FUNCTION:    DtNameError
 *
 * PARAMETERS:  Level               - Seriousness (Warning/error, etc.)
 *              MessageId           - Index into global message buffer
 *              Op                  - Parse node where error happened
 *              ExtraMessage        - additional error message
 *
 * RETURN:      None
 *
 * DESCRIPTION: Error interface for named objects
 *
 *****************************************************************************/

void
DtNameError (
    UINT8                   Level,
    UINT16                  MessageId,
    DT_FIELD                *FieldObject,
    char                    *ExtraMessage)
{

    switch (Level)
    {
    case ASL_WARNING2:
    case ASL_WARNING3:

        if (AslGbl_WarningLevel < Level)
        {
            return;
        }
        break;

    default:

        break;
    }

    if (FieldObject)
    {
        AslCommonError (Level, MessageId,
            FieldObject->Line,
            FieldObject->Line,
            FieldObject->ByteOffset,
            FieldObject->NameColumn,
            AslGbl_Files[ASL_FILE_INPUT].Filename, ExtraMessage);
    }
    else
    {
        AslCommonError (Level, MessageId, 0,
            0, 0, 0, 0, ExtraMessage);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    DtFatal
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the error log and abort the compiler. Used for serious
 *              compile or I/O errors
 *
 ******************************************************************************/

void
DtFatal (
    UINT16                  MessageId,
    DT_FIELD                *FieldObject,
    char                    *ExtraMessage)
{

    DtError (ASL_ERROR, MessageId, FieldObject, ExtraMessage);

/*
 * TBD: remove this entire function, DtFatal
 *
 * We cannot abort the compiler on error, because we may be compiling a
 * list of files. We must move on to the next file.
 */
#ifdef __OBSOLETE
    CmCleanupAndExit ();
    exit (1);
#endif
}


/*******************************************************************************
 *
 * FUNCTION:    DtDoConstant
 *
 * PARAMETERS:  String              - Only hex constants are supported,
 *                                    regardless of whether the 0x prefix
 *                                    is used
 *
 * RETURN:      Converted Integer
 *
 * DESCRIPTION: Convert a string to an integer, with overflow/error checking.
 *
 ******************************************************************************/

UINT64
DtDoConstant (
    char                    *String)
{
    UINT64                  ConvertedInteger;


    /*
     * TBD: The ImplicitStrtoul64 function does not report overflow
     * conditions. The input string is simply truncated. If it is
     * desired to report overflow to the table compiler, this should
     * somehow be added here. Note: integers that are prefixed with 0x
     * or not are both hex integers.
     */
    ConvertedInteger = AcpiUtImplicitStrtoul64 (String);
    return (ConvertedInteger);
}

/******************************************************************************
 *
 * FUNCTION:    DtGetFieldValue
 *
 * PARAMETERS:  Field               - Current field list pointer
 *
 * RETURN:      Field value
 *
 * DESCRIPTION: Get field value
 *
 *****************************************************************************/

char *
DtGetFieldValue (
    DT_FIELD                *Field)
{
    if (!Field)
    {
        return (NULL);
    }

    return (Field->Value);
}


/******************************************************************************
 *
 * FUNCTION:    DtGetFieldType
 *
 * PARAMETERS:  Info                - Data table info
 *
 * RETURN:      Field type
 *
 * DESCRIPTION: Get field type
 *
 *****************************************************************************/

UINT8
DtGetFieldType (
    ACPI_DMTABLE_INFO       *Info)
{
    UINT8                   Type;


    /* DT_FLAG means that this is the start of a block of flag bits */
    /* TBD - we can make these a separate opcode later */

    if (Info->Flags & DT_FLAG)
    {
        return (DT_FIELD_TYPE_FLAGS_INTEGER);
    }

    /* Type is based upon the opcode for this field in the info table */

    switch (Info->Opcode)
    {
    case ACPI_DMT_FLAG0:
    case ACPI_DMT_FLAG1:
    case ACPI_DMT_FLAG2:
    case ACPI_DMT_FLAG3:
    case ACPI_DMT_FLAG4:
    case ACPI_DMT_FLAG5:
    case ACPI_DMT_FLAG6:
    case ACPI_DMT_FLAG7:
    case ACPI_DMT_FLAGS0:
    case ACPI_DMT_FLAGS1:
    case ACPI_DMT_FLAGS2:
    case ACPI_DMT_FLAGS4:
    case ACPI_DMT_FLAGS4_0:
    case ACPI_DMT_FLAGS4_4:
    case ACPI_DMT_FLAGS4_8:
    case ACPI_DMT_FLAGS4_12:
    case ACPI_DMT_FLAGS16_16:

        Type = DT_FIELD_TYPE_FLAG;
        break;

    case ACPI_DMT_NAME4:
    case ACPI_DMT_SIG:
    case ACPI_DMT_NAME6:
    case ACPI_DMT_NAME8:
    case ACPI_DMT_STRING:

        Type = DT_FIELD_TYPE_STRING;
        break;

    case ACPI_DMT_BUFFER:
    case ACPI_DMT_RAW_BUFFER:
    case ACPI_DMT_BUF7:
    case ACPI_DMT_BUF10:
    case ACPI_DMT_BUF12:
    case ACPI_DMT_BUF16:
    case ACPI_DMT_BUF128:
    case ACPI_DMT_PCI_PATH:

        Type = DT_FIELD_TYPE_BUFFER;
        break;

    case ACPI_DMT_GAS:
    case ACPI_DMT_HESTNTFY:
    case ACPI_DMT_IORTMEM:

        Type = DT_FIELD_TYPE_INLINE_SUBTABLE;
        break;

    case ACPI_DMT_UNICODE:

        Type = DT_FIELD_TYPE_UNICODE;
        break;

    case ACPI_DMT_UUID:

        Type = DT_FIELD_TYPE_UUID;
        break;

    case ACPI_DMT_DEVICE_PATH:

        Type = DT_FIELD_TYPE_DEVICE_PATH;
        break;

    case ACPI_DMT_LABEL:

        Type = DT_FIELD_TYPE_LABEL;
        break;

    default:

        Type = DT_FIELD_TYPE_INTEGER;
        break;
    }

    return (Type);
}


/******************************************************************************
 *
 * FUNCTION:    DtGetBufferLength
 *
 * PARAMETERS:  Buffer              - List of integers,
 *                                    for example "10 3A 4F 2E"
 *
 * RETURN:      Count of integer
 *
 * DESCRIPTION: Get length of bytes needed to store the integers
 *
 *****************************************************************************/

UINT32
DtGetBufferLength (
    char                    *Buffer)
{
    UINT32                  ByteLength = 0;


    while (*Buffer)
    {
        if (*Buffer == ' ')
        {
            ByteLength++;

            while (*Buffer == ' ')
            {
                Buffer++;
            }
        }

        Buffer++;
    }

    return (++ByteLength);
}


/******************************************************************************
 *
 * FUNCTION:    DtGetFieldLength
 *
 * PARAMETERS:  Field               - Current field
 *              Info                - Data table info
 *
 * RETURN:      Field length
 *
 * DESCRIPTION: Get length of bytes needed to compile the field
 *
 * Note: This function must remain in sync with AcpiDmDumpTable.
 *
 *****************************************************************************/

UINT32
DtGetFieldLength (
    DT_FIELD                *Field,
    ACPI_DMTABLE_INFO       *Info)
{
    UINT32                  ByteLength = 0;
    char                    *Value;


    /* Length is based upon the opcode for this field in the info table */

    switch (Info->Opcode)
    {
    case ACPI_DMT_FLAG0:
    case ACPI_DMT_FLAG1:
    case ACPI_DMT_FLAG2:
    case ACPI_DMT_FLAG3:
    case ACPI_DMT_FLAG4:
    case ACPI_DMT_FLAG5:
    case ACPI_DMT_FLAG6:
    case ACPI_DMT_FLAG7:
    case ACPI_DMT_FLAGS0:
    case ACPI_DMT_FLAGS1:
    case ACPI_DMT_FLAGS2:
    case ACPI_DMT_FLAGS4:
    case ACPI_DMT_FLAGS4_0:
    case ACPI_DMT_FLAGS4_4:
    case ACPI_DMT_FLAGS4_8:
    case ACPI_DMT_FLAGS4_12:
    case ACPI_DMT_FLAGS16_16:
    case ACPI_DMT_LABEL:
    case ACPI_DMT_EXTRA_TEXT:

        ByteLength = 0;
        break;

    case ACPI_DMT_UINT8:
    case ACPI_DMT_CHKSUM:
    case ACPI_DMT_SPACEID:
    case ACPI_DMT_ACCWIDTH:
    case ACPI_DMT_IVRS:
    case ACPI_DMT_GTDT:
    case ACPI_DMT_MADT:
    case ACPI_DMT_PCCT:
    case ACPI_DMT_PMTT:
    case ACPI_DMT_PPTT:
    case ACPI_DMT_SDEV:
    case ACPI_DMT_SRAT:
    case ACPI_DMT_ASF:
    case ACPI_DMT_HESTNTYP:
    case ACPI_DMT_FADTPM:
    case ACPI_DMT_EINJACT:
    case ACPI_DMT_EINJINST:
    case ACPI_DMT_ERSTACT:
    case ACPI_DMT_ERSTINST:
    case ACPI_DMT_DMAR_SCOPE:

        ByteLength = 1;
        break;

    case ACPI_DMT_UINT16:
    case ACPI_DMT_DMAR:
    case ACPI_DMT_HEST:
    case ACPI_DMT_HMAT:
    case ACPI_DMT_NFIT:
    case ACPI_DMT_PCI_PATH:

        ByteLength = 2;
        break;

    case ACPI_DMT_UINT24:

        ByteLength = 3;
        break;

    case ACPI_DMT_UINT32:
    case ACPI_DMT_NAME4:
    case ACPI_DMT_SIG:
    case ACPI_DMT_LPIT:
    case ACPI_DMT_TPM2:

        ByteLength = 4;
        break;

    case ACPI_DMT_UINT40:

        ByteLength = 5;
        break;

    case ACPI_DMT_UINT48:
    case ACPI_DMT_NAME6:

        ByteLength = 6;
        break;

    case ACPI_DMT_UINT56:
    case ACPI_DMT_BUF7:

        ByteLength = 7;
        break;

    case ACPI_DMT_UINT64:
    case ACPI_DMT_NAME8:

        ByteLength = 8;
        break;

    case ACPI_DMT_STRING:

        Value = DtGetFieldValue (Field);
        if (Value)
        {
            ByteLength = strlen (Value) + 1;
        }
        else
        {   /* At this point, this is a fatal error */

            sprintf (AslGbl_MsgBuffer, "Expected \"%s\"", Info->Name);
            DtFatal (ASL_MSG_COMPILER_INTERNAL, NULL, AslGbl_MsgBuffer);
            return (0);
        }
        break;

    case ACPI_DMT_GAS:

        ByteLength = sizeof (ACPI_GENERIC_ADDRESS);
        break;

    case ACPI_DMT_HESTNTFY:

        ByteLength = sizeof (ACPI_HEST_NOTIFY);
        break;

    case ACPI_DMT_IORTMEM:

        ByteLength = sizeof (ACPI_IORT_MEMORY_ACCESS);
        break;

    case ACPI_DMT_BUFFER:
    case ACPI_DMT_RAW_BUFFER:

        Value = DtGetFieldValue (Field);
        if (Value)
        {
            ByteLength = DtGetBufferLength (Value);
        }
        else
        {   /* At this point, this is a fatal error */

            sprintf (AslGbl_MsgBuffer, "Expected \"%s\"", Info->Name);
            DtFatal (ASL_MSG_COMPILER_INTERNAL, NULL, AslGbl_MsgBuffer);
            return (0);
        }
        break;

    case ACPI_DMT_BUF10:

        ByteLength = 10;
        break;

    case ACPI_DMT_BUF12:

        ByteLength = 12;
        break;

    case ACPI_DMT_BUF16:
    case ACPI_DMT_UUID:

        ByteLength = 16;
        break;

    case ACPI_DMT_BUF128:

        ByteLength = 128;
        break;

    case ACPI_DMT_UNICODE:

        Value = DtGetFieldValue (Field);

        /* TBD: error if Value is NULL? (as below?) */

        ByteLength = (strlen (Value) + 1) * sizeof(UINT16);
        break;

    default:

        DtFatal (ASL_MSG_COMPILER_INTERNAL, Field, "Invalid table opcode");
        return (0);
    }

    return (ByteLength);
}


/******************************************************************************
 *
 * FUNCTION:    DtSum
 *
 * PARAMETERS:  DT_WALK_CALLBACK:
 *              Subtable            - Subtable
 *              Context             - Unused
 *              ReturnValue         - Store the checksum of subtable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the checksum of subtable
 *
 *****************************************************************************/

static void
DtSum (
    DT_SUBTABLE             *Subtable,
    void                    *Context,
    void                    *ReturnValue)
{
    UINT8                   Checksum;
    UINT8                   *Sum = ReturnValue;


    Checksum = AcpiTbChecksum (Subtable->Buffer, Subtable->Length);
    *Sum = (UINT8) (*Sum + Checksum);
}


/******************************************************************************
 *
 * FUNCTION:    DtSetTableChecksum
 *
 * PARAMETERS:  ChecksumPointer     - Where to return the checksum
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set checksum of the whole data table into the checksum field
 *
 *****************************************************************************/

void
DtSetTableChecksum (
    UINT8                   *ChecksumPointer)
{
    UINT8                   Checksum = 0;
    UINT8                   OldSum;


    DtWalkTableTree (AslGbl_RootTable, DtSum, NULL, &Checksum);

    OldSum = *ChecksumPointer;
    Checksum = (UINT8) (Checksum - OldSum);

    /* Compute the final checksum */

    Checksum = (UINT8) (0 - Checksum);
    *ChecksumPointer = Checksum;
}


/******************************************************************************
 *
 * FUNCTION:    DtSetTableLength
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Walk the subtables and set all the length fields
 *
 *****************************************************************************/

void
DtSetTableLength (
    void)
{
    DT_SUBTABLE             *ParentTable;
    DT_SUBTABLE             *ChildTable;


    ParentTable = AslGbl_RootTable;
    ChildTable = NULL;

    if (!ParentTable)
    {
        return;
    }

    DtSetSubtableLength (ParentTable);

    while (1)
    {
        ChildTable = DtGetNextSubtable (ParentTable, ChildTable);
        if (ChildTable)
        {
            if (ChildTable->LengthField)
            {
                DtSetSubtableLength (ChildTable);
            }

            if (ChildTable->Child)
            {
                ParentTable = ChildTable;
                ChildTable = NULL;
            }
            else
            {
                ParentTable->TotalLength += ChildTable->TotalLength;
                if (ParentTable->LengthField)
                {
                    DtSetSubtableLength (ParentTable);
                }
            }
        }
        else
        {
            ChildTable = ParentTable;

            if (ChildTable == AslGbl_RootTable)
            {
                break;
            }

            ParentTable = DtGetParentSubtable (ParentTable);

            ParentTable->TotalLength += ChildTable->TotalLength;
            if (ParentTable->LengthField)
            {
                DtSetSubtableLength (ParentTable);
            }
        }
    }
}


/******************************************************************************
 *
 * FUNCTION:    DtWalkTableTree
 *
 * PARAMETERS:  StartTable          - Subtable in the tree where walking begins
 *              UserFunction        - Called during the walk
 *              Context             - Passed to user function
 *              ReturnValue         - The return value of UserFunction
 *
 * RETURN:      None
 *
 * DESCRIPTION: Performs a depth-first walk of the subtable tree
 *
 *****************************************************************************/

void
DtWalkTableTree (
    DT_SUBTABLE             *StartTable,
    DT_WALK_CALLBACK        UserFunction,
    void                    *Context,
    void                    *ReturnValue)
{
    DT_SUBTABLE             *ParentTable;
    DT_SUBTABLE             *ChildTable;


    ParentTable = StartTable;
    ChildTable = NULL;

    if (!ParentTable)
    {
        return;
    }

    UserFunction (ParentTable, Context, ReturnValue);

    while (1)
    {
        ChildTable = DtGetNextSubtable (ParentTable, ChildTable);
        if (ChildTable)
        {
            UserFunction (ChildTable, Context, ReturnValue);

            if (ChildTable->Child)
            {
                ParentTable = ChildTable;
                ChildTable = NULL;
            }
        }
        else
        {
            ChildTable = ParentTable;
            if (ChildTable == AslGbl_RootTable)
            {
                break;
            }

            ParentTable = DtGetParentSubtable (ParentTable);

            if (ChildTable->Peer == StartTable)
            {
                break;
            }
        }
    }
}
