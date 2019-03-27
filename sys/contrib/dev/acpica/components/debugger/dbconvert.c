/*******************************************************************************
 *
 * Module Name: dbconvert - debugger miscellaneous conversion routines
 *
 ******************************************************************************/

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
#include <contrib/dev/acpica/include/acdebug.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dbconvert")


#define DB_DEFAULT_PKG_ELEMENTS     33


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbHexCharToValue
 *
 * PARAMETERS:  HexChar             - Ascii Hex digit, 0-9|a-f|A-F
 *              ReturnValue         - Where the converted value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a single hex character to a 4-bit number (0-16).
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbHexCharToValue (
    int                     HexChar,
    UINT8                   *ReturnValue)
{
    UINT8                   Value;


    /* Digit must be ascii [0-9a-fA-F] */

    if (!isxdigit (HexChar))
    {
        return (AE_BAD_HEX_CONSTANT);
    }

    if (HexChar <= 0x39)
    {
        Value = (UINT8) (HexChar - 0x30);
    }
    else
    {
        Value = (UINT8) (toupper (HexChar) - 0x37);
    }

    *ReturnValue = Value;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbHexByteToBinary
 *
 * PARAMETERS:  HexByte             - Double hex digit (0x00 - 0xFF) in format:
 *                                    HiByte then LoByte.
 *              ReturnValue         - Where the converted value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert two hex characters to an 8 bit number (0 - 255).
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbHexByteToBinary (
    char                    *HexByte,
    UINT8                   *ReturnValue)
{
    UINT8                   Local0;
    UINT8                   Local1;
    ACPI_STATUS             Status;


    /* High byte */

    Status = AcpiDbHexCharToValue (HexByte[0], &Local0);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* Low byte */

    Status = AcpiDbHexCharToValue (HexByte[1], &Local1);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    *ReturnValue = (UINT8) ((Local0 << 4) | Local1);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbConvertToBuffer
 *
 * PARAMETERS:  String              - Input string to be converted
 *              Object              - Where the buffer object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a string to a buffer object. String is treated a list
 *              of buffer elements, each separated by a space or comma.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbConvertToBuffer (
    char                    *String,
    ACPI_OBJECT             *Object)
{
    UINT32                  i;
    UINT32                  j;
    UINT32                  Length;
    UINT8                   *Buffer;
    ACPI_STATUS             Status;


    /* Generate the final buffer length */

    for (i = 0, Length = 0; String[i];)
    {
        i+=2;
        Length++;

        while (String[i] &&
              ((String[i] == ',') || (String[i] == ' ')))
        {
            i++;
        }
    }

    Buffer = ACPI_ALLOCATE (Length);
    if (!Buffer)
    {
        return (AE_NO_MEMORY);
    }

    /* Convert the command line bytes to the buffer */

    for (i = 0, j = 0; String[i];)
    {
        Status = AcpiDbHexByteToBinary (&String[i], &Buffer[j]);
        if (ACPI_FAILURE (Status))
        {
            ACPI_FREE (Buffer);
            return (Status);
        }

        j++;
        i += 2;
        while (String[i] &&
              ((String[i] == ',') || (String[i] == ' ')))
        {
            i++;
        }
    }

    Object->Type = ACPI_TYPE_BUFFER;
    Object->Buffer.Pointer = Buffer;
    Object->Buffer.Length = Length;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbConvertToPackage
 *
 * PARAMETERS:  String              - Input string to be converted
 *              Object              - Where the package object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a string to a package object. Handles nested packages
 *              via recursion with AcpiDbConvertToObject.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbConvertToPackage (
    char                    *String,
    ACPI_OBJECT             *Object)
{
    char                    *This;
    char                    *Next;
    UINT32                  i;
    ACPI_OBJECT_TYPE        Type;
    ACPI_OBJECT             *Elements;
    ACPI_STATUS             Status;


    Elements = ACPI_ALLOCATE_ZEROED (
        DB_DEFAULT_PKG_ELEMENTS * sizeof (ACPI_OBJECT));

    This = String;
    for (i = 0; i < (DB_DEFAULT_PKG_ELEMENTS - 1); i++)
    {
        This = AcpiDbGetNextToken (This, &Next, &Type);
        if (!This)
        {
            break;
        }

        /* Recursive call to convert each package element */

        Status = AcpiDbConvertToObject (Type, This, &Elements[i]);
        if (ACPI_FAILURE (Status))
        {
            AcpiDbDeleteObjects (i + 1, Elements);
            ACPI_FREE (Elements);
            return (Status);
        }

        This = Next;
    }

    Object->Type = ACPI_TYPE_PACKAGE;
    Object->Package.Count = i;
    Object->Package.Elements = Elements;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbConvertToObject
 *
 * PARAMETERS:  Type                - Object type as determined by parser
 *              String              - Input string to be converted
 *              Object              - Where the new object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a typed and tokenized string to an ACPI_OBJECT. Typing:
 *              1) String objects were surrounded by quotes.
 *              2) Buffer objects were surrounded by parentheses.
 *              3) Package objects were surrounded by brackets "[]".
 *              4) All standalone tokens are treated as integers.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbConvertToObject (
    ACPI_OBJECT_TYPE        Type,
    char                    *String,
    ACPI_OBJECT             *Object)
{
    ACPI_STATUS             Status = AE_OK;


    switch (Type)
    {
    case ACPI_TYPE_STRING:

        Object->Type = ACPI_TYPE_STRING;
        Object->String.Pointer = String;
        Object->String.Length = (UINT32) strlen (String);
        break;

    case ACPI_TYPE_BUFFER:

        Status = AcpiDbConvertToBuffer (String, Object);
        break;

    case ACPI_TYPE_PACKAGE:

        Status = AcpiDbConvertToPackage (String, Object);
        break;

    default:

        Object->Type = ACPI_TYPE_INTEGER;
        Status = AcpiUtStrtoul64 (String, &Object->Integer.Value);
        break;
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbEncodePldBuffer
 *
 * PARAMETERS:  PldInfo             - _PLD buffer struct (Using local struct)
 *
 * RETURN:      Encode _PLD buffer suitable for return value from _PLD
 *
 * DESCRIPTION: Bit-packs a _PLD buffer struct. Used to test the _PLD macros
 *
 ******************************************************************************/

UINT8 *
AcpiDbEncodePldBuffer (
    ACPI_PLD_INFO           *PldInfo)
{
    UINT32                  *Buffer;
    UINT32                  Dword;


    Buffer = ACPI_ALLOCATE_ZEROED (ACPI_PLD_BUFFER_SIZE);
    if (!Buffer)
    {
        return (NULL);
    }

    /* First 32 bits */

    Dword = 0;
    ACPI_PLD_SET_REVISION       (&Dword, PldInfo->Revision);
    ACPI_PLD_SET_IGNORE_COLOR   (&Dword, PldInfo->IgnoreColor);
    ACPI_PLD_SET_RED            (&Dword, PldInfo->Red);
    ACPI_PLD_SET_GREEN          (&Dword, PldInfo->Green);
    ACPI_PLD_SET_BLUE           (&Dword, PldInfo->Blue);
    ACPI_MOVE_32_TO_32 (&Buffer[0], &Dword);

    /* Second 32 bits */

    Dword = 0;
    ACPI_PLD_SET_WIDTH          (&Dword, PldInfo->Width);
    ACPI_PLD_SET_HEIGHT         (&Dword, PldInfo->Height);
    ACPI_MOVE_32_TO_32 (&Buffer[1], &Dword);

    /* Third 32 bits */

    Dword = 0;
    ACPI_PLD_SET_USER_VISIBLE   (&Dword, PldInfo->UserVisible);
    ACPI_PLD_SET_DOCK           (&Dword, PldInfo->Dock);
    ACPI_PLD_SET_LID            (&Dword, PldInfo->Lid);
    ACPI_PLD_SET_PANEL          (&Dword, PldInfo->Panel);
    ACPI_PLD_SET_VERTICAL       (&Dword, PldInfo->VerticalPosition);
    ACPI_PLD_SET_HORIZONTAL     (&Dword, PldInfo->HorizontalPosition);
    ACPI_PLD_SET_SHAPE          (&Dword, PldInfo->Shape);
    ACPI_PLD_SET_ORIENTATION    (&Dword, PldInfo->GroupOrientation);
    ACPI_PLD_SET_TOKEN          (&Dword, PldInfo->GroupToken);
    ACPI_PLD_SET_POSITION       (&Dword, PldInfo->GroupPosition);
    ACPI_PLD_SET_BAY            (&Dword, PldInfo->Bay);
    ACPI_MOVE_32_TO_32 (&Buffer[2], &Dword);

    /* Fourth 32 bits */

    Dword = 0;
    ACPI_PLD_SET_EJECTABLE      (&Dword, PldInfo->Ejectable);
    ACPI_PLD_SET_OSPM_EJECT     (&Dword, PldInfo->OspmEjectRequired);
    ACPI_PLD_SET_CABINET        (&Dword, PldInfo->CabinetNumber);
    ACPI_PLD_SET_CARD_CAGE      (&Dword, PldInfo->CardCageNumber);
    ACPI_PLD_SET_REFERENCE      (&Dword, PldInfo->Reference);
    ACPI_PLD_SET_ROTATION       (&Dword, PldInfo->Rotation);
    ACPI_PLD_SET_ORDER          (&Dword, PldInfo->Order);
    ACPI_MOVE_32_TO_32 (&Buffer[3], &Dword);

    if (PldInfo->Revision >= 2)
    {
        /* Fifth 32 bits */

        Dword = 0;
        ACPI_PLD_SET_VERT_OFFSET    (&Dword, PldInfo->VerticalOffset);
        ACPI_PLD_SET_HORIZ_OFFSET   (&Dword, PldInfo->HorizontalOffset);
        ACPI_MOVE_32_TO_32 (&Buffer[4], &Dword);
    }

    return (ACPI_CAST_PTR (UINT8, Buffer));
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDumpPldBuffer
 *
 * PARAMETERS:  ObjDesc             - Object returned from _PLD method
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Dumps formatted contents of a _PLD return buffer.
 *
 ******************************************************************************/

#define ACPI_PLD_OUTPUT     "%20s : %-6X\n"

void
AcpiDbDumpPldBuffer (
    ACPI_OBJECT             *ObjDesc)
{
    ACPI_OBJECT             *BufferDesc;
    ACPI_PLD_INFO           *PldInfo;
    UINT8                   *NewBuffer;
    ACPI_STATUS             Status;


    /* Object must be of type Package with at least one Buffer element */

    if (ObjDesc->Type != ACPI_TYPE_PACKAGE)
    {
        return;
    }

    BufferDesc = &ObjDesc->Package.Elements[0];
    if (BufferDesc->Type != ACPI_TYPE_BUFFER)
    {
        return;
    }

    /* Convert _PLD buffer to local _PLD struct */

    Status = AcpiDecodePldBuffer (BufferDesc->Buffer.Pointer,
        BufferDesc->Buffer.Length, &PldInfo);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Encode local _PLD struct back to a _PLD buffer */

    NewBuffer = AcpiDbEncodePldBuffer (PldInfo);
    if (!NewBuffer)
    {
        goto Exit;
    }

    /* The two bit-packed buffers should match */

    if (memcmp (NewBuffer, BufferDesc->Buffer.Pointer,
        BufferDesc->Buffer.Length))
    {
        AcpiOsPrintf ("Converted _PLD buffer does not compare. New:\n");

        AcpiUtDumpBuffer (NewBuffer,
            BufferDesc->Buffer.Length, DB_BYTE_DISPLAY, 0);
    }

    /* First 32-bit dword */

    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Revision", PldInfo->Revision);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_IgnoreColor", PldInfo->IgnoreColor);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Red", PldInfo->Red);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Green", PldInfo->Green);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Blue", PldInfo->Blue);

    /* Second 32-bit dword */

    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Width", PldInfo->Width);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Height", PldInfo->Height);

    /* Third 32-bit dword */

    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_UserVisible", PldInfo->UserVisible);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Dock", PldInfo->Dock);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Lid", PldInfo->Lid);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Panel", PldInfo->Panel);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_VerticalPosition", PldInfo->VerticalPosition);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_HorizontalPosition", PldInfo->HorizontalPosition);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Shape", PldInfo->Shape);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_GroupOrientation", PldInfo->GroupOrientation);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_GroupToken", PldInfo->GroupToken);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_GroupPosition", PldInfo->GroupPosition);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Bay", PldInfo->Bay);

    /* Fourth 32-bit dword */

    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Ejectable", PldInfo->Ejectable);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_EjectRequired", PldInfo->OspmEjectRequired);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_CabinetNumber", PldInfo->CabinetNumber);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_CardCageNumber", PldInfo->CardCageNumber);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Reference", PldInfo->Reference);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Rotation", PldInfo->Rotation);
    AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_Order", PldInfo->Order);

    /* Fifth 32-bit dword */

    if (BufferDesc->Buffer.Length > 16)
    {
        AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_VerticalOffset", PldInfo->VerticalOffset);
        AcpiOsPrintf (ACPI_PLD_OUTPUT, "PLD_HorizontalOffset", PldInfo->HorizontalOffset);
    }

    ACPI_FREE (NewBuffer);
Exit:
    ACPI_FREE (PldInfo);
}
