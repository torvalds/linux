/******************************************************************************
 *
 * Module Name: aslparseop - Parse op create/allocate/cache interfaces
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
#include <contrib/dev/acpica/include/acconvert.h>

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslparseop")


/*******************************************************************************
 *
 * FUNCTION:    TrCreateOp
 *
 * PARAMETERS:  ParseOpcode         - Opcode to be assigned to the op
 *              NumChildren         - Number of children to follow
 *              ...                 - A list of child ops to link to the new
 *                                    op. NumChildren long.
 *
 * RETURN:      Pointer to the new op. Aborts on allocation failure
 *
 * DESCRIPTION: Create a new parse op and link together a list of child
 *              ops underneath the new op.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrCreateOp (
    UINT32                  ParseOpcode,
    UINT32                  NumChildren,
    ...)
{
    ACPI_PARSE_OBJECT       *Op;
    ACPI_PARSE_OBJECT       *Child;
    ACPI_PARSE_OBJECT       *PrevChild;
    va_list                 ap;
    UINT32                  i;
    BOOLEAN                 FirstChild;


    va_start (ap, NumChildren);

    /* Allocate one new op */

    Op = TrAllocateOp (ParseOpcode);

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nCreateOp  Ln/Col %u/%u NewParent %p Child %u Op %s  ",
        Op->Asl.LineNumber, Op->Asl.Column, Op,
        NumChildren, UtGetOpName(ParseOpcode));

    /* Some extra debug output based on the parse opcode */

    switch (ParseOpcode)
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

    /* Link the new op to its children */

    PrevChild = NULL;
    FirstChild = TRUE;
    for (i = 0; i < NumChildren; i++)
    {
        /* Get the next child */

        Child = va_arg (ap, ACPI_PARSE_OBJECT *);
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

            /*
             * For the ASL-/ASL+ converter: if the ParseOp is a Connection,
             * External, Offset or AccessAs, it means that the comments in the
             * FirstChild belongs to their parent due to the parsing order in
             * the .y files. To correct this, take the comments in the
             * FirstChild place it in the parent. This also means that
             * legitimate comments for the child gets put to the parent.
             */
            if (AcpiGbl_CaptureComments &&
                ((ParseOpcode == PARSEOP_CONNECTION) ||
                 (ParseOpcode == PARSEOP_EXTERNAL) ||
                 (ParseOpcode == PARSEOP_OFFSET) ||
                 (ParseOpcode == PARSEOP_ACCESSAS)))
            {
                Op->Asl.CommentList      = Child->Asl.CommentList;
                Op->Asl.EndBlkComment    = Child->Asl.EndBlkComment;
                Op->Asl.InlineComment    = Child->Asl.InlineComment;
                Op->Asl.FileChanged      = Child->Asl.FileChanged;

                Child->Asl.CommentList   = NULL;
                Child->Asl.EndBlkComment = NULL;
                Child->Asl.InlineComment = NULL;
                Child->Asl.FileChanged   = FALSE;

                /*
                 * These do not need to be "passed off". They can be copied
                 * because the code for these opcodes should be printed in the
                 * same file.
                 */
                Op->Asl.Filename         = Child->Asl.Filename;
                Op->Asl.ParentFilename   = Child->Asl.ParentFilename;
            }
        }

        /* Point all children to parent */

        Child->Asl.Parent = Op;

        /* Link children in a peer list */

        if (PrevChild)
        {
            PrevChild->Asl.Next = Child;
        };

        /* Get the comment from last child in the resource template call */

        if (AcpiGbl_CaptureComments &&
            (Op->Asl.ParseOpcode == PARSEOP_RESOURCETEMPLATE))
        {
            CvDbgPrint ("Transferred current comment list to this op.\n");
            Op->Asl.CommentList = Child->Asl.CommentList;
            Child->Asl.CommentList = NULL;

            Op->Asl.InlineComment = Child->Asl.InlineComment;
            Child->Asl.InlineComment = NULL;
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
    DbgPrint (ASL_PARSE_OUTPUT, "\n");
    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrCreateLeafOp
 *
 * PARAMETERS:  ParseOpcode         - New opcode to be assigned to the op
 *
 * RETURN:      Pointer to the new op. Aborts on allocation failure
 *
 * DESCRIPTION: Create a simple leaf op (no children or peers, and no value
 *              assigned to the op)
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrCreateLeafOp (
    UINT32                  ParseOpcode)
{
    ACPI_PARSE_OBJECT       *Op;


    Op = TrAllocateOp (ParseOpcode);

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nCreateLeafOp  Ln/Col %u/%u NewOp %p  Op %s\n\n",
        Op->Asl.LineNumber, Op->Asl.Column, Op, UtGetOpName (ParseOpcode));

    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrCreateValuedLeafOp
 *
 * PARAMETERS:  ParseOpcode         - New opcode to be assigned to the op
 *              Value               - Value to be assigned to the op
 *
 * RETURN:      Pointer to the new op. Aborts on allocation failure
 *
 * DESCRIPTION: Create a leaf op (no children or peers) with a value
 *              assigned to it
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrCreateValuedLeafOp (
    UINT32                  ParseOpcode,
    UINT64                  Value)
{
    ACPI_PARSE_OBJECT       *Op;


    Op = TrAllocateOp (ParseOpcode);
    Op->Asl.Value.Integer = Value;

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nCreateValuedLeafOp  Ln/Col %u/%u NewOp %p  "
        "Op %s  Value %8.8X%8.8X  ",
        Op->Asl.LineNumber, Op->Asl.Column, Op, UtGetOpName(ParseOpcode),
        ACPI_FORMAT_UINT64 (Value));

    switch (ParseOpcode)
    {
    case PARSEOP_STRING_LITERAL:

        DbgPrint (ASL_PARSE_OUTPUT, "STRING->%s", Value);
        break;

    case PARSEOP_NAMESEG:

        DbgPrint (ASL_PARSE_OUTPUT, "NAMESEG->%s", Value);
        break;

    case PARSEOP_NAMESTRING:

        DbgPrint (ASL_PARSE_OUTPUT, "NAMESTRING->%s", Value);
        break;

    case PARSEOP_EISAID:

        DbgPrint (ASL_PARSE_OUTPUT, "EISAID->%s", Value);
        break;

    case PARSEOP_METHOD:

        DbgPrint (ASL_PARSE_OUTPUT, "METHOD");
        break;

    case PARSEOP_INTEGER:

        DbgPrint (ASL_PARSE_OUTPUT, "INTEGER->%8.8X%8.8X",
            ACPI_FORMAT_UINT64 (Value));
        break;

    default:
        break;
    }

    DbgPrint (ASL_PARSE_OUTPUT, "\n\n");
    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrCreateTargetOp
 *
 * PARAMETERS:  OriginalOp          - Op to be copied
 *
 * RETURN:      Pointer to the new op. Aborts on allocation failure
 *
 * DESCRIPTION: Copy an existing op (and subtree). Used in ASL+ (C-style)
 *              expressions where the target is the same as one of the
 *              operands. A new op and subtree must be created from the
 *              original so that the parse tree can be linked properly.
 *
 * NOTE:        This code is specific to target operands that are the last
 *              operand in an ASL/AML operator. Meaning that the top-level
 *              parse Op in a possible subtree has a NULL Next pointer.
 *              This simplifies the recursion.
 *
 *              Subtree example:
 *                  DeRefOf (Local1) += 32
 *
 *              This gets converted to:
 *                  Add (DeRefOf (Local1), 32, DeRefOf (Local1))
 *
 *              Each DeRefOf has a single child, Local1. Even more complex
 *              subtrees can be created via the Index and DeRefOf operators.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrCreateTargetOp (
    ACPI_PARSE_OBJECT       *OriginalOp,
    ACPI_PARSE_OBJECT       *ParentOp)
{
    ACPI_PARSE_OBJECT       *Op;


    if (!OriginalOp)
    {
        return (NULL);
    }

    Op = UtParseOpCacheCalloc ();

    /* Copy the pertinent values (omit link pointer fields) */

    Op->Asl.Value               = OriginalOp->Asl.Value;
    Op->Asl.Filename            = OriginalOp->Asl.Filename;
    Op->Asl.LineNumber          = OriginalOp->Asl.LineNumber;
    Op->Asl.LogicalLineNumber   = OriginalOp->Asl.LogicalLineNumber;
    Op->Asl.LogicalByteOffset   = OriginalOp->Asl.LogicalByteOffset;
    Op->Asl.Column              = OriginalOp->Asl.Column;
    Op->Asl.Flags               = OriginalOp->Asl.Flags;
    Op->Asl.CompileFlags        = OriginalOp->Asl.CompileFlags;
    Op->Asl.AmlOpcode           = OriginalOp->Asl.AmlOpcode;
    Op->Asl.ParseOpcode         = OriginalOp->Asl.ParseOpcode;
    Op->Asl.Parent              = ParentOp;

    UtSetParseOpName (Op);

    /* Copy a possible subtree below this op */

    if (OriginalOp->Asl.Child)
    {
        Op->Asl.Child = TrCreateTargetOp (OriginalOp->Asl.Child, Op);
    }

    if (OriginalOp->Asl.Next) /* Null for top-level op */
    {
        Op->Asl.Next = TrCreateTargetOp (OriginalOp->Asl.Next, ParentOp);
    }

    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrCreateAssignmentOp
 *
 * PARAMETERS:  Target              - Assignment target
 *              Source              - Assignment source
 *
 * RETURN:      Pointer to the new op. Aborts on allocation failure
 *
 * DESCRIPTION: Implements the C-style '=' operator. It changes the parse
 *              tree if possible to utilize the last argument of the math
 *              operators which is a target operand -- thus saving invocation
 *              of and additional Store() operator. An optimization.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrCreateAssignmentOp (
    ACPI_PARSE_OBJECT       *Target,
    ACPI_PARSE_OBJECT       *Source)
{
    ACPI_PARSE_OBJECT       *TargetOp;
    ACPI_PARSE_OBJECT       *SourceOp1;
    ACPI_PARSE_OBJECT       *SourceOp2;
    ACPI_PARSE_OBJECT       *Operator;


    DbgPrint (ASL_PARSE_OUTPUT,
        "\nTrCreateAssignmentOp  Line [%u to %u] Source %s Target %s\n",
        Source->Asl.LineNumber, Source->Asl.EndLine,
        UtGetOpName (Source->Asl.ParseOpcode),
        UtGetOpName (Target->Asl.ParseOpcode));

    TrSetOpFlags (Target, OP_IS_TARGET);

    switch (Source->Asl.ParseOpcode)
    {
    /*
     * Only these operators can be optimized because they have
     * a target operand
     */
    case PARSEOP_ADD:
    case PARSEOP_AND:
    case PARSEOP_DIVIDE:
    case PARSEOP_INDEX:
    case PARSEOP_MOD:
    case PARSEOP_MULTIPLY:
    case PARSEOP_NOT:
    case PARSEOP_OR:
    case PARSEOP_SHIFTLEFT:
    case PARSEOP_SHIFTRIGHT:
    case PARSEOP_SUBTRACT:
    case PARSEOP_XOR:

        break;

    /* Otherwise, just create a normal Store operator */

    default:
        goto CannotOptimize;
    }

    /*
     * Transform the parse tree such that the target is moved to the
     * last operand of the operator
     */
    SourceOp1 = Source->Asl.Child;
    SourceOp2 = SourceOp1->Asl.Next;

    /* NOT only has one operand, but has a target */

    if (Source->Asl.ParseOpcode == PARSEOP_NOT)
    {
        SourceOp2 = SourceOp1;
    }

    /* DIVIDE has an extra target operand (remainder) */

    if (Source->Asl.ParseOpcode == PARSEOP_DIVIDE)
    {
        SourceOp2 = SourceOp2->Asl.Next;
    }

    TargetOp = SourceOp2->Asl.Next;

    /*
     * Can't perform this optimization if there already is a target
     * for the operator (ZERO is a "no target" placeholder).
     */
    if (TargetOp->Asl.ParseOpcode != PARSEOP_ZERO)
    {
        goto CannotOptimize;
    }

    /* Link in the target as the final operand */

    SourceOp2->Asl.Next = Target;
    Target->Asl.Parent = Source;
    return (Source);


CannotOptimize:

    Operator = TrAllocateOp (PARSEOP_STORE);
    TrLinkOpChildren (Operator, 2, Source, Target);

    /* Set the appropriate line numbers for the new op */

    Operator->Asl.LineNumber        = Target->Asl.LineNumber;
    Operator->Asl.LogicalLineNumber = Target->Asl.LogicalLineNumber;
    Operator->Asl.LogicalByteOffset = Target->Asl.LogicalByteOffset;
    Operator->Asl.Column            = Target->Asl.Column;

    return (Operator);
}


/*******************************************************************************
 *
 * FUNCTION:    TrCreateNullTargetOp
 *
 * PARAMETERS:  None
 *
 * RETURN:      Pointer to the new op. Aborts on allocation failure
 *
 * DESCRIPTION: Create a "null" target op. This is defined by the ACPI
 *              specification to be a zero AML opcode, and indicates that
 *              no target has been specified for the parent operation
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrCreateNullTargetOp (
    void)
{
    ACPI_PARSE_OBJECT       *Op;


    Op = TrAllocateOp (PARSEOP_ZERO);
    Op->Asl.CompileFlags |= (OP_IS_TARGET | OP_COMPILE_TIME_CONST);

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nCreateNullTargetOp  Ln/Col %u/%u NewOp %p  Op %s\n",
        Op->Asl.LineNumber, Op->Asl.Column, Op,
        UtGetOpName (Op->Asl.ParseOpcode));

    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrCreateConstantLeafOp
 *
 * PARAMETERS:  ParseOpcode         - The constant opcode
 *
 * RETURN:      Pointer to the new op. Aborts on allocation failure
 *
 * DESCRIPTION: Create a leaf op (no children or peers) for one of the
 *              special constants - __LINE__, __FILE__, and __DATE__.
 *
 * Note: The fullimplemenation of __METHOD__ cannot happen here because we
 * don't have a full parse tree at this time and cannot find the parent
 * control method. __METHOD__ must be implemented later, after the parse
 * tree has been fully constructed.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrCreateConstantLeafOp (
    UINT32                  ParseOpcode)
{
    ACPI_PARSE_OBJECT       *Op = NULL;
    time_t                  CurrentTime;
    char                    *StaticTimeString;
    char                    *TimeString;
    char                    *Filename;


    switch (ParseOpcode)
    {
    case PARSEOP___LINE__:

        Op = TrAllocateOp (PARSEOP_INTEGER);
        Op->Asl.Value.Integer = Op->Asl.LineNumber;
        break;

    case PARSEOP___METHOD__:

        /* Will become a string literal later */

        Op = TrAllocateOp (PARSEOP___METHOD__);
        Op->Asl.Value.String = NULL;
        break;

    case PARSEOP___PATH__:

        Op = TrAllocateOp (PARSEOP_STRING_LITERAL);

        /* Op.Asl.Filename contains the full pathname to the file */

        Op->Asl.Value.String = Op->Asl.Filename;
        break;

    case PARSEOP___FILE__:

        Op = TrAllocateOp (PARSEOP_STRING_LITERAL);

        /* Get the simple filename from the full path */

        FlSplitInputPathname (Op->Asl.Filename, NULL, &Filename);
        Op->Asl.Value.String = Filename;
        break;

    case PARSEOP___DATE__:

        Op = TrAllocateOp (PARSEOP_STRING_LITERAL);

        /* Get a copy of the current time */

        CurrentTime = time (NULL);
        StaticTimeString = ctime (&CurrentTime);
        TimeString = UtLocalCalloc (strlen (StaticTimeString) + 1);
        strcpy (TimeString, StaticTimeString);

        TimeString[strlen(TimeString) -1] = 0;  /* Remove trailing newline */
        Op->Asl.Value.String = TimeString;
        break;

    default: /* This would be an internal error */

        return (NULL);
    }

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nCreateConstantLeafOp  Ln/Col %u/%u NewOp %p  "
        "Op %s  Value %8.8X%8.8X  \n",
        Op->Asl.LineNumber, Op->Asl.Column, Op, UtGetOpName (ParseOpcode),
        ACPI_FORMAT_UINT64 (Op->Asl.Value.Integer));

    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrAllocateOp
 *
 * PARAMETERS:  ParseOpcode         - Opcode to be assigned to the op
 *
 * RETURN:      New parse op. Aborts on allocation failure
 *
 * DESCRIPTION: Allocate and initialize a new parse op for the parse tree
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
TrAllocateOp (
    UINT32                  ParseOpcode)
{
    ACPI_PARSE_OBJECT       *Op;
    ACPI_PARSE_OBJECT       *LatestOp;


    Op = UtParseOpCacheCalloc ();

    Op->Asl.ParseOpcode       = (UINT16) ParseOpcode;
    Op->Asl.Filename          = AslGbl_Files[ASL_FILE_INPUT].Filename;
    Op->Asl.LineNumber        = AslGbl_CurrentLineNumber;
    Op->Asl.LogicalLineNumber = AslGbl_LogicalLineNumber;
    Op->Asl.LogicalByteOffset = AslGbl_CurrentLineOffset;
    Op->Asl.Column            = AslGbl_CurrentColumn;

    UtSetParseOpName (Op);

    /* The following is for capturing comments */

    if (AcpiGbl_CaptureComments)
    {
        LatestOp = AslGbl_CommentState.LatestParseOp;
        Op->Asl.InlineComment     = NULL;
        Op->Asl.EndNodeComment    = NULL;
        Op->Asl.CommentList       = NULL;
        Op->Asl.FileChanged       = FALSE;

        /*
         * Check to see if the file name has changed before resetting the
         * latest parse op.
         */
        if (LatestOp &&
            (ParseOpcode != PARSEOP_INCLUDE) &&
            (ParseOpcode != PARSEOP_INCLUDE_END) &&
            strcmp (LatestOp->Asl.Filename, Op->Asl.Filename))
        {
            CvDbgPrint ("latest op: %s\n", LatestOp->Asl.ParseOpName);
            Op->Asl.FileChanged = TRUE;
            if (AslGbl_IncludeFileStack)
            {
                Op->Asl.ParentFilename = AslGbl_IncludeFileStack->Filename;
            }
            else
            {
                Op->Asl.ParentFilename = NULL;
            }
        }

        AslGbl_CommentState.LatestParseOp = Op;
        CvDbgPrint ("TrAllocateOp=Set latest parse op to this op.\n");
        CvDbgPrint ("           Op->Asl.ParseOpName = %s\n",
            AslGbl_CommentState.LatestParseOp->Asl.ParseOpName);
        CvDbgPrint ("           Op->Asl.ParseOpcode = 0x%x\n", ParseOpcode);

        if (Op->Asl.FileChanged)
        {
            CvDbgPrint("    file has been changed!\n");
        }

        /*
         * if this parse op's syntax uses () and {} (i.e. Package(1){0x00}) then
         * set a flag in the comment state. This facilitates paring comments for
         * these types of opcodes.
         */
        if ((CvParseOpBlockType(Op) == (BLOCK_PAREN | BLOCK_BRACE)) &&
            (ParseOpcode != PARSEOP_DEFINITION_BLOCK))
        {
            CvDbgPrint ("Parsing paren/Brace op now!\n");
            AslGbl_CommentState.ParsingParenBraceNode = Op;
        }

        if (AslGbl_CommentListHead)
        {
            CvDbgPrint ("Transferring...\n");
            Op->Asl.CommentList = AslGbl_CommentListHead;
            AslGbl_CommentListHead = NULL;
            AslGbl_CommentListTail = NULL;
            CvDbgPrint ("    Transferred current comment list to this op.\n");
            CvDbgPrint ("    %s\n", Op->Asl.CommentList->Comment);
        }

        if (AslGbl_InlineCommentBuffer)
        {
            Op->Asl.InlineComment = AslGbl_InlineCommentBuffer;
            AslGbl_InlineCommentBuffer = NULL;
            CvDbgPrint ("Transferred current inline comment list to this op.\n");
        }
    }

    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrPrintOpFlags
 *
 * PARAMETERS:  Flags               - Flags word to be decoded
 *              OutputLevel         - Debug output level: ASL_TREE_OUTPUT etc.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a flags word to text. Displays all flags that are set.
 *
 ******************************************************************************/

void
TrPrintOpFlags (
    UINT32                  Flags,
    UINT32                  OutputLevel)
{
    UINT32                  FlagBit = 1;
    UINT32                  i;


    for (i = 0; i < ACPI_NUM_OP_FLAGS; i++)
    {
        if (Flags & FlagBit)
        {
            DbgPrint (OutputLevel, " %s", AslGbl_OpFlagNames[i]);
        }

        FlagBit <<= 1;
    }
}
