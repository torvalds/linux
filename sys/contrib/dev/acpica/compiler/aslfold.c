/******************************************************************************
 *
 * Module Name: aslfold - Constant folding
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

#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acparser.h>

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslfold")

/* Local prototypes */

static ACPI_STATUS
OpcAmlEvaluationWalk1 (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);

static ACPI_STATUS
OpcAmlEvaluationWalk2 (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);

static ACPI_STATUS
OpcAmlCheckForConstant (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);

static void
OpcUpdateIntegerNode (
    ACPI_PARSE_OBJECT       *Op,
    UINT64                  Value);

static ACPI_STATUS
TrTransformToStoreOp (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState);

static ACPI_STATUS
TrSimpleConstantReduction (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState);

static void
TrInstallReducedConstant (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_OPERAND_OBJECT     *ObjDesc);


/*******************************************************************************
 *
 * FUNCTION:    OpcAmlConstantWalk
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Reduce an Op and its subtree to a constant if possible.
 *              Called during ascent of the parse tree.
 *
 ******************************************************************************/

ACPI_STATUS
OpcAmlConstantWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ACPI_WALK_STATE         *WalkState;
    ACPI_STATUS             Status = AE_OK;


    if (Op->Asl.CompileFlags == 0)
    {
        return (AE_OK);
    }

    /*
     * Only interested in subtrees that could possibly contain
     * expressions that can be evaluated at this time
     */
    if ((!(Op->Asl.CompileFlags & OP_COMPILE_TIME_CONST)) ||
          (Op->Asl.CompileFlags & OP_IS_TARGET))
    {
        return (AE_OK);
    }

    /* Create a new walk state */

    WalkState = AcpiDsCreateWalkState (0, NULL, NULL, NULL);
    if (!WalkState)
    {
        return (AE_NO_MEMORY);
    }

    WalkState->NextOp = NULL;
    WalkState->Params = NULL;

    /*
     * Examine the entire subtree -- all nodes must be constants
     * or type 3/4/5 opcodes
     */
    Status = TrWalkParseTree (Op, ASL_WALK_VISIT_DOWNWARD,
        OpcAmlCheckForConstant, NULL, WalkState);

    /*
     * Did we find an entire subtree that contains all constants
     * and type 3/4/5 opcodes?
     */
    switch (Status)
    {
    case AE_OK:

        /* Simple case, like Add(3,4) -> 7 */

        Status = TrSimpleConstantReduction (Op, WalkState);
        break;

    case AE_CTRL_RETURN_VALUE:

        /* More complex case, like Add(3,4,Local0) -> Store(7,Local0) */

        Status = TrTransformToStoreOp (Op, WalkState);
        break;

    case AE_TYPE:

        AcpiDsDeleteWalkState (WalkState);
        return (AE_OK);

    default:
        AcpiDsDeleteWalkState (WalkState);
        break;
    }

    if (ACPI_FAILURE (Status))
    {
        DbgPrint (ASL_PARSE_OUTPUT, "Cannot resolve, %s\n",
            AcpiFormatException (Status));

        /* We could not resolve the subtree for some reason */

        AslError (ASL_ERROR, ASL_MSG_CONSTANT_EVALUATION, Op,
            (char *) AcpiFormatException (Status));

        /* Set the subtree value to ZERO anyway. Eliminates further errors */

        OpcUpdateIntegerNode (Op, 0);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    OpcAmlCheckForConstant
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check one Op for a reducible type 3/4/5 AML opcode.
 *              This is performed via an upward walk of the parse subtree.
 *
 ******************************************************************************/

static ACPI_STATUS
OpcAmlCheckForConstant (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ACPI_WALK_STATE         *WalkState = Context;
    ACPI_STATUS             Status = AE_OK;
    ACPI_PARSE_OBJECT       *NextOp;
    const ACPI_OPCODE_INFO  *OpInfo;


    WalkState->Op = Op;
    WalkState->Opcode = Op->Common.AmlOpcode;
    WalkState->OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);

    DbgPrint (ASL_PARSE_OUTPUT, "[%.4d] Opcode: %12.12s ",
        Op->Asl.LogicalLineNumber, Op->Asl.ParseOpName);

    /*
     * These opcodes do not appear in the OpcodeInfo table, but
     * they represent constants, so abort the constant walk now.
     */
    if ((WalkState->Opcode == AML_RAW_DATA_BYTE) ||
        (WalkState->Opcode == AML_RAW_DATA_WORD) ||
        (WalkState->Opcode == AML_RAW_DATA_DWORD) ||
        (WalkState->Opcode == AML_RAW_DATA_QWORD))
    {
        DbgPrint (ASL_PARSE_OUTPUT, "RAW DATA");
        Status = AE_TYPE;
        goto CleanupAndExit;
    }

    /*
     * Search upwards for a possible Name() operator. This is done
     * because a type 3/4/5 opcode within a Name() expression
     * MUST be reduced to a simple constant.
     */
    NextOp = Op->Asl.Parent;
    while (NextOp)
    {
        /* Finished if we find a Name() opcode */

        if (NextOp->Asl.AmlOpcode == AML_NAME_OP)
        {
            break;
        }

        /*
         * Any "deferred" opcodes contain one or more TermArg parameters,
         * and thus are not required to be folded to constants at compile
         * time. This affects things like Buffer() and Package() objects.
         * We just ignore them here. However, any sub-expressions can and
         * will still be typechecked. Note: These are called the
         * "deferred" opcodes in the AML interpreter.
         */
        OpInfo = AcpiPsGetOpcodeInfo (NextOp->Common.AmlOpcode);
        if (OpInfo->Flags & AML_DEFER)
        {
            NextOp = NULL;
            break;
        }

        NextOp = NextOp->Asl.Parent;
    }

    /* Type 3/4/5 opcodes have the AML_CONSTANT flag set */

    if (!(WalkState->OpInfo->Flags & AML_CONSTANT))
    {
        /*
         * From the ACPI specification:
         *
         * "The Type 3/4/5 opcodes return a value and can be used in an
         * expression that evaluates to a constant. These opcodes may be
         * evaluated at ASL compile-time. To ensure that these opcodes
         * will evaluate to a constant, the following rules apply: The
         * term cannot have a destination (target) operand, and must have
         * either a Type3Opcode, Type4Opcode, Type5Opcode, ConstExprTerm,
         * Integer, BufferTerm, Package, or String for all arguments."
         */

        /*
         * The value (second) operand for the Name() operator MUST
         * reduce to a single constant, as per the ACPI specification
         * (the operand is a DataObject). This also implies that there
         * can be no target operand. Name() is the only ASL operator
         * with a "DataObject" as an operand and is thus special-
         * cased here.
         */
        if (NextOp) /* Inspect a Name() operator */
        {
            /* Error if there is a target operand */

            if (Op->Asl.CompileFlags & OP_IS_TARGET)
            {
                AslError (ASL_ERROR, ASL_MSG_INVALID_TARGET, Op, NULL);
                Status = AE_TYPE;
            }

            /* Error if expression cannot be reduced (folded) */

            if (!(NextOp->Asl.CompileFlags & OP_COULD_NOT_REDUCE))
            {
                /* Ensure only one error message per statement */

                NextOp->Asl.CompileFlags |= OP_COULD_NOT_REDUCE;
                DbgPrint (ASL_PARSE_OUTPUT,
                    "**** Could not reduce operands for NAME opcode ****\n");

                AslError (ASL_ERROR, ASL_MSG_CONSTANT_REQUIRED, Op,
                    "Constant is required for Name operator");
                Status = AE_TYPE;
            }
        }

        if (ACPI_FAILURE (Status))
        {
            goto CleanupAndExit;
        }

        /* This is not a 3/4/5 opcode, but maybe can convert to STORE */

        if (Op->Asl.CompileFlags & OP_IS_TARGET)
        {
            DbgPrint (ASL_PARSE_OUTPUT,
                "**** Valid Target, transform to Store or CopyObject ****\n");
            return (AE_CTRL_RETURN_VALUE);
        }

        /* Expression cannot be reduced */

        DbgPrint (ASL_PARSE_OUTPUT,
            "**** Not a Type 3/4/5 opcode or cannot reduce/fold (%s) ****\n",
             Op->Asl.ParseOpName);

        Status = AE_TYPE;
        goto CleanupAndExit;
    }

    /*
     * TBD: Ignore buffer constants for now. The problem is that these
     * constants have been transformed into RAW_DATA at this point, from
     * the parse tree transform process which currently happens before
     * the constant folding process. We may need to defer this transform
     * for buffer until after the constant folding.
     */
    if (WalkState->Opcode == AML_BUFFER_OP)
    {
        DbgPrint (ASL_PARSE_OUTPUT,
            "\nBuffer constant reduction is currently not supported\n");

        if (NextOp) /* Found a Name() operator, error */
        {
            AslError (ASL_ERROR, ASL_MSG_UNSUPPORTED, Op,
                "Buffer expression cannot be reduced");
        }

        Status = AE_TYPE;
        goto CleanupAndExit;
    }

    /* Debug output */

    DbgPrint (ASL_PARSE_OUTPUT, "TYPE_345");

    if (Op->Asl.CompileFlags & OP_IS_TARGET)
    {
        if (Op->Asl.ParseOpcode == PARSEOP_ZERO)
        {
            DbgPrint (ASL_PARSE_OUTPUT, "%-16s", " NULL TARGET");
        }
        else
        {
            DbgPrint (ASL_PARSE_OUTPUT, "%-16s", " VALID TARGET");
        }
    }

    if (Op->Asl.CompileFlags & OP_IS_TERM_ARG)
    {
        DbgPrint (ASL_PARSE_OUTPUT, "%-16s", " TERMARG");
    }

CleanupAndExit:

    /* Dump the node compile flags also */

    TrPrintOpFlags (Op->Asl.CompileFlags, ASL_PARSE_OUTPUT);
    DbgPrint (ASL_PARSE_OUTPUT, "\n");
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    TrSimpleConstantReduction
 *
 * PARAMETERS:  Op                  - Parent operator to be transformed
 *              WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Reduce an entire AML operation to a single constant. The
 *              operation must not have a target operand.
 *
 *              Add (32,64) --> 96
 *
 ******************************************************************************/

static ACPI_STATUS
TrSimpleConstantReduction (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_PARSE_OBJECT       *RootOp;
    ACPI_PARSE_OBJECT       *OriginalParentOp;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_STATUS             Status;


    DbgPrint (ASL_PARSE_OUTPUT,
        "Simple subtree constant reduction, operator to constant\n");

    /* Allocate a new temporary root for this subtree */

    RootOp = TrAllocateOp (PARSEOP_INTEGER);
    if (!RootOp)
    {
        return (AE_NO_MEMORY);
    }

    RootOp->Common.AmlOpcode = AML_INT_EVAL_SUBTREE_OP;

    OriginalParentOp = Op->Common.Parent;
    Op->Common.Parent = RootOp;

    /* Hand off the subtree to the AML interpreter */

    WalkState->CallerReturnDesc = &ObjDesc;

    Status = TrWalkParseTree (Op, ASL_WALK_VISIT_TWICE,
        OpcAmlEvaluationWalk1, OpcAmlEvaluationWalk2, WalkState);

    /* Restore original parse tree */

    Op->Common.Parent = OriginalParentOp;

    if (ACPI_FAILURE (Status))
    {
        DbgPrint (ASL_PARSE_OUTPUT,
            "Constant Subtree evaluation(1), %s\n",
            AcpiFormatException (Status));
        return (Status);
    }

    /* Get the final result */

    Status = AcpiDsResultPop (&ObjDesc, WalkState);
    if (ACPI_FAILURE (Status))
    {
        DbgPrint (ASL_PARSE_OUTPUT,
            "Constant Subtree evaluation(2), %s\n",
            AcpiFormatException (Status));
        return (Status);
    }

    /* Disconnect any existing children, install new constant */

    Op->Asl.Child = NULL;
    TrInstallReducedConstant (Op, ObjDesc);

    UtSetParseOpName (Op);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    TrTransformToStoreOp
 *
 * PARAMETERS:  Op                  - Parent operator to be transformed
 *              WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transforms a single AML operation with a constant and target
 *              to a simple store operation:
 *
 *              Add (32,64,DATA) --> Store (96,DATA)
 *
 ******************************************************************************/

static ACPI_STATUS
TrTransformToStoreOp (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_PARSE_OBJECT       *OriginalTarget;
    ACPI_PARSE_OBJECT       *NewTarget;
    ACPI_PARSE_OBJECT       *Child1;
    ACPI_PARSE_OBJECT       *Child2;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_PARSE_OBJECT       *NewParent;
    ACPI_PARSE_OBJECT       *OriginalParent;
    ACPI_STATUS             Status;
    UINT16                  NewParseOpcode;
    UINT16                  NewAmlOpcode;


    /* Extract the operands */

    Child1 = Op->Asl.Child;
    Child2 = Child1->Asl.Next;

    /*
     * Special case for DIVIDE -- it has two targets. The first
     * is for the remainder and if present, we will not attempt
     * to reduce the expression.
     */
    if (Op->Asl.ParseOpcode == PARSEOP_DIVIDE)
    {
        Child2 = Child2->Asl.Next;
        if (Child2->Asl.ParseOpcode != PARSEOP_ZERO)
        {
            DbgPrint (ASL_PARSE_OUTPUT,
                "Cannot reduce DIVIDE - has two targets\n\n");
            return (AE_OK);
        }
    }

    switch (Op->Asl.ParseOpcode)
    {
    /*
     * Folding of the explicit conversion opcodes must use CopyObject
     * instead of Store. This can change the object type of the target
     * operand, as per the ACPI specification:
     *
     * "If the ASL operator is one of the explicit conversion operators
     * (ToString, ToInteger, etc., and the CopyObject operator), no
     * [implicit] conversion is performed. (In other words, the result
     * object is stored directly to the target and completely overwrites
     * any existing object already stored at the target)"
     */
    case PARSEOP_TOINTEGER:
    case PARSEOP_TOSTRING:
    case PARSEOP_TOBUFFER:
    case PARSEOP_TODECIMALSTRING:
    case PARSEOP_TOHEXSTRING:
    case PARSEOP_TOBCD:
    case PARSEOP_FROMBCD:

        NewParseOpcode = PARSEOP_COPYOBJECT;
        NewAmlOpcode = AML_COPY_OBJECT_OP;

        DbgPrint (ASL_PARSE_OUTPUT,
            "Reduction/Transform to CopyObjectOp: CopyObject(%s, %s)\n",
            Child1->Asl.ParseOpName, Child2->Asl.ParseOpName);
        break;

    default:

        NewParseOpcode = PARSEOP_STORE;
        NewAmlOpcode = AML_STORE_OP;

        DbgPrint (ASL_PARSE_OUTPUT,
            "Reduction/Transform to StoreOp: Store(%s, %s)\n",
            Child1->Asl.ParseOpName, Child2->Asl.ParseOpName);
        break;
    }

    /*
     * Create a NULL (zero) target so that we can use the
     * interpreter to evaluate the expression.
     */
    NewTarget = TrCreateNullTargetOp ();
    NewTarget->Common.AmlOpcode = AML_INT_NAMEPATH_OP;

    /* Handle one-operand cases (NOT, TOBCD, etc.) */

    if (!Child2->Asl.Next)
    {
        Child2 = Child1;
    }

    /* Link in new NULL target as the last operand */

    OriginalTarget = Child2->Asl.Next;
    Child2->Asl.Next = NewTarget;
    NewTarget->Asl.Parent = OriginalTarget->Asl.Parent;

    NewParent = TrAllocateOp (PARSEOP_INTEGER);
    NewParent->Common.AmlOpcode = AML_INT_EVAL_SUBTREE_OP;

    OriginalParent = Op->Common.Parent;
    Op->Common.Parent = NewParent;

    /* Hand off the subtree to the AML interpreter */

    WalkState->CallerReturnDesc = &ObjDesc;

    Status = TrWalkParseTree (Op, ASL_WALK_VISIT_TWICE,
        OpcAmlEvaluationWalk1, OpcAmlEvaluationWalk2, WalkState);
    if (ACPI_FAILURE (Status))
    {
        DbgPrint (ASL_PARSE_OUTPUT,
            "Constant Subtree evaluation(3), %s\n",
            AcpiFormatException (Status));
        goto EvalError;
    }

    /* Get the final result */

    Status = AcpiDsResultPop (&ObjDesc, WalkState);
    if (ACPI_FAILURE (Status))
    {
        DbgPrint (ASL_PARSE_OUTPUT,
            "Constant Subtree evaluation(4), %s\n",
            AcpiFormatException (Status));
        goto EvalError;
    }

    /* Truncate any subtree expressions, they have been evaluated */

    Child1->Asl.Child = NULL;

    /* Folded constant is in ObjDesc, store into Child1 */

    TrInstallReducedConstant (Child1, ObjDesc);

    /* Convert operator to STORE or COPYOBJECT */

    Op->Asl.ParseOpcode = NewParseOpcode;
    Op->Asl.AmlOpcode = NewAmlOpcode;
    UtSetParseOpName (Op);
    Op->Common.Parent = OriginalParent;

    /* First child is the folded constant */

    /* Second child will be the target */

    Child1->Asl.Next = OriginalTarget;
    return (AE_OK);


EvalError:

    /* Restore original links */

    Op->Common.Parent = OriginalParent;
    Child2->Asl.Next = OriginalTarget;
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    TrInstallReducedConstant
 *
 * PARAMETERS:  Op                  - Parent operator to be transformed
 *              ObjDesc             - Reduced constant to be installed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Transform the original operator to a simple constant.
 *              Handles Integers, Strings, and Buffers.
 *
 ******************************************************************************/

static void
TrInstallReducedConstant (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_OPERAND_OBJECT     *ObjDesc)
{
    ACPI_PARSE_OBJECT       *LengthOp;
    ACPI_PARSE_OBJECT       *DataOp;


    AslGbl_TotalFolds++;
    AslError (ASL_OPTIMIZATION, ASL_MSG_CONSTANT_FOLDED, Op,
        Op->Asl.ParseOpName);

    /*
     * Because we know we executed type 3/4/5 opcodes above, we know that
     * the result must be either an Integer, String, or Buffer.
     */
    switch (ObjDesc->Common.Type)
    {
    case ACPI_TYPE_INTEGER:

        OpcUpdateIntegerNode (Op, ObjDesc->Integer.Value);

        DbgPrint (ASL_PARSE_OUTPUT,
            "Constant expression reduced to (%s) %8.8X%8.8X\n\n",
            Op->Asl.ParseOpName,
            ACPI_FORMAT_UINT64 (Op->Common.Value.Integer));
        break;

    case ACPI_TYPE_STRING:

        Op->Asl.ParseOpcode = PARSEOP_STRING_LITERAL;
        Op->Common.AmlOpcode = AML_STRING_OP;
        Op->Asl.AmlLength = strlen (ObjDesc->String.Pointer) + 1;
        Op->Common.Value.String = ObjDesc->String.Pointer;

        DbgPrint (ASL_PARSE_OUTPUT,
            "Constant expression reduced to (STRING) %s\n\n",
            Op->Common.Value.String);
        break;

    case ACPI_TYPE_BUFFER:
        /*
         * Create a new parse subtree of the form:
         *
         * BUFFER (Buffer AML opcode)
         *    INTEGER (Buffer length in bytes)
         *    RAW_DATA (Buffer byte data)
         */
        Op->Asl.ParseOpcode = PARSEOP_BUFFER;
        Op->Common.AmlOpcode = AML_BUFFER_OP;
        Op->Asl.CompileFlags = OP_AML_PACKAGE;
        UtSetParseOpName (Op);

        /* Child node is the buffer length */

        LengthOp = TrAllocateOp (PARSEOP_INTEGER);

        LengthOp->Asl.AmlOpcode = AML_DWORD_OP;
        LengthOp->Asl.Value.Integer = ObjDesc->Buffer.Length;
        LengthOp->Asl.Parent = Op;
        (void) OpcSetOptimalIntegerSize (LengthOp);

        Op->Asl.Child = LengthOp;

        /* Next child is the raw buffer data */

        DataOp = TrAllocateOp (PARSEOP_RAW_DATA);
        DataOp->Asl.AmlOpcode = AML_RAW_DATA_BUFFER;
        DataOp->Asl.AmlLength = ObjDesc->Buffer.Length;
        DataOp->Asl.Value.String = (char *) ObjDesc->Buffer.Pointer;
        DataOp->Asl.Parent = Op;

        LengthOp->Asl.Next = DataOp;

        DbgPrint (ASL_PARSE_OUTPUT,
            "Constant expression reduced to (BUFFER) length %X\n\n",
            ObjDesc->Buffer.Length);
        break;

    default:
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    OpcUpdateIntegerNode
 *
 * PARAMETERS:  Op                  - Current parse object
 *              Value               - Value for the integer op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Update node to the correct Integer type and value
 *
 ******************************************************************************/

static void
OpcUpdateIntegerNode (
    ACPI_PARSE_OBJECT       *Op,
    UINT64                  Value)
{

    Op->Common.Value.Integer = Value;

    /*
     * The AmlLength is used by the parser to indicate a constant,
     * (if non-zero). Length is either (1/2/4/8)
     */
    switch (Op->Asl.AmlLength)
    {
    case 1:

        TrSetOpIntegerValue (PARSEOP_BYTECONST, Op);
        Op->Asl.AmlOpcode = AML_RAW_DATA_BYTE;
        break;

    case 2:

        TrSetOpIntegerValue (PARSEOP_WORDCONST, Op);
        Op->Asl.AmlOpcode = AML_RAW_DATA_WORD;
        break;

    case 4:

        TrSetOpIntegerValue (PARSEOP_DWORDCONST, Op);
        Op->Asl.AmlOpcode = AML_RAW_DATA_DWORD;
        break;

    case 8:

        TrSetOpIntegerValue (PARSEOP_QWORDCONST, Op);
        Op->Asl.AmlOpcode = AML_RAW_DATA_QWORD;
        break;

    case 0:
    default:

        OpcSetOptimalIntegerSize (Op);
        TrSetOpIntegerValue (PARSEOP_INTEGER, Op);
        break;
    }

    Op->Asl.AmlLength = 0;
}


/*******************************************************************************
 *
 * FUNCTION:    OpcAmlEvaluationWalk1
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback for AML execution of constant subtrees
 *
 ******************************************************************************/

static ACPI_STATUS
OpcAmlEvaluationWalk1 (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ACPI_WALK_STATE         *WalkState = Context;
    ACPI_STATUS             Status;
    ACPI_PARSE_OBJECT       *OutOp;


    WalkState->Op = Op;
    WalkState->Opcode = Op->Common.AmlOpcode;
    WalkState->OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);

    /* Copy child pointer to Arg for compatibility with Interpreter */

    if (Op->Asl.Child)
    {
        Op->Common.Value.Arg = Op->Asl.Child;
    }

    /* Call AML dispatcher */

    Status = AcpiDsExecBeginOp (WalkState, &OutOp);
    if (ACPI_FAILURE (Status))
    {
        DbgPrint (ASL_PARSE_OUTPUT,
            "%s Constant interpretation failed (1) - %s\n",
            Op->Asl.ParseOpName, AcpiFormatException (Status));
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    OpcAmlEvaluationWalk2
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback for AML execution of constant subtrees
 *
 ******************************************************************************/

static ACPI_STATUS
OpcAmlEvaluationWalk2 (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ACPI_WALK_STATE         *WalkState = Context;
    ACPI_STATUS             Status;


    WalkState->Op = Op;
    WalkState->Opcode = Op->Common.AmlOpcode;
    WalkState->OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);

    /* Copy child pointer to Arg for compatibility with Interpreter */

    if (Op->Asl.Child)
    {
        Op->Common.Value.Arg = Op->Asl.Child;
    }

    /* Call AML dispatcher */

    Status = AcpiDsExecEndOp (WalkState);
    if (ACPI_FAILURE (Status))
    {
        DbgPrint (ASL_PARSE_OUTPUT,
            "%s: Constant interpretation failed (2) - %s\n",
            Op->Asl.ParseOpName, AcpiFormatException (Status));
    }

    return (Status);
}
