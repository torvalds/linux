/******************************************************************************
 *
 * Module Name: asllisting - Listing file generation
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
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("asllisting")


/* Local prototypes */

static void
LsGenerateListing (
    UINT32                  FileId);

static ACPI_STATUS
LsAmlListingWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);

static ACPI_STATUS
LsTreeWriteWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);

static void
LsWriteNodeToListing (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  FileId);

static void
LsFinishSourceListing (
    UINT32                  FileId);


/*******************************************************************************
 *
 * FUNCTION:    LsDoListings
 *
 * PARAMETERS:  None. Examines the various output file global flags.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generate all requested listing files.
 *
 ******************************************************************************/

void
LsDoListings (
    void)
{

    if (AslGbl_C_OutputFlag)
    {
        LsGenerateListing (ASL_FILE_C_SOURCE_OUTPUT);
    }

    if (AslGbl_ListingFlag)
    {
        LsGenerateListing (ASL_FILE_LISTING_OUTPUT);
    }

    if (AslGbl_AsmOutputFlag)
    {
        LsGenerateListing (ASL_FILE_ASM_SOURCE_OUTPUT);
    }

    if (AslGbl_C_IncludeOutputFlag)
    {
        LsGenerateListing (ASL_FILE_C_INCLUDE_OUTPUT);
    }

    if (AslGbl_AsmIncludeOutputFlag)
    {
        LsGenerateListing (ASL_FILE_ASM_INCLUDE_OUTPUT);
    }

    if (AslGbl_C_OffsetTableFlag)
    {
        LsGenerateListing (ASL_FILE_C_OFFSET_OUTPUT);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    LsGenerateListing
 *
 * PARAMETERS:  FileId      - ID of listing file
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generate a listing file. This can be one of the several types
 *              of "listings" supported.
 *
 ******************************************************************************/

static void
LsGenerateListing (
    UINT32                  FileId)
{

    /* Start at the beginning of both the source and AML files */

    FlSeekFile (ASL_FILE_SOURCE_OUTPUT, 0);
    FlSeekFile (ASL_FILE_AML_OUTPUT, 0);
    AslGbl_SourceLine = 0;
    AslGbl_CurrentHexColumn = 0;
    LsPushNode (AslGbl_Files[ASL_FILE_INPUT].Filename);

    if (FileId == ASL_FILE_C_OFFSET_OUTPUT)
    {
        AslGbl_CurrentAmlOffset = 0;

        /* Offset table file has a special header and footer */

        LsDoOffsetTableHeader (FileId);

        TrWalkParseTree (AslGbl_ParseTreeRoot, ASL_WALK_VISIT_DOWNWARD,
            LsAmlOffsetWalk, NULL, (void *) ACPI_TO_POINTER (FileId));
        LsDoOffsetTableFooter (FileId);
        return;
    }

    /* Process all parse nodes */

    TrWalkParseTree (AslGbl_ParseTreeRoot, ASL_WALK_VISIT_DOWNWARD,
        LsAmlListingWalk, NULL, (void *) ACPI_TO_POINTER (FileId));

    /* Final processing */

    LsFinishSourceListing (FileId);
}


/*******************************************************************************
 *
 * FUNCTION:    LsAmlListingWalk
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Process one node during a listing file generation.
 *
 ******************************************************************************/

static ACPI_STATUS
LsAmlListingWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    UINT8                   FileByte;
    UINT32                  i;
    UINT32                  FileId = (UINT32) ACPI_TO_INTEGER (Context);


    LsWriteNodeToListing (Op, FileId);

    if (Op->Asl.CompileFlags & OP_IS_RESOURCE_DATA)
    {
        /* Buffer is a resource template, don't dump the data all at once */

        return (AE_OK);
    }

    if ((FileId == ASL_FILE_ASM_INCLUDE_OUTPUT) ||
        (FileId == ASL_FILE_C_INCLUDE_OUTPUT))
    {
        return (AE_OK);
    }

    /* Write the hex bytes to the listing file(s) (if requested) */

    for (i = 0; i < Op->Asl.FinalAmlLength; i++)
    {
        if (ACPI_FAILURE (FlReadFile (ASL_FILE_AML_OUTPUT, &FileByte, 1)))
        {
            FlFileError (ASL_FILE_AML_OUTPUT, ASL_MSG_READ);
            AslAbort ();
        }

        LsWriteListingHexBytes (&FileByte, 1, FileId);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    LsDumpParseTree, LsTreeWriteWalk
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump entire parse tree, for compiler debug only
 *
 ******************************************************************************/

void
LsDumpParseTree (
    void)
{

    if (!AslGbl_DebugFlag)
    {
        return;
    }

    DbgPrint (ASL_TREE_OUTPUT, "\nOriginal parse tree from parser:\n\n");
    DbgPrint (ASL_TREE_OUTPUT, ASL_PARSE_TREE_HEADER1);

    TrWalkParseTree (AslGbl_ParseTreeRoot, ASL_WALK_VISIT_DOWNWARD,
        LsTreeWriteWalk, NULL, NULL);

    DbgPrint (ASL_TREE_OUTPUT, ASL_PARSE_TREE_HEADER1);
}


static ACPI_STATUS
LsTreeWriteWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{

    /* Dump ParseOp name and possible value */

    switch (Op->Asl.ParseOpcode)
    {
        case PARSEOP_NAMESEG:
        case PARSEOP_NAMESTRING:
        case PARSEOP_METHODCALL:
        case PARSEOP_STRING_LITERAL:

        UtDumpStringOp (Op, Level);
        break;

    case PARSEOP_BYTECONST:

        UtDumpIntegerOp (Op, Level, 2);
        break;

    case PARSEOP_WORDCONST:
    case PARSEOP_PACKAGE_LENGTH:

        UtDumpIntegerOp (Op, Level, 4);
        break;

    case PARSEOP_DWORDCONST:
    case PARSEOP_EISAID:

        UtDumpIntegerOp (Op, Level, 8);
        break;

    case PARSEOP_QWORDCONST:
    case PARSEOP_INTEGER:
    case PARSEOP_ONE:
    case PARSEOP_ZERO:
    case PARSEOP_ONES:

        UtDumpIntegerOp (Op, Level, 16);
        break;

    case PARSEOP_INCLUDE:

        DbgPrint (ASL_TREE_OUTPUT,
            "Open: %s\n", Op->Asl.Value.String);
        return (AE_OK);

    case PARSEOP_INCLUDE_END:

        DbgPrint (ASL_TREE_OUTPUT,
            "Close: %s\n", Op->Asl.Filename);
        return (AE_OK);

    default:

        UtDumpBasicOp (Op, Level);
        break;
    }

    /* Dump the remaining data */

    DbgPrint (ASL_TREE_OUTPUT, ASL_PARSE_TREE_DEBUG1,
        Op->Asl.ParseOpcode, Op->Asl.CompileFlags,
        Op->Asl.LineNumber, Op->Asl.EndLine,
        Op->Asl.LogicalLineNumber, Op->Asl.EndLogicalLine);

    TrPrintOpFlags (Op->Asl.CompileFlags, ASL_TREE_OUTPUT);
    DbgPrint (ASL_TREE_OUTPUT, "\n");
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    LsWriteNodeToListing
 *
 * PARAMETERS:  Op              - Parse node to write to the listing file.
 *              FileId          - ID of current listing file
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Write "a node" to the listing file. This means to
 *              1) Write out all of the source text associated with the node
 *              2) Write out all of the AML bytes associated with the node
 *              3) Write any compiler exceptions associated with the node
 *
 ******************************************************************************/

static void
LsWriteNodeToListing (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  FileId)
{
    const ACPI_OPCODE_INFO  *OpInfo;
    UINT32                  OpClass;
    char                    *Pathname;
    UINT32                  Length;
    UINT32                  i;


    OpInfo  = AcpiPsGetOpcodeInfo (Op->Asl.AmlOpcode);
    OpClass = OpInfo->Class;

    /* TBD: clean this up with a single flag that says:
     * I start a named output block
     */
    if (FileId == ASL_FILE_C_SOURCE_OUTPUT)
    {
        switch (Op->Asl.ParseOpcode)
        {
        case PARSEOP_DEFINITION_BLOCK:
        case PARSEOP_METHODCALL:
        case PARSEOP_INCLUDE:
        case PARSEOP_INCLUDE_END:
        case PARSEOP_DEFAULT_ARG:

            break;

        default:

            switch (OpClass)
            {
            case AML_CLASS_NAMED_OBJECT:

                switch (Op->Asl.AmlOpcode)
                {
                case AML_SCOPE_OP:
                case AML_ALIAS_OP:

                    break;

                default:

                    if (Op->Asl.ExternalName)
                    {
                        LsFlushListingBuffer (FileId);
                        FlPrintFile (FileId, "    };\n");
                    }
                    break;
                }
                break;

            default:

                /* Don't care about other objects */

                break;
            }
            break;
        }
    }

    /* These cases do not have a corresponding AML opcode */

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_DEFINITION_BLOCK:

        /* Always start a definition block at AML offset zero */

        AslGbl_CurrentAmlOffset = 0;
        LsWriteSourceLines (Op->Asl.EndLine, Op->Asl.EndLogicalLine, FileId);

        /* Use the table Signature and TableId to build a unique name */

        switch (FileId)
        {
        case ASL_FILE_ASM_SOURCE_OUTPUT:

            FlPrintFile (FileId,
                "%s_%s_Header \\\n",
                AslGbl_TableSignature, AslGbl_TableId);
            break;

        case ASL_FILE_C_SOURCE_OUTPUT:

            FlPrintFile (FileId,
                "    unsigned char    %s_%s_Header [] =\n    {\n",
                AslGbl_TableSignature, AslGbl_TableId);
            break;

        case ASL_FILE_ASM_INCLUDE_OUTPUT:

            FlPrintFile (FileId,
                "extrn %s_%s_Header : byte\n",
                AslGbl_TableSignature, AslGbl_TableId);
            break;

        case ASL_FILE_C_INCLUDE_OUTPUT:

            FlPrintFile (FileId,
                "extern unsigned char    %s_%s_Header [];\n",
                AslGbl_TableSignature, AslGbl_TableId);
            break;

        default:
            break;
        }

        return;


    case PARSEOP_METHODCALL:

        LsWriteSourceLines (Op->Asl.LineNumber, Op->Asl.LogicalLineNumber,
            FileId);
        return;


    case PARSEOP_INCLUDE:

        /* Flush everything up to and including the include source line */

        LsWriteSourceLines (Op->Asl.LineNumber, Op->Asl.LogicalLineNumber,
            FileId);

        /* Create a new listing node and push it */

        LsPushNode (Op->Asl.Value.String);
        return;


    case PARSEOP_INCLUDE_END:

        /* Flush out the rest of the include file */

        LsWriteSourceLines (Op->Asl.LineNumber, Op->Asl.LogicalLineNumber,
            FileId);

        /* Pop off this listing node and go back to the parent file */

        (void) LsPopNode ();
        return;


    case PARSEOP_DEFAULT_ARG:

        if (Op->Asl.CompileFlags & OP_IS_RESOURCE_DESC)
        {
            LsWriteSourceLines (Op->Asl.LineNumber, Op->Asl.EndLogicalLine,
                FileId);
        }
        return;


    default:

        /* All other opcodes have an AML opcode */

        break;
    }

    /*
     * Otherwise, we look at the AML opcode because we can
     * switch on the opcode type, getting an entire class
     * at once
     */
    switch (OpClass)
    {
    case AML_CLASS_ARGUMENT:       /* argument type only */
    case AML_CLASS_INTERNAL:

        break;

    case AML_CLASS_NAMED_OBJECT:

        switch (Op->Asl.AmlOpcode)
        {
        case AML_FIELD_OP:
        case AML_INDEX_FIELD_OP:
        case AML_BANK_FIELD_OP:
            /*
             * For fields, we want to dump all the AML after the
             * entire definition
             */
            LsWriteSourceLines (Op->Asl.EndLine, Op->Asl.EndLogicalLine,
                FileId);
            break;

        case AML_NAME_OP:

            if (Op->Asl.CompileFlags & OP_IS_RESOURCE_DESC)
            {
                LsWriteSourceLines (Op->Asl.LineNumber, Op->Asl.LogicalLineNumber,
                    FileId);
            }
            else
            {
                /*
                 * For fields, we want to dump all the AML after the
                 * entire definition
                 */
                LsWriteSourceLines (Op->Asl.EndLine, Op->Asl.EndLogicalLine,
                    FileId);
            }
            break;

        default:

            LsWriteSourceLines (Op->Asl.LineNumber, Op->Asl.LogicalLineNumber,
                FileId);
            break;
        }

        switch (Op->Asl.AmlOpcode)
        {
        case AML_SCOPE_OP:
        case AML_ALIAS_OP:

            /* These opcodes do not declare a new object, ignore them */

            break;

        default:

            /* All other named object opcodes come here */

            switch (FileId)
            {
            case ASL_FILE_ASM_SOURCE_OUTPUT:
            case ASL_FILE_C_SOURCE_OUTPUT:
            case ASL_FILE_ASM_INCLUDE_OUTPUT:
            case ASL_FILE_C_INCLUDE_OUTPUT:
                /*
                 * For named objects, we will create a valid symbol so that the
                 * AML code can be referenced from C or ASM
                 */
                if (Op->Asl.ExternalName)
                {
                    /* Get the full pathname associated with this node */

                    Pathname = AcpiNsGetExternalPathname (Op->Asl.Node);
                    Length = strlen (Pathname);
                    if (Length >= 4)
                    {
                        /* Convert all dots in the path to underscores */

                        for (i = 0; i < Length; i++)
                        {
                            if (Pathname[i] == '.')
                            {
                                Pathname[i] = '_';
                            }
                        }

                        /* Create the appropriate symbol in the output file */

                        switch (FileId)
                        {
                        case ASL_FILE_ASM_SOURCE_OUTPUT:

                            FlPrintFile (FileId,
                                "%s_%s_%s  \\\n",
                                AslGbl_TableSignature, AslGbl_TableId, &Pathname[1]);
                            break;

                        case ASL_FILE_C_SOURCE_OUTPUT:

                            FlPrintFile (FileId,
                                "    unsigned char    %s_%s_%s [] =\n    {\n",
                                AslGbl_TableSignature, AslGbl_TableId, &Pathname[1]);
                            break;

                        case ASL_FILE_ASM_INCLUDE_OUTPUT:

                            FlPrintFile (FileId,
                                "extrn %s_%s_%s : byte\n",
                                AslGbl_TableSignature, AslGbl_TableId, &Pathname[1]);
                            break;

                        case ASL_FILE_C_INCLUDE_OUTPUT:

                            FlPrintFile (FileId,
                                "extern unsigned char    %s_%s_%s [];\n",
                                AslGbl_TableSignature, AslGbl_TableId, &Pathname[1]);
                            break;

                        default:
                            break;
                        }
                    }

                    ACPI_FREE (Pathname);
                }
                break;

            default:

                /* Nothing to do for listing file */

                break;
            }
        }
        break;

    case AML_CLASS_EXECUTE:
    case AML_CLASS_CREATE:
    default:

        if ((Op->Asl.ParseOpcode == PARSEOP_BUFFER) &&
            (Op->Asl.CompileFlags & OP_IS_RESOURCE_DESC))
        {
            return;
        }

        LsWriteSourceLines (Op->Asl.LineNumber, Op->Asl.LogicalLineNumber,
            FileId);
        break;

    case AML_CLASS_UNKNOWN:

        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    LsFinishSourceListing
 *
 * PARAMETERS:  FileId          - ID of current listing file.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Cleanup routine for the listing file. Flush the hex AML
 *              listing buffer, and flush out any remaining lines in the
 *              source input file.
 *
 ******************************************************************************/

static void
LsFinishSourceListing (
    UINT32                  FileId)
{

    if ((FileId == ASL_FILE_ASM_INCLUDE_OUTPUT) ||
        (FileId == ASL_FILE_C_INCLUDE_OUTPUT))
    {
        return;
    }

    LsFlushListingBuffer (FileId);
    AslGbl_CurrentAmlOffset = 0;

    /* Flush any remaining text in the source file */

    if (FileId == ASL_FILE_C_SOURCE_OUTPUT)
    {
        FlPrintFile (FileId, "    /*\n");
    }

    while (LsWriteOneSourceLine (FileId))
    { ; }

    if (FileId == ASL_FILE_C_SOURCE_OUTPUT)
    {
        FlPrintFile (FileId, "\n     */\n    };\n");
    }

    FlPrintFile (FileId, "\n");

    if (FileId == ASL_FILE_LISTING_OUTPUT)
    {
        /* Print a summary of the compile exceptions */

        FlPrintFile (FileId, "\n\nSummary of errors and warnings\n\n");
        AePrintErrorLog (FileId);
        FlPrintFile (FileId, "\n");
        UtDisplaySummary (FileId);
        FlPrintFile (FileId, "\n");
    }
}
