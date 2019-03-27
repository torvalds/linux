/******************************************************************************
 *
 * Module Name: aslprintf - ASL Printf/Fprintf macro support
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

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslprintf")


/* Local prototypes */

static void
OpcCreateConcatenateNode (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *Node);

static void
OpcParsePrintf (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *DestOp);


/*******************************************************************************
 *
 * FUNCTION:    OpcDoPrintf
 *
 * PARAMETERS:  Op                  - printf parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert printf macro to a Store(..., Debug) AML operation.
 *
 ******************************************************************************/

void
OpcDoPrintf (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *DestOp;


    /* Store destination is the Debug op */

    DestOp = TrAllocateOp (PARSEOP_DEBUG);
    DestOp->Asl.AmlOpcode = AML_DEBUG_OP;
    DestOp->Asl.Parent = Op;
    DestOp->Asl.LogicalLineNumber = Op->Asl.LogicalLineNumber;

    OpcParsePrintf (Op, DestOp);
}


/*******************************************************************************
 *
 * FUNCTION:    OpcDoFprintf
 *
 * PARAMETERS:  Op                  - fprintf parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert fprintf macro to a Store AML operation.
 *
 ******************************************************************************/

void
OpcDoFprintf (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *DestOp;


    /* Store destination is the first argument of fprintf */

    DestOp = Op->Asl.Child;
    Op->Asl.Child = DestOp->Asl.Next;
    DestOp->Asl.Next = NULL;

    OpcParsePrintf (Op, DestOp);
}


/*******************************************************************************
 *
 * FUNCTION:    OpcParsePrintf
 *
 * PARAMETERS:  Op                  - Printf parse node
 *              DestOp              - Destination of Store operation
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert printf macro to a Store AML operation. The printf
 *              macro parse tree is laid out as follows:
 *
 *              Op        - printf parse op
 *              Op->Child - Format string
 *              Op->Next  - Format string arguments
 *
 ******************************************************************************/

static void
OpcParsePrintf (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *DestOp)
{
    char                    *Format;
    char                    *StartPosition = NULL;
    ACPI_PARSE_OBJECT       *ArgNode;
    ACPI_PARSE_OBJECT       *NextNode;
    UINT32                  StringLength = 0;
    char                    *NewString;
    BOOLEAN                 StringToProcess = FALSE;
    ACPI_PARSE_OBJECT       *NewOp;


    /* Get format string */

    Format = ACPI_CAST_PTR (char, Op->Asl.Child->Asl.Value.String);
    ArgNode = Op->Asl.Child->Asl.Next;

    /*
     * Detach argument list so that we can use a NULL check to distinguish
     * the first concatenation operation we need to make
     */
    Op->Asl.Child = NULL;

    for (; *Format; ++Format)
    {
        if (*Format != '%')
        {
            if (!StringToProcess)
            {
                /* Mark the beginning of a string */

                StartPosition = Format;
                StringToProcess = TRUE;
            }

            ++StringLength;
            continue;
        }

        /* Save string, if any, to new string object and concat it */

        if (StringToProcess)
        {
            NewString = UtLocalCacheCalloc (StringLength + 1);
            strncpy (NewString, StartPosition, StringLength);

            NewOp = TrAllocateOp (PARSEOP_STRING_LITERAL);
            NewOp->Asl.Value.String = NewString;
            NewOp->Asl.AmlOpcode = AML_STRING_OP;
            NewOp->Asl.AcpiBtype = ACPI_BTYPE_STRING;
            NewOp->Asl.LogicalLineNumber = Op->Asl.LogicalLineNumber;

            OpcCreateConcatenateNode(Op, NewOp);

            StringLength = 0;
            StringToProcess = FALSE;
        }

        ++Format;

        /*
         * We have a format parameter and will need an argument to go
         * with it
         */
        if (!ArgNode ||
            ArgNode->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
        {
            AslError(ASL_ERROR, ASL_MSG_ARG_COUNT_LO, Op, NULL);
            return;
        }

        /*
         * We do not support sub-specifiers of printf (flags, width,
         * precision, length). For specifiers we only support %x/%X for
         * hex or %s for strings. Also, %o for generic "acpi object".
         */
        switch (*Format)
        {
        case 's':

            if (ArgNode->Asl.ParseOpcode != PARSEOP_STRING_LITERAL)
            {
                AslError(ASL_ERROR, ASL_MSG_INVALID_TYPE, ArgNode,
                    "String required");
                return;
            }

            NextNode = ArgNode->Asl.Next;
            ArgNode->Asl.Next = NULL;
            OpcCreateConcatenateNode(Op, ArgNode);
            ArgNode = NextNode;
            continue;

        case 'X':
        case 'x':
        case 'o':

            NextNode = ArgNode->Asl.Next;
            ArgNode->Asl.Next = NULL;

            /*
             * Append an empty string if the first argument is
             * not a string. This will implicitly conver the 2nd
             * concat source to a string per the ACPI specification.
             */
            if (!Op->Asl.Child)
            {
                NewOp = TrAllocateOp (PARSEOP_STRING_LITERAL);
                NewOp->Asl.Value.String = "";
                NewOp->Asl.AmlOpcode = AML_STRING_OP;
                NewOp->Asl.AcpiBtype = ACPI_BTYPE_STRING;
                NewOp->Asl.LogicalLineNumber = Op->Asl.LogicalLineNumber;

                OpcCreateConcatenateNode(Op, NewOp);
            }

            OpcCreateConcatenateNode(Op, ArgNode);
            ArgNode = NextNode;
            break;

        default:

            AslError(ASL_ERROR, ASL_MSG_INVALID_OPERAND, Op,
                "Unrecognized format specifier");
            continue;
        }
    }

    /* Process any remaining string */

    if (StringToProcess)
    {
        NewString = UtLocalCacheCalloc (StringLength + 1);
        strncpy (NewString, StartPosition, StringLength);

        NewOp = TrAllocateOp (PARSEOP_STRING_LITERAL);
        NewOp->Asl.Value.String = NewString;
        NewOp->Asl.AcpiBtype = ACPI_BTYPE_STRING;
        NewOp->Asl.AmlOpcode = AML_STRING_OP;
        NewOp->Asl.LogicalLineNumber = Op->Asl.LogicalLineNumber;

        OpcCreateConcatenateNode(Op, NewOp);
    }

    /*
     * If we get here and there's no child node then Format
     * was an empty string. Just make a no op.
     */
    if (!Op->Asl.Child)
    {
        Op->Asl.ParseOpcode = PARSEOP_NOOP;
        AslError(ASL_WARNING, ASL_MSG_NULL_STRING, Op,
            "Converted to NOOP");
        return;
    }

     /* Check for erroneous extra arguments */

    if (ArgNode &&
        ArgNode->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
    {
        AslError(ASL_WARNING, ASL_MSG_ARG_COUNT_HI, ArgNode,
            "Extra arguments ignored");
    }

    /* Change Op to a Store */

    Op->Asl.ParseOpcode = PARSEOP_STORE;
    Op->Common.AmlOpcode = AML_STORE_OP;
    Op->Asl.CompileFlags  = 0;

    /* Disable further optimization */

    Op->Asl.CompileFlags &= ~OP_COMPILE_TIME_CONST;
    UtSetParseOpName (Op);

    /* Set Store destination */

    Op->Asl.Child->Asl.Next = DestOp;
}


/*******************************************************************************
 *
 * FUNCTION:    OpcCreateConcatenateNode
 *
 * PARAMETERS:  Op                  - Parse node
 *              Node                - Parse node to be concatenated
 *
 * RETURN:      None
 *
 * DESCRIPTION: Make Node the child of Op. If child node already exists, then
 *              concat child with Node and makes concat node the child of Op.
 *
 ******************************************************************************/

static void
OpcCreateConcatenateNode (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *Node)
{
    ACPI_PARSE_OBJECT       *NewConcatOp;


    if (!Op->Asl.Child)
    {
        Op->Asl.Child = Node;
        Node->Asl.Parent = Op;
        return;
    }

    NewConcatOp = TrAllocateOp (PARSEOP_CONCATENATE);
    NewConcatOp->Asl.AmlOpcode = AML_CONCATENATE_OP;
    NewConcatOp->Asl.AcpiBtype = 0x7;
    NewConcatOp->Asl.LogicalLineNumber = Op->Asl.LogicalLineNumber;

    /* First arg is child of Op*/

    NewConcatOp->Asl.Child = Op->Asl.Child;
    Op->Asl.Child->Asl.Parent = NewConcatOp;

    /* Second arg is Node */

    NewConcatOp->Asl.Child->Asl.Next = Node;
    Node->Asl.Parent = NewConcatOp;

    /* Third arg is Zero (not used) */

    NewConcatOp->Asl.Child->Asl.Next->Asl.Next =
        TrAllocateOp (PARSEOP_ZERO);
    NewConcatOp->Asl.Child->Asl.Next->Asl.Next->Asl.Parent =
        NewConcatOp;

    Op->Asl.Child = NewConcatOp;
    NewConcatOp->Asl.Parent = Op;
}
