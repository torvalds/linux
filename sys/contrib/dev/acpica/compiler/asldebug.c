/******************************************************************************
 *
 * Module Name: asldebug -- Debug output support
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
#include "aslcompiler.y.h"


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("asldebug")


/* Local prototypes */

static void
UtDumpParseOpName (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    UINT32                  DataLength);

static char *
UtCreateEscapeSequences (
    char                    *InString);


/*******************************************************************************
 *
 * FUNCTION:    CvDbgPrint
 *
 * PARAMETERS:  Type                - Type of output
 *              Fmt                 - Printf format string
 *              ...                 - variable printf list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print statement for debug messages within the converter.
 *
 ******************************************************************************/

void
CvDbgPrint (
    char                    *Fmt,
    ...)
{
    va_list                 Args;


    if (!AcpiGbl_CaptureComments || !AcpiGbl_DebugAslConversion)
    {
        return;
    }

    va_start (Args, Fmt);
    (void) vfprintf (AcpiGbl_ConvDebugFile, Fmt, Args);
    va_end (Args);
    return;
}


/*******************************************************************************
 *
 * FUNCTION:    UtDumpIntegerOp
 *
 * PARAMETERS:  Op                  - Current parse op
 *              Level               - Current output indentation level
 *              IntegerLength       - Output length of the integer (2/4/8/16)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Emit formatted debug output for "integer" ops.
 *              Note: IntegerLength must be one of 2,4,8,16.
 *
 ******************************************************************************/

void
UtDumpIntegerOp (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    UINT32                  IntegerLength)
{

    /* Emit the ParseOp name, leaving room for the integer */

    UtDumpParseOpName (Op, Level, IntegerLength);

    /* Emit the integer based upon length */

    switch (IntegerLength)
    {
    case 2: /* Byte */
    case 4: /* Word */
    case 8: /* Dword */

        DbgPrint (ASL_TREE_OUTPUT,
            "%*.*X", IntegerLength, IntegerLength, Op->Asl.Value.Integer);
        break;

    case 16: /* Qword and Integer */

        DbgPrint (ASL_TREE_OUTPUT,
            "%8.8X%8.8X", ACPI_FORMAT_UINT64 (Op->Asl.Value.Integer));
        break;

    default:
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    UtDumpStringOp
 *
 * PARAMETERS:  Op                  - Current parse op
 *              Level               - Current output indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Emit formatted debug output for String/Pathname ops.
 *
 ******************************************************************************/

void
UtDumpStringOp (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level)
{
    char                    *String;


    String = Op->Asl.Value.String;
    if (Op->Asl.ParseOpcode != PARSEOP_STRING_LITERAL)
    {
        /*
         * For the "path" ops NAMEPATH, NAMESEG, METHODCALL -- if the
         * ExternalName is valid, it takes precedence. In these cases the
         * Value.String is the raw "internal" name from the AML code, which
         * we don't want to use, because it contains non-ascii characters.
         */
        if (Op->Asl.ExternalName)
        {
            String = Op->Asl.ExternalName;
        }
    }

    if (!String)
    {
        DbgPrint (ASL_TREE_OUTPUT,
            " ERROR: Could not find a valid String/Path pointer\n");
        return;
    }

    String = UtCreateEscapeSequences (String);

    /* Emit the ParseOp name, leaving room for the string */

    UtDumpParseOpName (Op, Level, strlen (String));
    DbgPrint (ASL_TREE_OUTPUT, "%s", String);
}


/*******************************************************************************
 *
 * FUNCTION:    UtCreateEscapeSequences
 *
 * PARAMETERS:  InString            - ASCII string to be expanded
 *
 * RETURN:      Expanded string
 *
 * DESCRIPTION: Expand all non-printable ASCII bytes (0-0x1F) to escape
 *              sequences. For example, hex 14 becomes \x14
 *
 * NOTE:        Since this function is used for debug output only, it does
 *              not attempt to translate into the "known" escape sequences
 *              such as \a, \f, \t, etc.
 *
 ******************************************************************************/

static char *
UtCreateEscapeSequences (
    char                    *InString)
{
    char                    *String = InString;
    char                    *OutString;
    char                    *OutStringPtr;
    UINT32                  InStringLength = 0;
    UINT32                  EscapeCount = 0;


    /*
     * Determine up front how many escapes are within the string.
     * Obtain the input string length while doing so.
     */
    while (*String)
    {
        if ((*String <= 0x1F) || (*String >= 0x7F))
        {
            EscapeCount++;
        }

        InStringLength++;
        String++;
    }

    if (!EscapeCount)
    {
        return (InString); /* No escapes, nothing to do */
    }

    /* New string buffer, 3 extra chars per escape (4 total) */

    OutString = UtLocalCacheCalloc (InStringLength + (EscapeCount * 3));
    OutStringPtr = OutString;

    /* Convert non-ascii or non-printable chars to escape sequences */

    while (*InString)
    {
        if ((*InString <= 0x1F) || (*InString >= 0x7F))
        {
            /* Insert a \x hex escape sequence */

            OutStringPtr[0] = '\\';
            OutStringPtr[1] = 'x';
            OutStringPtr[2] = AcpiUtHexToAsciiChar (*InString, 4);
            OutStringPtr[3] = AcpiUtHexToAsciiChar (*InString, 0);
            OutStringPtr += 4;
        }
        else /* Normal ASCII character */
        {
            *OutStringPtr = *InString;
            OutStringPtr++;
        }

        InString++;
    }

    return (OutString);
}


/*******************************************************************************
 *
 * FUNCTION:    UtDumpBasicOp
 *
 * PARAMETERS:  Op                  - Current parse op
 *              Level               - Current output indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generic formatted debug output for "basic" ops that have no
 *              associated strings or integer values.
 *
 ******************************************************************************/

void
UtDumpBasicOp (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level)
{

    /* Just print out the ParseOp name, there is no extra data */

    UtDumpParseOpName (Op, Level, 0);
}


/*******************************************************************************
 *
 * FUNCTION:    UtDumpParseOpName
 *
 * PARAMETERS:  Op                  - Current parse op
 *              Level               - Current output indentation level
 *              DataLength          - Length of data to appear after the name
 *
 * RETURN:      None
 *
 * DESCRIPTION: Indent and emit the ascii ParseOp name for the op
 *
 ******************************************************************************/

static void
UtDumpParseOpName (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    UINT32                  DataLength)
{
    char                    *ParseOpName;
    UINT32                  IndentLength;
    UINT32                  NameLength;
    UINT32                  LineLength;
    UINT32                  PaddingLength;


    /* Emit the LineNumber/IndentLevel prefix on each output line */

    DbgPrint (ASL_TREE_OUTPUT,
        "%5.5d [%2d]", Op->Asl.LogicalLineNumber, Level);

    ParseOpName = UtGetOpName (Op->Asl.ParseOpcode);

    /* Calculate various lengths for output alignment */

    IndentLength = Level * DEBUG_SPACES_PER_INDENT;
    NameLength = strlen (ParseOpName);
    LineLength = IndentLength + 1 + NameLength + 1 + DataLength;
    PaddingLength = (DEBUG_MAX_LINE_LENGTH + 1) - LineLength;

    /* Parse tree indentation is based upon the nesting/indent level */

    if (Level)
    {
        DbgPrint (ASL_TREE_OUTPUT, "%*s", IndentLength, " ");
    }

    /* Emit the actual name here */

    DbgPrint (ASL_TREE_OUTPUT, " %s", ParseOpName);

    /* Emit extra padding blanks for alignment of later data items */

    if (LineLength > DEBUG_MAX_LINE_LENGTH)
    {
        /* Split a long line immediately after the ParseOpName string */

        DbgPrint (ASL_TREE_OUTPUT, "\n%*s",
            (DEBUG_FULL_LINE_LENGTH - DataLength), " ");
    }
    else
    {
        DbgPrint (ASL_TREE_OUTPUT, "%*s", PaddingLength, " ");
    }
}
