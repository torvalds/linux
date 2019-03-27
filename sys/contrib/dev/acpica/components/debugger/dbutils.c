/*******************************************************************************
 *
 * Module Name: dbutils - AML debugger utilities
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
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acdebug.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dbutils")


/* Local prototypes */

#ifdef ACPI_OBSOLETE_FUNCTIONS
ACPI_STATUS
AcpiDbSecondPassParse (
    ACPI_PARSE_OBJECT       *Root);

void
AcpiDbDumpBuffer (
    UINT32                  Address);
#endif


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbMatchArgument
 *
 * PARAMETERS:  UserArgument            - User command line
 *              Arguments               - Array of commands to match against
 *
 * RETURN:      Index into command array or ACPI_TYPE_NOT_FOUND if not found
 *
 * DESCRIPTION: Search command array for a command match
 *
 ******************************************************************************/

ACPI_OBJECT_TYPE
AcpiDbMatchArgument (
    char                    *UserArgument,
    ACPI_DB_ARGUMENT_INFO   *Arguments)
{
    UINT32                  i;


    if (!UserArgument || UserArgument[0] == 0)
    {
        return (ACPI_TYPE_NOT_FOUND);
    }

    for (i = 0; Arguments[i].Name; i++)
    {
        if (strstr (
            ACPI_CAST_PTR (char, Arguments[i].Name),
            ACPI_CAST_PTR (char, UserArgument)) == Arguments[i].Name)
        {
            return (i);
        }
    }

    /* Argument not recognized */

    return (ACPI_TYPE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbSetOutputDestination
 *
 * PARAMETERS:  OutputFlags         - Current flags word
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set the current destination for debugger output. Also sets
 *              the debug output level accordingly.
 *
 ******************************************************************************/

void
AcpiDbSetOutputDestination (
    UINT32                  OutputFlags)
{

    AcpiGbl_DbOutputFlags = (UINT8) OutputFlags;

    if ((OutputFlags & ACPI_DB_REDIRECTABLE_OUTPUT) &&
        AcpiGbl_DbOutputToFile)
    {
        AcpiDbgLevel = AcpiGbl_DbDebugLevel;
    }
    else
    {
        AcpiDbgLevel = AcpiGbl_DbConsoleDebugLevel;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDumpExternalObject
 *
 * PARAMETERS:  ObjDesc         - External ACPI object to dump
 *              Level           - Nesting level.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the contents of an ACPI external object
 *
 ******************************************************************************/

void
AcpiDbDumpExternalObject (
    ACPI_OBJECT             *ObjDesc,
    UINT32                  Level)
{
    UINT32                  i;


    if (!ObjDesc)
    {
        AcpiOsPrintf ("[Null Object]\n");
        return;
    }

    for (i = 0; i < Level; i++)
    {
        AcpiOsPrintf ("  ");
    }

    switch (ObjDesc->Type)
    {
    case ACPI_TYPE_ANY:

        AcpiOsPrintf ("[Null Object] (Type=0)\n");
        break;

    case ACPI_TYPE_INTEGER:

        AcpiOsPrintf ("[Integer] = %8.8X%8.8X\n",
            ACPI_FORMAT_UINT64 (ObjDesc->Integer.Value));
        break;

    case ACPI_TYPE_STRING:

        AcpiOsPrintf ("[String] Length %.2X = ", ObjDesc->String.Length);
        AcpiUtPrintString (ObjDesc->String.Pointer, ACPI_UINT8_MAX);
        AcpiOsPrintf ("\n");
        break;

    case ACPI_TYPE_BUFFER:

        AcpiOsPrintf ("[Buffer] Length %.2X = ", ObjDesc->Buffer.Length);
        if (ObjDesc->Buffer.Length)
        {
            if (ObjDesc->Buffer.Length > 16)
            {
                AcpiOsPrintf ("\n");
            }

            AcpiUtDebugDumpBuffer (
                ACPI_CAST_PTR (UINT8, ObjDesc->Buffer.Pointer),
                ObjDesc->Buffer.Length, DB_BYTE_DISPLAY, _COMPONENT);
        }
        else
        {
            AcpiOsPrintf ("\n");
        }
        break;

    case ACPI_TYPE_PACKAGE:

        AcpiOsPrintf ("[Package] Contains %u Elements:\n",
            ObjDesc->Package.Count);

        for (i = 0; i < ObjDesc->Package.Count; i++)
        {
            AcpiDbDumpExternalObject (
                &ObjDesc->Package.Elements[i], Level+1);
        }
        break;

    case ACPI_TYPE_LOCAL_REFERENCE:

        AcpiOsPrintf ("[Object Reference] = ");
        AcpiDbDisplayInternalObject (ObjDesc->Reference.Handle, NULL);
        break;

    case ACPI_TYPE_PROCESSOR:

        AcpiOsPrintf ("[Processor]\n");
        break;

    case ACPI_TYPE_POWER:

        AcpiOsPrintf ("[Power Resource]\n");
        break;

    default:

        AcpiOsPrintf ("[Unknown Type] %X\n", ObjDesc->Type);
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbPrepNamestring
 *
 * PARAMETERS:  Name            - String to prepare
 *
 * RETURN:      None
 *
 * DESCRIPTION: Translate all forward slashes and dots to backslashes.
 *
 ******************************************************************************/

void
AcpiDbPrepNamestring (
    char                    *Name)
{

    if (!Name)
    {
        return;
    }

    AcpiUtStrupr (Name);

    /* Convert a leading forward slash to a backslash */

    if (*Name == '/')
    {
        *Name = '\\';
    }

    /* Ignore a leading backslash, this is the root prefix */

    if (ACPI_IS_ROOT_PREFIX (*Name))
    {
        Name++;
    }

    /* Convert all slash path separators to dots */

    while (*Name)
    {
        if ((*Name == '/') ||
            (*Name == '\\'))
        {
            *Name = '.';
        }

        Name++;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbLocalNsLookup
 *
 * PARAMETERS:  Name            - Name to lookup
 *
 * RETURN:      Pointer to a namespace node, null on failure
 *
 * DESCRIPTION: Lookup a name in the ACPI namespace
 *
 * Note: Currently begins search from the root. Could be enhanced to use
 * the current prefix (scope) node as the search beginning point.
 *
 ******************************************************************************/

ACPI_NAMESPACE_NODE *
AcpiDbLocalNsLookup (
    char                    *Name)
{
    char                    *InternalPath;
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node = NULL;


    AcpiDbPrepNamestring (Name);

    /* Build an internal namestring */

    Status = AcpiNsInternalizeName (Name, &InternalPath);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Invalid namestring: %s\n", Name);
        return (NULL);
    }

    /*
     * Lookup the name.
     * (Uses root node as the search starting point)
     */
    Status = AcpiNsLookup (NULL, InternalPath, ACPI_TYPE_ANY,
        ACPI_IMODE_EXECUTE, ACPI_NS_NO_UPSEARCH | ACPI_NS_DONT_OPEN_SCOPE,
        NULL, &Node);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could not locate name: %s, %s\n",
            Name, AcpiFormatException (Status));
    }

    ACPI_FREE (InternalPath);
    return (Node);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbUint32ToHexString
 *
 * PARAMETERS:  Value           - The value to be converted to string
 *              Buffer          - Buffer for result (not less than 11 bytes)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert the unsigned 32-bit value to the hexadecimal image
 *
 * NOTE: It is the caller's responsibility to ensure that the length of buffer
 *       is sufficient.
 *
 ******************************************************************************/

void
AcpiDbUint32ToHexString (
    UINT32                  Value,
    char                    *Buffer)
{
    int                     i;


    if (Value == 0)
    {
        strcpy (Buffer, "0");
        return;
    }

    Buffer[8] = '\0';

    for (i = 7; i >= 0; i--)
    {
        Buffer[i] = AcpiGbl_UpperHexDigits [Value & 0x0F];
        Value = Value >> 4;
    }
}


#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    AcpiDbSecondPassParse
 *
 * PARAMETERS:  Root            - Root of the parse tree
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Second pass parse of the ACPI tables. We need to wait until
 *              second pass to parse the control methods
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbSecondPassParse (
    ACPI_PARSE_OBJECT       *Root)
{
    ACPI_PARSE_OBJECT       *Op = Root;
    ACPI_PARSE_OBJECT       *Method;
    ACPI_PARSE_OBJECT       *SearchOp;
    ACPI_PARSE_OBJECT       *StartOp;
    ACPI_STATUS             Status = AE_OK;
    UINT32                  BaseAmlOffset;
    ACPI_WALK_STATE         *WalkState;


    ACPI_FUNCTION_ENTRY ();


    AcpiOsPrintf ("Pass two parse ....\n");

    while (Op)
    {
        if (Op->Common.AmlOpcode == AML_METHOD_OP)
        {
            Method = Op;

            /* Create a new walk state for the parse */

            WalkState = AcpiDsCreateWalkState (0, NULL, NULL, NULL);
            if (!WalkState)
            {
                return (AE_NO_MEMORY);
            }

            /* Init the Walk State */

            WalkState->ParserState.Aml          =
            WalkState->ParserState.AmlStart     = Method->Named.Data;
            WalkState->ParserState.AmlEnd       =
            WalkState->ParserState.PkgEnd       = Method->Named.Data +
                                                  Method->Named.Length;
            WalkState->ParserState.StartScope   = Op;

            WalkState->DescendingCallback       = AcpiDsLoad1BeginOp;
            WalkState->AscendingCallback        = AcpiDsLoad1EndOp;

            /* Perform the AML parse */

            Status = AcpiPsParseAml (WalkState);

            BaseAmlOffset = (Method->Common.Value.Arg)->Common.AmlOffset + 1;
            StartOp = (Method->Common.Value.Arg)->Common.Next;
            SearchOp = StartOp;

            while (SearchOp)
            {
                SearchOp->Common.AmlOffset += BaseAmlOffset;
                SearchOp = AcpiPsGetDepthNext (StartOp, SearchOp);
            }
        }

        if (Op->Common.AmlOpcode == AML_REGION_OP)
        {
            /* TBD: [Investigate] this isn't quite the right thing to do! */
            /*
             *
             * Method = (ACPI_DEFERRED_OP *) Op;
             * Status = AcpiPsParseAml (Op, Method->Body, Method->BodyLength);
             */
        }

        if (ACPI_FAILURE (Status))
        {
            break;
        }

        Op = AcpiPsGetDepthNext (Root, Op);
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDumpBuffer
 *
 * PARAMETERS:  Address             - Pointer to the buffer
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print a portion of a buffer
 *
 ******************************************************************************/

void
AcpiDbDumpBuffer (
    UINT32                  Address)
{

    AcpiOsPrintf ("\nLocation %X:\n", Address);

    AcpiDbgLevel |= ACPI_LV_TABLES;
    AcpiUtDebugDumpBuffer (ACPI_TO_POINTER (Address), 64, DB_BYTE_DISPLAY,
        ACPI_UINT32_MAX);
}
#endif
