/******************************************************************************
 *
 * Module Name: asltree - Parse tree management
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
#include <contrib/dev/acpica/include/acapps.h>

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("asltree")


/*******************************************************************************
 *
 * FUNCTION:    TrSetOpIntegerValue
 *
 * PARAMETERS:  ParseOpcode         - New opcode to be assigned to the op
 *              Op                  - An existing parse op
 *
 * RETURN:      The updated op
 *
 * DESCRIPTION: Used to set the integer value of a op,
 *              usually to a specific size (8, 16, 32, or 64 bits)
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrSetOpIntegerValue (
    UINT32                  ParseOpcode,
    ACPI_PARSE_OBJECT       *Op)
{

    if (!Op)
    {
        return (NULL);
    }

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nUpdateOp: Old - %s, New - %s\n",
        UtGetOpName (Op->Asl.ParseOpcode),
        UtGetOpName (ParseOpcode));

    /* Assign new opcode and name */

    if (Op->Asl.ParseOpcode == PARSEOP_ONES)
    {
        switch (ParseOpcode)
        {
        case PARSEOP_BYTECONST:

            Op->Asl.Value.Integer = ACPI_UINT8_MAX;
            break;

        case PARSEOP_WORDCONST:

            Op->Asl.Value.Integer = ACPI_UINT16_MAX;
            break;

        case PARSEOP_DWORDCONST:

            Op->Asl.Value.Integer = ACPI_UINT32_MAX;
            break;

        /* Don't need to do the QWORD case */

        default:

            /* Don't care about others */
            break;
        }
    }

    Op->Asl.ParseOpcode = (UINT16) ParseOpcode;
    UtSetParseOpName (Op);

    /*
     * For the BYTE, WORD, and DWORD constants, make sure that the integer
     * that was passed in will actually fit into the data type
     */
    switch (ParseOpcode)
    {
    case PARSEOP_BYTECONST:

        UtCheckIntegerRange (Op, 0x00, ACPI_UINT8_MAX);
        Op->Asl.Value.Integer &= ACPI_UINT8_MAX;
        break;

    case PARSEOP_WORDCONST:

        UtCheckIntegerRange (Op, 0x00, ACPI_UINT16_MAX);
        Op->Asl.Value.Integer &= ACPI_UINT16_MAX;
        break;

    case PARSEOP_DWORDCONST:

        UtCheckIntegerRange (Op, 0x00, ACPI_UINT32_MAX);
        Op->Asl.Value.Integer &= ACPI_UINT32_MAX;
        break;

    default:

        /* Don't care about others, don't need to check QWORD */

        break;
    }

    /* Converter: if this is a method invocation, turn off capture comments */

    if (AcpiGbl_CaptureComments &&
        (ParseOpcode == PARSEOP_METHODCALL))
    {
        AslGbl_CommentState.CaptureComments = FALSE;
    }

    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrSetOpFlags
 *
 * PARAMETERS:  Op                  - An existing parse op
 *              Flags               - New flags word
 *
 * RETURN:      The updated parser op
 *
 * DESCRIPTION: Set bits in the op flags word. Will not clear bits, only set
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrSetOpFlags (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Flags)
{

    if (!Op)
    {
        return (NULL);
    }

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nSetOpFlags: %s Op %p, %8.8X", Op->Asl.ParseOpName, Op, Flags);

    TrPrintOpFlags (Flags, ASL_PARSE_OUTPUT);
    DbgPrint (ASL_PARSE_OUTPUT, "\n\n");

    Op->Asl.CompileFlags |= Flags;
    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrSetOpAmlLength
 *
 * PARAMETERS:  Op                  - An existing parse op
 *              Length              - AML Length
 *
 * RETURN:      The updated parser op
 *
 * DESCRIPTION: Set the AML Length in a op. Used by the parser to indicate
 *              the presence of a op that must be reduced to a fixed length
 *              constant.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrSetOpAmlLength (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Length)
{

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nSetOpAmlLength: Op %p, %8.8X\n", Op, Length);

    if (!Op)
    {
        return (NULL);
    }

    Op->Asl.AmlLength = Length;
    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrSetOpParent
 *
 * PARAMETERS:  Op                  - To be set to new parent
 *              ParentOp            - The parent
 *
 * RETURN:      None, sets Op parent directly
 *
 * DESCRIPTION: Change the parent of a parse op.
 *
 ******************************************************************************/

void
TrSetOpParent (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *ParentOp)
{

    Op->Asl.Parent = ParentOp;
}


/*******************************************************************************
 *
 * FUNCTION:    TrSetOpCurrentFilename
 *
 * PARAMETERS:  Op                  - An existing parse op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Save the include file filename. Used for debug output only.
 *
 ******************************************************************************/

void
TrSetOpCurrentFilename (
    ACPI_PARSE_OBJECT       *Op)
{

    Op->Asl.Filename = AslGbl_PreviousIncludeFilename;
}


/*******************************************************************************
 *
 * FUNCTION:    TrSetOpIntegerWidth
 *
 * PARAMETERS:  Op                  - An existing parse op
 *
 * RETURN:      None
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

void
TrSetOpIntegerWidth (
    ACPI_PARSE_OBJECT       *TableSignatureOp,
    ACPI_PARSE_OBJECT       *RevisionOp)
{

    /* TBD: Check table sig? (DSDT vs. SSDT) */

    /* Handle command-line version override */

    if (AslGbl_RevisionOverride)
    {
        AcpiUtSetIntegerWidth (AslGbl_RevisionOverride);
    }
    else
    {
        AcpiUtSetIntegerWidth ((UINT8) RevisionOp->Asl.Value.Integer);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    TrSetOpEndLineNumber
 *
 * PARAMETERS:  Op                - An existing parse op
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Set the ending line numbers (file line and logical line) of a
 *              parse op to the current line numbers.
 *
 ******************************************************************************/

void
TrSetOpEndLineNumber (
    ACPI_PARSE_OBJECT       *Op)
{

    /* If the end line # is already set, just return */

    if (Op->Asl.EndLine)
    {
        return;
    }

    Op->Asl.EndLine = AslGbl_CurrentLineNumber;
    Op->Asl.EndLogicalLine = AslGbl_LogicalLineNumber;
}


/*******************************************************************************
 *
 * FUNCTION:    TrLinkOpChildren
 *
 * PARAMETERS:  Op                - An existing parse op
 *              NumChildren        - Number of children to follow
 *              ...                - A list of child ops to link to the new
 *                                   op. NumChildren long.
 *
 * RETURN:      The updated (linked) op
 *
 * DESCRIPTION: Link a group of ops to an existing parse op
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrLinkOpChildren (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  NumChildren,
    ...)
{
    ACPI_PARSE_OBJECT       *Child;
    ACPI_PARSE_OBJECT       *PrevChild;
    va_list                 ap;
    UINT32                  i;
    BOOLEAN                 FirstChild;


    va_start (ap, NumChildren);

    TrSetOpEndLineNumber (Op);

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nLinkChildren  Line [%u to %u] NewParent %p Child %u Op %s  ",
        Op->Asl.LineNumber, Op->Asl.EndLine,
        Op, NumChildren, UtGetOpName(Op->Asl.ParseOpcode));

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_ASL_CODE:

        AslGbl_ParseTreeRoot = Op;
        Op->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
        DbgPrint (ASL_PARSE_OUTPUT, "ASLCODE (Tree Completed)->");
        break;

    case PARSEOP_DEFINITION_BLOCK:

        DbgPrint (ASL_PARSE_OUTPUT, "DEFINITION_BLOCK (Tree Completed)->");
        break;

    case PARSEOP_OPERATIONREGION:

        DbgPrint (ASL_PARSE_OUTPUT, "OPREGION->");
        break;

    case PARSEOP_OR:

        DbgPrint (ASL_PARSE_OUTPUT, "OR->");
        break;

    default:

        /* Nothing to do for other opcodes */

        break;
    }

    /* The following is for capturing comments */

    if (AcpiGbl_CaptureComments)
    {
        /*
         * If there are "regular comments" detected at this point,
         * then is an endBlk comment. Categorize it as so and distribute
         * all regular comments to this parse op.
         */
        if (AslGbl_CommentListHead)
        {
            Op->Asl.EndBlkComment = AslGbl_CommentListHead;
            CvDbgPrint ("EndBlk Comment for %s: %s",
                Op->Asl.ParseOpName, AslGbl_CommentListHead->Comment);
            AslGbl_CommentListHead = NULL;
            AslGbl_CommentListTail = NULL;
        }
    }

    /* Link the new op to it's children */

    PrevChild = NULL;
    FirstChild = TRUE;
    for (i = 0; i < NumChildren; i++)
    {
        Child = va_arg (ap, ACPI_PARSE_OBJECT *);

        if ((Child == PrevChild) && (Child != NULL))
        {
            AslError (ASL_WARNING, ASL_MSG_COMPILER_INTERNAL, Child,
                "Child op list invalid");
            va_end(ap);
            return (Op);
        }

        DbgPrint (ASL_PARSE_OUTPUT, "%p, ", Child);

        /*
         * If child is NULL, this means that an optional argument
         * was omitted. We must create a placeholder with a special
         * opcode (DEFAULT_ARG) so that the code generator will know
         * that it must emit the correct default for this argument
         */
        if (!Child)
        {
            Child = TrAllocateOp (PARSEOP_DEFAULT_ARG);
        }

        /* Link first child to parent */

        if (FirstChild)
        {
            FirstChild = FALSE;
            Op->Asl.Child = Child;
        }

        /* Point all children to parent */

        Child->Asl.Parent = Op;

        /* Link children in a peer list */

        if (PrevChild)
        {
            PrevChild->Asl.Next = Child;
        }

        /*
         * This child might be a list, point all ops in the list
         * to the same parent
         */
        while (Child->Asl.Next)
        {
            Child = Child->Asl.Next;
            Child->Asl.Parent = Op;
        }

        PrevChild = Child;
    }

    va_end(ap);
    DbgPrint (ASL_PARSE_OUTPUT, "\n\n");

    if (AcpiGbl_CaptureComments)
    {
        AslGbl_CommentState.LatestParseOp = Op;
        CvDbgPrint ("TrLinkOpChildren=====Set latest parse op to this op.\n");
    }

    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrLinkPeerOp
 *
 * PARAMETERS:  Op1           - First peer
 *              Op2           - Second peer
 *
 * RETURN:      Op1 or the non-null op.
 *
 * DESCRIPTION: Link two ops as peers. Handles cases where one peer is null.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrLinkPeerOp (
    ACPI_PARSE_OBJECT       *Op1,
    ACPI_PARSE_OBJECT       *Op2)
{
    ACPI_PARSE_OBJECT       *Next;


    DbgPrint (ASL_PARSE_OUTPUT,
        "\nLinkPeerOp: 1=%p (%s), 2=%p (%s)\n",
        Op1, Op1 ? UtGetOpName(Op1->Asl.ParseOpcode) : NULL,
        Op2, Op2 ? UtGetOpName(Op2->Asl.ParseOpcode) : NULL);


    if ((!Op1) && (!Op2))
    {
        DbgPrint (ASL_PARSE_OUTPUT, "\nTwo Null ops!\n");
        return (Op1);
    }

    /* If one of the ops is null, just return the non-null op */

    if (!Op2)
    {
        return (Op1);
    }

    if (!Op1)
    {
        return (Op2);
    }

    if (Op1 == Op2)
    {
        DbgPrint (ASL_DEBUG_OUTPUT,
            "\n************* Internal error, linking op to itself %p\n",
            Op1);
        AslError (ASL_WARNING, ASL_MSG_COMPILER_INTERNAL, Op1,
            "Linking op to itself");
        return (Op1);
    }

    Op1->Asl.Parent = Op2->Asl.Parent;

    /*
     * Op 1 may already have a peer list (such as an IF/ELSE pair),
     * so we must walk to the end of the list and attach the new
     * peer at the end
     */
    Next = Op1;
    while (Next->Asl.Next)
    {
        Next = Next->Asl.Next;
    }

    Next->Asl.Next = Op2;
    return (Op1);
}


/*******************************************************************************
 *
 * FUNCTION:    TrLinkPeerOps
 *
 * PARAMETERS:  NumPeers            - The number of ops in the list to follow
 *              ...                 - A list of ops to link together as peers
 *
 * RETURN:      The first op in the list (head of the peer list)
 *
 * DESCRIPTION: Link together an arbitrary number of peer ops.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrLinkPeerOps (
    UINT32                  NumPeers,
    ...)
{
    ACPI_PARSE_OBJECT       *This;
    ACPI_PARSE_OBJECT       *Next;
    va_list                 ap;
    UINT32                  i;
    ACPI_PARSE_OBJECT       *Start;


    DbgPrint (ASL_PARSE_OUTPUT,
        "\nLinkPeerOps: (%u) ", NumPeers);

    va_start (ap, NumPeers);
    This = va_arg (ap, ACPI_PARSE_OBJECT *);
    Start = This;

    /*
     * Link all peers
     */
    for (i = 0; i < (NumPeers -1); i++)
    {
        DbgPrint (ASL_PARSE_OUTPUT, "%u=%p ", (i+1), This);

        while (This->Asl.Next)
        {
            This = This->Asl.Next;
        }

        /* Get another peer op */

        Next = va_arg (ap, ACPI_PARSE_OBJECT *);
        if (!Next)
        {
            Next = TrAllocateOp (PARSEOP_DEFAULT_ARG);
        }

        /* link new op to the current op */

        This->Asl.Next = Next;
        This = Next;
    }

    va_end (ap);
    DbgPrint (ASL_PARSE_OUTPUT,"\n");
    return (Start);
}


/*******************************************************************************
 *
 * FUNCTION:    TrLinkChildOp
 *
 * PARAMETERS:  Op1           - Parent op
 *              Op2           - Op to become a child
 *
 * RETURN:      The parent op
 *
 * DESCRIPTION: Link two ops together as a parent and child
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrLinkChildOp (
    ACPI_PARSE_OBJECT       *Op1,
    ACPI_PARSE_OBJECT       *Op2)
{
    ACPI_PARSE_OBJECT       *Next;


    DbgPrint (ASL_PARSE_OUTPUT,
        "\nLinkChildOp: Parent=%p (%s), Child=%p (%s)\n",
        Op1, Op1 ? UtGetOpName(Op1->Asl.ParseOpcode): NULL,
        Op2, Op2 ? UtGetOpName(Op2->Asl.ParseOpcode): NULL);

    /*
     * Converter: if TrLinkChildOp is called to link a method call,
     * turn on capture comments as it signifies that we are done parsing
     * a method call.
     */
    if (AcpiGbl_CaptureComments && Op1)
    {
        if (Op1->Asl.ParseOpcode == PARSEOP_METHODCALL)
        {
            AslGbl_CommentState.CaptureComments = TRUE;
        }
        AslGbl_CommentState.LatestParseOp = Op1;
    }

    if (!Op1 || !Op2)
    {
        return (Op1);
    }

    Op1->Asl.Child = Op2;

    /* Set the child and all peers of the child to point to the parent */

    Next = Op2;
    while (Next)
    {
        Next->Asl.Parent = Op1;
        Next = Next->Asl.Next;
    }

    return (Op1);
}


/*******************************************************************************
 *
 * FUNCTION:    TrWalkParseTree
 *
 * PARAMETERS:  Op                      - Walk starting point
 *              Visitation              - Type of walk
 *              DescendingCallback      - Called during tree descent
 *              AscendingCallback       - Called during tree ascent
 *              Context                 - To be passed to the callbacks
 *
 * RETURN:      Status from callback(s)
 *
 * DESCRIPTION: Walk the entire parse tree.
 *
 ******************************************************************************/

ACPI_STATUS
TrWalkParseTree (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Visitation,
    ASL_WALK_CALLBACK       DescendingCallback,
    ASL_WALK_CALLBACK       AscendingCallback,
    void                    *Context)
{
    UINT32                  Level;
    BOOLEAN                 OpPreviouslyVisited;
    ACPI_PARSE_OBJECT       *StartOp = Op;
    ACPI_STATUS             Status;


    if (!AslGbl_ParseTreeRoot)
    {
        return (AE_OK);
    }

    Level = 0;
    OpPreviouslyVisited = FALSE;

    switch (Visitation)
    {
    case ASL_WALK_VISIT_DOWNWARD:

        while (Op)
        {
            if (!OpPreviouslyVisited)
            {
                /* Let the callback process the op. */

                Status = DescendingCallback (Op, Level, Context);
                if (ACPI_SUCCESS (Status))
                {
                    /* Visit children first, once */

                    if (Op->Asl.Child)
                    {
                        Level++;
                        Op = Op->Asl.Child;
                        continue;
                    }
                }
                else if (Status != AE_CTRL_DEPTH)
                {
                    /* Exit immediately on any error */

                    return (Status);
                }
            }

            /* Terminate walk at start op */

            if (Op == StartOp)
            {
                break;
            }

            /* No more children, visit peers */

            if (Op->Asl.Next)
            {
                Op = Op->Asl.Next;
                OpPreviouslyVisited = FALSE;
            }
            else
            {
                /* No children or peers, re-visit parent */

                if (Level != 0 )
                {
                    Level--;
                }
                Op = Op->Asl.Parent;
                OpPreviouslyVisited = TRUE;
            }
        }
        break;

    case ASL_WALK_VISIT_UPWARD:

        while (Op)
        {
            /* Visit leaf op (no children) or parent op on return trip */

            if ((!Op->Asl.Child) ||
                (OpPreviouslyVisited))
            {
                /* Let the callback process the op. */

                Status = AscendingCallback (Op, Level, Context);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
            }
            else
            {
                /* Visit children first, once */

                Level++;
                Op = Op->Asl.Child;
                continue;
            }

            /* Terminate walk at start op */

            if (Op == StartOp)
            {
                break;
            }

            /* No more children, visit peers */

            if (Op->Asl.Next)
            {
                Op = Op->Asl.Next;
                OpPreviouslyVisited = FALSE;
            }
            else
            {
                /* No children or peers, re-visit parent */

                if (Level != 0 )
                {
                    Level--;
                }
                Op = Op->Asl.Parent;
                OpPreviouslyVisited = TRUE;
            }
        }
        break;

     case ASL_WALK_VISIT_TWICE:

        while (Op)
        {
            if (OpPreviouslyVisited)
            {
                Status = AscendingCallback (Op, Level, Context);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
            }
            else
            {
                /* Let the callback process the op. */

                Status = DescendingCallback (Op, Level, Context);
                if (ACPI_SUCCESS (Status))
                {
                    /* Visit children first, once */

                    if (Op->Asl.Child)
                    {
                        Level++;
                        Op = Op->Asl.Child;
                        continue;
                    }
                }
                else if (Status != AE_CTRL_DEPTH)
                {
                    /* Exit immediately on any error */

                    return (Status);
                }
            }

            /* Terminate walk at start op */

            if (Op == StartOp)
            {
                break;
            }

            /* No more children, visit peers */

            if (Op->Asl.Next)
            {
                Op = Op->Asl.Next;
                OpPreviouslyVisited = FALSE;
            }
            else
            {
                /* No children or peers, re-visit parent */

                if (Level != 0 )
                {
                    Level--;
                }
                Op = Op->Asl.Parent;
                OpPreviouslyVisited = TRUE;
            }
        }
        break;

    default:
        /* No other types supported */
        break;
    }

    /* If we get here, the walk completed with no errors */

    return (AE_OK);
}
