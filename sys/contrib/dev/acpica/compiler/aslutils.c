/******************************************************************************
 *
 * Module Name: aslutils -- compiler utilities
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
#include <contrib/dev/acpica/include/acdisasm.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acapps.h>
#include <sys/stat.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslutils")


/* Local prototypes */

static void
UtPadNameWithUnderscores (
    char                    *NameSeg,
    char                    *PaddedNameSeg);

static void
UtAttachNameseg (
    ACPI_PARSE_OBJECT       *Op,
    char                    *Name);


/*******************************************************************************
 *
 * FUNCTION:    UtIsBigEndianMachine
 *
 * PARAMETERS:  None
 *
 * RETURN:      TRUE if machine is big endian
 *              FALSE if machine is little endian
 *
 * DESCRIPTION: Detect whether machine is little endian or big endian.
 *
 ******************************************************************************/

UINT8
UtIsBigEndianMachine (
    void)
{
    union {
        UINT32              Integer;
        UINT8               Bytes[4];
    } Overlay =                 {0xFF000000};


    return (Overlay.Bytes[0]); /* Returns 0xFF (TRUE) for big endian */
}


/******************************************************************************
 *
 * FUNCTION:    UtQueryForOverwrite
 *
 * PARAMETERS:  Pathname            - Output filename
 *
 * RETURN:      TRUE if file does not exist or overwrite is authorized
 *
 * DESCRIPTION: Query for file overwrite if it already exists.
 *
 ******************************************************************************/

BOOLEAN
UtQueryForOverwrite (
    char                    *Pathname)
{
    struct stat             StatInfo;


    if (!stat (Pathname, &StatInfo))
    {
        fprintf (stderr, "Target file \"%s\" already exists, overwrite? [y|n] ",
            Pathname);

        if (getchar () != 'y')
        {
            return (FALSE);
        }
    }

    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    UtNodeIsDescendantOf
 *
 * PARAMETERS:  Node1                   - Child node
 *              Node2                   - Possible parent node
 *
 * RETURN:      Boolean
 *
 * DESCRIPTION: Returns TRUE if Node1 is a descendant of Node2. Otherwise,
 *              return FALSE. Note, we assume a NULL Node2 element to be the
 *              topmost (root) scope. All nodes are descendants of the root.
 *              Note: Nodes at the same level (siblings) are not considered
 *              descendants.
 *
 ******************************************************************************/

BOOLEAN
UtNodeIsDescendantOf (
    ACPI_NAMESPACE_NODE     *Node1,
    ACPI_NAMESPACE_NODE     *Node2)
{

    if (Node1 == Node2)
    {
        return (FALSE);
    }

    if (!Node2)
    {
        return (TRUE); /* All nodes descend from the root */
    }

    /* Walk upward until the root is reached or parent is found */

    while (Node1)
    {
        if (Node1 == Node2)
        {
            return (TRUE);
        }

        Node1 = Node1->Parent;
    }

    return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    UtGetParentMethod
 *
 * PARAMETERS:  Node                    - Namespace node for any object
 *
 * RETURN:      Namespace node for the parent method
 *              NULL - object is not within a method
 *
 * DESCRIPTION: Find the parent (owning) method node for a namespace object
 *
 ******************************************************************************/

void *
UtGetParentMethod (
    ACPI_NAMESPACE_NODE     *Node)
{
    ACPI_NAMESPACE_NODE     *ParentNode;


    if (!Node)
    {
        return (NULL);
    }

    /* Walk upward until a method is found, or the root is reached */

    ParentNode = Node->Parent;
    while (ParentNode)
    {
        if (ParentNode->Type == ACPI_TYPE_METHOD)
        {
            return (ParentNode);
        }

        ParentNode = ParentNode->Parent;
    }

    return (NULL); /* Object is not within a control method */
}


/*******************************************************************************
 *
 * FUNCTION:    UtDisplaySupportedTables
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print all supported ACPI table names.
 *
 ******************************************************************************/

void
UtDisplaySupportedTables (
    void)
{
    const AH_TABLE          *TableData;
    UINT32                  i;


    printf ("\nACPI tables supported by iASL version %8.8X:\n"
        "  (Compiler, Disassembler, Template Generator)\n\n",
        ACPI_CA_VERSION);

    /* All ACPI tables with the common table header */

    printf ("\n  Supported ACPI tables:\n");
    for (TableData = AcpiGbl_SupportedTables, i = 1;
         TableData->Signature; TableData++, i++)
    {
        printf ("%8u) %s    %s\n", i,
            TableData->Signature, TableData->Description);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    UtDisplayConstantOpcodes
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print AML opcodes that can be used in constant expressions.
 *
 ******************************************************************************/

void
UtDisplayConstantOpcodes (
    void)
{
    UINT32                  i;


    printf ("Constant expression opcode information\n\n");

    for (i = 0; i < sizeof (AcpiGbl_AmlOpInfo) / sizeof (ACPI_OPCODE_INFO); i++)
    {
        if (AcpiGbl_AmlOpInfo[i].Flags & AML_CONSTANT)
        {
            printf ("%s\n", AcpiGbl_AmlOpInfo[i].Name);
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    UtBeginEvent
 *
 * PARAMETERS:  Name                - Ascii name of this event
 *
 * RETURN:      Event number (integer index)
 *
 * DESCRIPTION: Saves the current time with this event
 *
 ******************************************************************************/

UINT8
UtBeginEvent (
    char                    *Name)
{

    if (AslGbl_NextEvent >= ASL_NUM_EVENTS)
    {
        AcpiOsPrintf ("Ran out of compiler event structs!\n");
        return (AslGbl_NextEvent);
    }

    /* Init event with current (start) time */

    AslGbl_Events[AslGbl_NextEvent].StartTime = AcpiOsGetTimer ();
    AslGbl_Events[AslGbl_NextEvent].EventName = Name;
    AslGbl_Events[AslGbl_NextEvent].Valid = TRUE;
    return (AslGbl_NextEvent++);
}


/*******************************************************************************
 *
 * FUNCTION:    UtEndEvent
 *
 * PARAMETERS:  Event               - Event number (integer index)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Saves the current time (end time) with this event
 *
 ******************************************************************************/

void
UtEndEvent (
    UINT8                   Event)
{

    if (Event >= ASL_NUM_EVENTS)
    {
        return;
    }

    /* Insert end time for event */

    AslGbl_Events[Event].EndTime = AcpiOsGetTimer ();
}


/*******************************************************************************
 *
 * FUNCTION:    DbgPrint
 *
 * PARAMETERS:  Type                - Type of output
 *              Fmt                 - Printf format string
 *              ...                 - variable printf list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Conditional print statement. Prints to stderr only if the
 *              debug flag is set.
 *
 ******************************************************************************/

void
DbgPrint (
    UINT32                  Type,
    char                    *Fmt,
    ...)
{
    va_list                 Args;


    if (!AslGbl_DebugFlag)
    {
        return;
    }

    if ((Type == ASL_PARSE_OUTPUT) &&
        (!(AslCompilerdebug)))
    {
        return;
    }

    va_start (Args, Fmt);
    (void) vfprintf (stderr, Fmt, Args);
    va_end (Args);
    return;
}


/*******************************************************************************
 *
 * FUNCTION:    UtSetParseOpName
 *
 * PARAMETERS:  Op                  - Parse op to be named.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Insert the ascii name of the parse opcode
 *
 ******************************************************************************/

void
UtSetParseOpName (
    ACPI_PARSE_OBJECT       *Op)
{

    AcpiUtSafeStrncpy (Op->Asl.ParseOpName, UtGetOpName (Op->Asl.ParseOpcode),
        ACPI_MAX_PARSEOP_NAME);
}


/*******************************************************************************
 *
 * FUNCTION:    UtDisplaySummary
 *
 * PARAMETERS:  FileID              - ID of outpout file
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display compilation statistics
 *
 ******************************************************************************/

void
UtDisplaySummary (
    UINT32                  FileId)
{
    UINT32                  i;


    if (FileId != ASL_FILE_STDOUT)
    {
        /* Compiler name and version number */

        FlPrintFile (FileId, "%s version %X\n\n",
            ASL_COMPILER_NAME, (UINT32) ACPI_CA_VERSION);
    }

    /* Summary of main input and output files */

    if (AslGbl_FileType == ASL_INPUT_TYPE_ASCII_DATA)
    {
        FlPrintFile (FileId,
            "%-14s %s - %u lines, %u bytes, %u fields\n",
            "Table Input:",
            AslGbl_Files[ASL_FILE_INPUT].Filename, AslGbl_CurrentLineNumber,
            AslGbl_InputByteCount, AslGbl_InputFieldCount);

        if ((AslGbl_ExceptionCount[ASL_ERROR] == 0) || (AslGbl_IgnoreErrors))
        {
            FlPrintFile (FileId,
                "%-14s %s - %u bytes\n",
                "Binary Output:",
                AslGbl_Files[ASL_FILE_AML_OUTPUT].Filename, AslGbl_TableLength);
        }
    }
    else
    {
        FlPrintFile (FileId,
            "%-14s %s - %u lines, %u bytes, %u keywords\n",
            "ASL Input:",
            AslGbl_Files[ASL_FILE_INPUT].Filename, AslGbl_CurrentLineNumber,
            AslGbl_OriginalInputFileSize, AslGbl_TotalKeywords);

        /* AML summary */

        if ((AslGbl_ExceptionCount[ASL_ERROR] == 0) || (AslGbl_IgnoreErrors))
        {
            if (AslGbl_Files[ASL_FILE_AML_OUTPUT].Handle)
            {
                FlPrintFile (FileId,
                    "%-14s %s - %u bytes, %u named objects, "
                    "%u executable opcodes\n",
                    "AML Output:",
                    AslGbl_Files[ASL_FILE_AML_OUTPUT].Filename,
                    FlGetFileSize (ASL_FILE_AML_OUTPUT),
                    AslGbl_TotalNamedObjects, AslGbl_TotalExecutableOpcodes);
            }
        }
    }

    /* Display summary of any optional files */

    for (i = ASL_FILE_SOURCE_OUTPUT; i <= ASL_MAX_FILE_TYPE; i++)
    {
        if (!AslGbl_Files[i].Filename || !AslGbl_Files[i].Handle)
        {
            continue;
        }

        /* .SRC is a temp file unless specifically requested */

        if ((i == ASL_FILE_SOURCE_OUTPUT) && (!AslGbl_SourceOutputFlag))
        {
            continue;
        }

        /* .PRE is the preprocessor intermediate file */

        if ((i == ASL_FILE_PREPROCESSOR)  && (!AslGbl_KeepPreprocessorTempFile))
        {
            continue;
        }

        FlPrintFile (FileId, "%14s %s - %u bytes\n",
            AslGbl_Files[i].ShortDescription,
            AslGbl_Files[i].Filename, FlGetFileSize (i));
    }

    /* Error summary */

    FlPrintFile (FileId,
        "\nCompilation complete. %u Errors, %u Warnings, %u Remarks",
        AslGbl_ExceptionCount[ASL_ERROR],
        AslGbl_ExceptionCount[ASL_WARNING] +
            AslGbl_ExceptionCount[ASL_WARNING2] +
            AslGbl_ExceptionCount[ASL_WARNING3],
        AslGbl_ExceptionCount[ASL_REMARK]);

    if (AslGbl_FileType != ASL_INPUT_TYPE_ASCII_DATA)
    {
        FlPrintFile (FileId, ", %u Optimizations",
            AslGbl_ExceptionCount[ASL_OPTIMIZATION]);

        if (AslGbl_TotalFolds)
        {
            FlPrintFile (FileId, ", %u Constants Folded", AslGbl_TotalFolds);
        }
    }

    FlPrintFile (FileId, "\n");
}


/*******************************************************************************
 *
 * FUNCTION:    UtCheckIntegerRange
 *
 * PARAMETERS:  Op                  - Integer parse node
 *              LowValue            - Smallest allowed value
 *              HighValue           - Largest allowed value
 *
 * RETURN:      Op if OK, otherwise NULL
 *
 * DESCRIPTION: Check integer for an allowable range
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
UtCheckIntegerRange (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  LowValue,
    UINT32                  HighValue)
{

    if (!Op)
    {
        return (NULL);
    }

    if ((Op->Asl.Value.Integer < LowValue) ||
        (Op->Asl.Value.Integer > HighValue))
    {
        sprintf (AslGbl_MsgBuffer, "0x%X, allowable: 0x%X-0x%X",
            (UINT32) Op->Asl.Value.Integer, LowValue, HighValue);

        AslError (ASL_ERROR, ASL_MSG_RANGE, Op, AslGbl_MsgBuffer);
        return (NULL);
    }

    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    UtInternalizeName
 *
 * PARAMETERS:  ExternalName        - Name to convert
 *              ConvertedName       - Where the converted name is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an external (ASL) name to an internal (AML) name
 *
 ******************************************************************************/

ACPI_STATUS
UtInternalizeName (
    char                    *ExternalName,
    char                    **ConvertedName)
{
    ACPI_NAMESTRING_INFO    Info;
    ACPI_STATUS             Status;


    if (!ExternalName)
    {
        return (AE_OK);
    }

    /* Get the length of the new internal name */

    Info.ExternalName = ExternalName;
    AcpiNsGetInternalNameLength (&Info);

    /* We need a segment to store the internal name */

    Info.InternalName = UtLocalCacheCalloc (Info.Length);

    /* Build the name */

    Status = AcpiNsBuildInternalName (&Info);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    *ConvertedName = Info.InternalName;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    UtPadNameWithUnderscores
 *
 * PARAMETERS:  NameSeg             - Input nameseg
 *              PaddedNameSeg       - Output padded nameseg
 *
 * RETURN:      Padded nameseg.
 *
 * DESCRIPTION: Pads a NameSeg with underscores if necessary to form a full
 *              ACPI_NAME.
 *
 ******************************************************************************/

static void
UtPadNameWithUnderscores (
    char                    *NameSeg,
    char                    *PaddedNameSeg)
{
    UINT32                  i;


    for (i = 0; (i < ACPI_NAME_SIZE); i++)
    {
        if (*NameSeg)
        {
            *PaddedNameSeg = *NameSeg;
            NameSeg++;
        }
        else
        {
            *PaddedNameSeg = '_';
        }

        PaddedNameSeg++;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    UtAttachNameseg
 *
 * PARAMETERS:  Op                  - Parent parse node
 *              Name                - Full ExternalName
 *
 * RETURN:      None; Sets the NameSeg field in parent node
 *
 * DESCRIPTION: Extract the last nameseg of the ExternalName and store it
 *              in the NameSeg field of the Op.
 *
 ******************************************************************************/

static void
UtAttachNameseg (
    ACPI_PARSE_OBJECT       *Op,
    char                    *Name)
{
    char                    *NameSeg;
    char                    PaddedNameSeg[4];


    if (!Name)
    {
        return;
    }

    /* Look for the last dot in the namepath */

    NameSeg = strrchr (Name, '.');
    if (NameSeg)
    {
        /* Found last dot, we have also found the final nameseg */

        NameSeg++;
        UtPadNameWithUnderscores (NameSeg, PaddedNameSeg);
    }
    else
    {
        /* No dots in the namepath, there is only a single nameseg. */
        /* Handle prefixes */

        while (ACPI_IS_ROOT_PREFIX (*Name) ||
               ACPI_IS_PARENT_PREFIX (*Name))
        {
            Name++;
        }

        /* Remaining string should be one single nameseg */

        UtPadNameWithUnderscores (Name, PaddedNameSeg);
    }

    ACPI_MOVE_NAME (Op->Asl.NameSeg, PaddedNameSeg);
}


/*******************************************************************************
 *
 * FUNCTION:    UtAttachNamepathToOwner
 *
 * PARAMETERS:  Op                  - Parent parse node
 *              NameOp              - Node that contains the name
 *
 * RETURN:      Sets the ExternalName and Namepath in the parent node
 *
 * DESCRIPTION: Store the name in two forms in the parent node: The original
 *              (external) name, and the internalized name that is used within
 *              the ACPI namespace manager.
 *
 ******************************************************************************/

void
UtAttachNamepathToOwner (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *NameOp)
{
    ACPI_STATUS             Status;


    /* Full external path */

    Op->Asl.ExternalName = NameOp->Asl.Value.String;

    /* Save the NameOp for possible error reporting later */

    Op->Asl.ParentMethod = (void *) NameOp;

    /* Last nameseg of the path */

    UtAttachNameseg (Op, Op->Asl.ExternalName);

    /* Create internalized path */

    Status = UtInternalizeName (NameOp->Asl.Value.String, &Op->Asl.Namepath);
    if (ACPI_FAILURE (Status))
    {
        /* TBD: abort on no memory */
    }
}


/*******************************************************************************
 *
 * FUNCTION:    UtDoConstant
 *
 * PARAMETERS:  String              - Hex/Decimal/Octal
 *
 * RETURN:      Converted Integer
 *
 * DESCRIPTION: Convert a string to an integer, with overflow/error checking.
 *
 ******************************************************************************/

UINT64
UtDoConstant (
    char                    *String)
{
    ACPI_STATUS             Status;
    UINT64                  ConvertedInteger;
    char                    ErrBuf[64];


    Status = AcpiUtStrtoul64 (String, &ConvertedInteger);
    if (ACPI_FAILURE (Status))
    {
        sprintf (ErrBuf, "While creating 64-bit constant: %s\n",
            AcpiFormatException (Status));

        AslCommonError (ASL_ERROR, ASL_MSG_SYNTAX, AslGbl_CurrentLineNumber,
            AslGbl_LogicalLineNumber, AslGbl_CurrentLineOffset,
            AslGbl_CurrentColumn, AslGbl_Files[ASL_FILE_INPUT].Filename, ErrBuf);
    }

    return (ConvertedInteger);
}
