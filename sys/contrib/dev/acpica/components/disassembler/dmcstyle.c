/*******************************************************************************
 *
 * Module Name: dmcstyle - Support for C-style operator disassembly
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acdebug.h>
#include <contrib/dev/acpica/include/acconvert.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dmcstyle")


/* Local prototypes */

static char *
AcpiDmGetCompoundSymbol (
   UINT16                   AslOpcode);

static void
AcpiDmPromoteTarget (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *Target);

static BOOLEAN
AcpiDmIsValidTarget (
    ACPI_PARSE_OBJECT       *Op);

static BOOLEAN
AcpiDmIsTargetAnOperand (
    ACPI_PARSE_OBJECT       *Target,
    ACPI_PARSE_OBJECT       *Operand,
    BOOLEAN                 TopLevel);

static BOOLEAN
AcpiDmIsOptimizationIgnored (
    ACPI_PARSE_OBJECT       *StoreOp,
    ACPI_PARSE_OBJECT       *StoreArgument);


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmCheckForSymbolicOpcode
 *
 * PARAMETERS:  Op                  - Current parse object
 *              Walk                - Current parse tree walk info
 *
 * RETURN:      TRUE if opcode can be converted to symbolic, FALSE otherwise
 *
 * DESCRIPTION: This is the main code that implements disassembly of AML code
 *              to C-style operators. Called during descending phase of the
 *              parse tree walk.
 *
 ******************************************************************************/

BOOLEAN
AcpiDmCheckForSymbolicOpcode (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_OP_WALK_INFO       *Info)
{
    char                    *OperatorSymbol = NULL;
    ACPI_PARSE_OBJECT       *Argument1;
    ACPI_PARSE_OBJECT       *Argument2;
    ACPI_PARSE_OBJECT       *Target;
    ACPI_PARSE_OBJECT       *Target2;


    /* Exit immediately if ASL+ not enabled */

    if (!AcpiGbl_CstyleDisassembly)
    {
        return (FALSE);
    }

    /* Get the first operand */

    Argument1 = AcpiPsGetArg (Op, 0);
    if (!Argument1)
    {
        return (FALSE);
    }

    /* Get the second operand */

    Argument2 = Argument1->Common.Next;

    /* Setup the operator string for this opcode */

    switch (Op->Common.AmlOpcode)
    {
    case AML_ADD_OP:
        OperatorSymbol = " + ";
        break;

    case AML_SUBTRACT_OP:
        OperatorSymbol = " - ";
        break;

    case AML_MULTIPLY_OP:
        OperatorSymbol = " * ";
        break;

    case AML_DIVIDE_OP:
        OperatorSymbol = " / ";
        break;

    case AML_MOD_OP:
        OperatorSymbol = " % ";
        break;

    case AML_SHIFT_LEFT_OP:
        OperatorSymbol = " << ";
        break;

    case AML_SHIFT_RIGHT_OP:
        OperatorSymbol = " >> ";
        break;

    case AML_BIT_AND_OP:
        OperatorSymbol = " & ";
        break;

    case AML_BIT_OR_OP:
        OperatorSymbol = " | ";
        break;

    case AML_BIT_XOR_OP:
        OperatorSymbol = " ^ ";
        break;

    /* Logical operators, no target */

    case AML_LOGICAL_AND_OP:
        OperatorSymbol = " && ";
        break;

    case AML_LOGICAL_EQUAL_OP:
        OperatorSymbol = " == ";
        break;

    case AML_LOGICAL_GREATER_OP:
        OperatorSymbol = " > ";
        break;

    case AML_LOGICAL_LESS_OP:
        OperatorSymbol = " < ";
        break;

    case AML_LOGICAL_OR_OP:
        OperatorSymbol = " || ";
        break;

    case AML_LOGICAL_NOT_OP:
        /*
         * Check for the LNOT sub-opcodes. These correspond to
         * LNotEqual, LLessEqual, and LGreaterEqual. There are
         * no actual AML opcodes for these operators.
         */
        switch (Argument1->Common.AmlOpcode)
        {
        case AML_LOGICAL_EQUAL_OP:
            OperatorSymbol = " != ";
            break;

        case AML_LOGICAL_GREATER_OP:
            OperatorSymbol = " <= ";
            break;

        case AML_LOGICAL_LESS_OP:
            OperatorSymbol = " >= ";
            break;

        default:

            /* Unary LNOT case, emit "!" immediately */

            AcpiOsPrintf ("!");
            return (TRUE);
        }

        Argument1->Common.DisasmOpcode = ACPI_DASM_LNOT_SUFFIX;
        Op->Common.DisasmOpcode = ACPI_DASM_LNOT_PREFIX;

        /* Save symbol string in the next child (not peer) */

        Argument2 = AcpiPsGetArg (Argument1, 0);
        if (!Argument2)
        {
            return (FALSE);
        }

        Argument2->Common.OperatorSymbol = OperatorSymbol;
        return (TRUE);

    case AML_INDEX_OP:
        /*
         * Check for constant source operand. Note: although technically
         * legal syntax, the iASL compiler does not support this with
         * the symbolic operators for Index(). It doesn't make sense to
         * use Index() with a constant anyway.
         */
        if ((Argument1->Common.AmlOpcode == AML_STRING_OP)  ||
            (Argument1->Common.AmlOpcode == AML_BUFFER_OP)  ||
            (Argument1->Common.AmlOpcode == AML_PACKAGE_OP) ||
            (Argument1->Common.AmlOpcode == AML_VARIABLE_PACKAGE_OP))
        {
            Op->Common.DisasmFlags |= ACPI_PARSEOP_CLOSING_PAREN;
            return (FALSE);
        }

        /* Index operator is [] */

        Argument1->Common.OperatorSymbol = " [";
        Argument2->Common.OperatorSymbol = "]";
        break;

    /* Unary operators */

    case AML_DECREMENT_OP:
        OperatorSymbol = "--";
        break;

    case AML_INCREMENT_OP:
        OperatorSymbol = "++";
        break;

    case AML_BIT_NOT_OP:
    case AML_STORE_OP:
        OperatorSymbol = NULL;
        break;

    default:
        return (FALSE);
    }

    if (Argument1->Common.DisasmOpcode == ACPI_DASM_LNOT_SUFFIX)
    {
        return (TRUE);
    }

    /*
     * This is the key to how the disassembly of the C-style operators
     * works. We save the operator symbol in the first child, thus
     * deferring symbol output until after the first operand has been
     * emitted.
     */
    if (!Argument1->Common.OperatorSymbol)
    {
        Argument1->Common.OperatorSymbol = OperatorSymbol;
    }

    /*
     * Check for a valid target as the 3rd (or sometimes 2nd) operand
     *
     * Compound assignment operator support:
     * Attempt to optimize constructs of the form:
     *      Add (Local1, 0xFF, Local1)
     * to:
     *      Local1 += 0xFF
     *
     * Only the math operators and Store() have a target.
     * Logicals have no target.
     */
    switch (Op->Common.AmlOpcode)
    {
    case AML_ADD_OP:
    case AML_SUBTRACT_OP:
    case AML_MULTIPLY_OP:
    case AML_DIVIDE_OP:
    case AML_MOD_OP:
    case AML_SHIFT_LEFT_OP:
    case AML_SHIFT_RIGHT_OP:
    case AML_BIT_AND_OP:
    case AML_BIT_OR_OP:
    case AML_BIT_XOR_OP:

        /* Target is 3rd operand */

        Target = Argument2->Common.Next;
        if (Op->Common.AmlOpcode == AML_DIVIDE_OP)
        {
            Target2 = Target->Common.Next;

            /*
             * Divide has an extra target operand (Remainder).
             * Default behavior is to simply ignore ASL+ conversion
             * if the remainder target (modulo) is specified.
             */
            if (!AcpiGbl_DoDisassemblerOptimizations)
            {
                if (AcpiDmIsValidTarget (Target))
                {
                    Argument1->Common.OperatorSymbol = NULL;
                    Op->Common.DisasmFlags |= ACPI_PARSEOP_LEGACY_ASL_ONLY;
                    return (FALSE);
                }

                Target->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                Target = Target2;
            }
            else
            {
                /*
                 * Divide has an extra target operand (Remainder).
                 * If both targets are specified, it cannot be converted
                 * to a C-style operator.
                 */
                if (AcpiDmIsValidTarget (Target) &&
                    AcpiDmIsValidTarget (Target2))
                {
                    Argument1->Common.OperatorSymbol = NULL;
                    Op->Common.DisasmFlags |= ACPI_PARSEOP_LEGACY_ASL_ONLY;
                    return (FALSE);
                }

                if (AcpiDmIsValidTarget (Target)) /* Only first Target is valid (remainder) */
                {
                    /* Convert the Divide to Modulo */

                    Op->Common.AmlOpcode = AML_MOD_OP;

                    Argument1->Common.OperatorSymbol = " % ";
                    Target2->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                }
                else /* Only second Target (quotient) is valid */
                {
                    Target->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                    Target = Target2;
                }
            }
        }

        /* Parser should ensure there is at least a placeholder target */

        if (!Target)
        {
            return (FALSE);
        }

        if (!AcpiDmIsValidTarget (Target))
        {
            /* Not a valid target (placeholder only, from parser) */
            break;
        }

        /*
         * Promote the target up to the first child in the parse
         * tree. This is done because the target will be output
         * first, in the form:
         *     <Target> = Operands...
         */
        AcpiDmPromoteTarget (Op, Target);

        /* Check operands for conversion to a "Compound Assignment" */

        switch (Op->Common.AmlOpcode)
        {
            /* Commutative operators */

        case AML_ADD_OP:
        case AML_MULTIPLY_OP:
        case AML_BIT_AND_OP:
        case AML_BIT_OR_OP:
        case AML_BIT_XOR_OP:
            /*
             * For the commutative operators, we can convert to a
             * compound statement only if at least one (either) operand
             * is the same as the target.
             *
             *      Add (A, B, A) --> A += B
             *      Add (B, A, A) --> A += B
             *      Add (B, C, A) --> A = (B + C)
             */
            if ((AcpiDmIsTargetAnOperand (Target, Argument1, TRUE)) ||
                (AcpiDmIsTargetAnOperand (Target, Argument2, TRUE)))
            {
                Target->Common.OperatorSymbol =
                    AcpiDmGetCompoundSymbol (Op->Common.AmlOpcode);

                /* Convert operator to compound assignment */

                Op->Common.DisasmFlags |= ACPI_PARSEOP_COMPOUND_ASSIGNMENT;
                Argument1->Common.OperatorSymbol = NULL;
                return (TRUE);
            }
            break;

            /* Non-commutative operators */

        case AML_SUBTRACT_OP:
        case AML_DIVIDE_OP:
        case AML_MOD_OP:
        case AML_SHIFT_LEFT_OP:
        case AML_SHIFT_RIGHT_OP:
            /*
             * For the non-commutative operators, we can convert to a
             * compound statement only if the target is the same as the
             * first operand.
             *
             *      Subtract (A, B, A) --> A -= B
             *      Subtract (B, A, A) --> A = (B - A)
             */
            if ((AcpiDmIsTargetAnOperand (Target, Argument1, TRUE)))
            {
                Target->Common.OperatorSymbol =
                    AcpiDmGetCompoundSymbol (Op->Common.AmlOpcode);

                /* Convert operator to compound assignment */

                Op->Common.DisasmFlags |= ACPI_PARSEOP_COMPOUND_ASSIGNMENT;
                Argument1->Common.OperatorSymbol = NULL;
                return (TRUE);
            }
            break;

        default:
            break;
        }

        /*
         * If we are within a C-style expression, emit an extra open
         * paren. Implemented by examining the parent op.
         */
        switch (Op->Common.Parent->Common.AmlOpcode)
        {
        case AML_ADD_OP:
        case AML_SUBTRACT_OP:
        case AML_MULTIPLY_OP:
        case AML_DIVIDE_OP:
        case AML_MOD_OP:
        case AML_SHIFT_LEFT_OP:
        case AML_SHIFT_RIGHT_OP:
        case AML_BIT_AND_OP:
        case AML_BIT_OR_OP:
        case AML_BIT_XOR_OP:
        case AML_LOGICAL_AND_OP:
        case AML_LOGICAL_EQUAL_OP:
        case AML_LOGICAL_GREATER_OP:
        case AML_LOGICAL_LESS_OP:
        case AML_LOGICAL_OR_OP:

            Op->Common.DisasmFlags |= ACPI_PARSEOP_ASSIGNMENT;
            AcpiOsPrintf ("(");
            break;

        default:
            break;
        }

        /* Normal output for ASL/AML operators with a target operand */

        Target->Common.OperatorSymbol = " = (";
        return (TRUE);

    /* Binary operators, no parens */

    case AML_DECREMENT_OP:
    case AML_INCREMENT_OP:
        return (TRUE);

    case AML_INDEX_OP:

        /* Target is optional, 3rd operand */

        Target = Argument2->Common.Next;
        if (AcpiDmIsValidTarget (Target))
        {
            AcpiDmPromoteTarget (Op, Target);

            if (!Target->Common.OperatorSymbol)
            {
                Target->Common.OperatorSymbol = " = ";
            }
        }
        return (TRUE);

    case AML_STORE_OP:
        /*
         * For Store, the Target is the 2nd operand. We know the target
         * is valid, because it is not optional.
         *
         * Ignore any optimizations/folding if flag is set.
         * Used for iASL/disassembler test suite only.
         */
        if (AcpiDmIsOptimizationIgnored (Op, Argument1))
        {
            return (FALSE);
        }

        /*
         * Perform conversion.
         * In the parse tree, simply swap the target with the
         * source so that the target is processed first.
         */
        Target = Argument1->Common.Next;
        if (!Target)
        {
            return (FALSE);
        }

        AcpiDmPromoteTarget (Op, Target);
        if (!Target->Common.OperatorSymbol)
        {
            Target->Common.OperatorSymbol = " = ";
        }
        return (TRUE);

    case AML_BIT_NOT_OP:

        /* Target is optional, 2nd operand */

        Target = Argument1->Common.Next;
        if (!Target)
        {
            return (FALSE);
        }

        if (AcpiDmIsValidTarget (Target))
        {
            /* Valid target, not a placeholder */

            AcpiDmPromoteTarget (Op, Target);
            Target->Common.OperatorSymbol = " = ~";
        }
        else
        {
            /* No target. Emit this prefix operator immediately */

            AcpiOsPrintf ("~");
        }
        return (TRUE);

    default:
        break;
    }

    /* All other operators, emit an open paren */

    AcpiOsPrintf ("(");
    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmIsOptimizationIgnored
 *
 * PARAMETERS:  StoreOp             - Store operator parse object
 *              StoreArgument       - Target associate with the Op
 *
 * RETURN:      TRUE if this Store operator should not be converted/removed.
 *
 * DESCRIPTION: The following function implements "Do not optimize if a
 *              store is immediately followed by a math/bit operator that
 *              has no target".
 *
 *              Function is ignored if DoDisassemblerOptimizations is TRUE.
 *              This is the default, ignore this function.
 *
 *              Disables these types of optimizations, and simply emits
 *              legacy ASL code:
 *                  Store (Add (INT1, 4), INT2) --> Add (INT1, 4, INT2)
 *                                              --> INT2 = INT1 + 4
 *
 *                  Store (Not (INT1), INT2)    --> Not (INT1, INT2)
 *                                              --> INT2 = ~INT1
 *
 *              Used only for the ASL test suite. For the test suite, we
 *              don't want to perform some optimizations to ensure binary
 *              compatibility with the generation of the legacy ASL->AML.
 *              In other words, for all test modules we want exactly:
 *                  (ASL+ -> AML) == (ASL- -> AML)
 *
 ******************************************************************************/

static BOOLEAN
AcpiDmIsOptimizationIgnored (
    ACPI_PARSE_OBJECT       *StoreOp,
    ACPI_PARSE_OBJECT       *StoreArgument)
{
    ACPI_PARSE_OBJECT       *Argument1;
    ACPI_PARSE_OBJECT       *Argument2;
    ACPI_PARSE_OBJECT       *Target;


    /* No optimizations/folding for the typical case */

    if (AcpiGbl_DoDisassemblerOptimizations)
    {
        return (FALSE);
    }

    /*
     * Only a small subset of ASL/AML operators can be optimized.
     * Can only optimize/fold if there is no target (or targets)
     * specified for the operator. And of course, the operator
     * is surrrounded by a Store() operator.
     */
    switch (StoreArgument->Common.AmlOpcode)
    {
    case AML_ADD_OP:
    case AML_SUBTRACT_OP:
    case AML_MULTIPLY_OP:
    case AML_MOD_OP:
    case AML_SHIFT_LEFT_OP:
    case AML_SHIFT_RIGHT_OP:
    case AML_BIT_AND_OP:
    case AML_BIT_OR_OP:
    case AML_BIT_XOR_OP:
    case AML_INDEX_OP:

        /* These operators have two arguments and one target */

        Argument1 = StoreArgument->Common.Value.Arg;
        Argument2 = Argument1->Common.Next;
        Target = Argument2->Common.Next;

        if (!AcpiDmIsValidTarget (Target))
        {
            StoreOp->Common.DisasmFlags |= ACPI_PARSEOP_LEGACY_ASL_ONLY;
            return (TRUE);
        }
        break;

    case AML_DIVIDE_OP:

        /* This operator has two arguments and two targets */

        Argument1 = StoreArgument->Common.Value.Arg;
        Argument2 = Argument1->Common.Next;
        Target = Argument2->Common.Next;

        if (!AcpiDmIsValidTarget (Target) ||
            !AcpiDmIsValidTarget (Target->Common.Next))
        {
            StoreOp->Common.DisasmFlags |= ACPI_PARSEOP_LEGACY_ASL_ONLY;
            return (TRUE);
        }
        break;

    case AML_BIT_NOT_OP:

        /* This operator has one operand and one target */

        Argument1 = StoreArgument->Common.Value.Arg;
        Target = Argument1->Common.Next;

        if (!AcpiDmIsValidTarget (Target))
        {
            StoreOp->Common.DisasmFlags |= ACPI_PARSEOP_LEGACY_ASL_ONLY;
            return (TRUE);
        }
        break;

    default:
        break;
    }

    return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmCloseOperator
 *
 * PARAMETERS:  Op                  - Current parse object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Closes an operator by adding a closing parentheses if and
 *              when necessary. Called during ascending phase of the
 *              parse tree walk.
 *
 ******************************************************************************/

void
AcpiDmCloseOperator (
    ACPI_PARSE_OBJECT       *Op)
{

    /* Always emit paren if ASL+ disassembly disabled */

    if (!AcpiGbl_CstyleDisassembly)
    {
        AcpiOsPrintf (")");
        ASL_CV_PRINT_ONE_COMMENT (Op, AML_COMMENT_END_NODE, NULL, 0);
        return;
    }

    if (Op->Common.DisasmFlags & ACPI_PARSEOP_LEGACY_ASL_ONLY)
    {
        AcpiOsPrintf (")");
        ASL_CV_PRINT_ONE_COMMENT (Op, AML_COMMENT_END_NODE, NULL, 0);
        return;
    }

    /* Check if we need to add an additional closing paren */

    switch (Op->Common.AmlOpcode)
    {
    case AML_ADD_OP:
    case AML_SUBTRACT_OP:
    case AML_MULTIPLY_OP:
    case AML_DIVIDE_OP:
    case AML_MOD_OP:
    case AML_SHIFT_LEFT_OP:
    case AML_SHIFT_RIGHT_OP:
    case AML_BIT_AND_OP:
    case AML_BIT_OR_OP:
    case AML_BIT_XOR_OP:
    case AML_LOGICAL_AND_OP:
    case AML_LOGICAL_EQUAL_OP:
    case AML_LOGICAL_GREATER_OP:
    case AML_LOGICAL_LESS_OP:
    case AML_LOGICAL_OR_OP:

        /* Emit paren only if this is not a compound assignment */

        if (Op->Common.DisasmFlags & ACPI_PARSEOP_COMPOUND_ASSIGNMENT)
        {
            ASL_CV_PRINT_ONE_COMMENT (Op, AML_COMMENT_END_NODE, NULL, 0);
            return;
        }

        /* Emit extra close paren for assignment within an expression */

        if (Op->Common.DisasmFlags & ACPI_PARSEOP_ASSIGNMENT)
        {
            AcpiOsPrintf (")");
        }
        break;

    case AML_INDEX_OP:

        /* This is case for unsupported Index() source constants */

        if (Op->Common.DisasmFlags & ACPI_PARSEOP_CLOSING_PAREN)
        {
            AcpiOsPrintf (")");
        }
        ASL_CV_PRINT_ONE_COMMENT (Op, AML_COMMENT_END_NODE, NULL, 0);
        return;

    /* No need for parens for these */

    case AML_DECREMENT_OP:
    case AML_INCREMENT_OP:
    case AML_LOGICAL_NOT_OP:
    case AML_BIT_NOT_OP:
    case AML_STORE_OP:
        ASL_CV_PRINT_ONE_COMMENT (Op, AML_COMMENT_END_NODE, NULL, 0);
        return;

    default:

        /* Always emit paren for non-ASL+ operators */
        break;
    }

    AcpiOsPrintf (")");
    ASL_CV_PRINT_ONE_COMMENT (Op, AML_COMMENT_END_NODE, NULL, 0);

    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmGetCompoundSymbol
 *
 * PARAMETERS:  AslOpcode
 *
 * RETURN:      String containing the compound assignment symbol
 *
 * DESCRIPTION: Detect opcodes that can be converted to compound assignment,
 *              return the appropriate operator string.
 *
 ******************************************************************************/

static char *
AcpiDmGetCompoundSymbol (
   UINT16                   AmlOpcode)
{
    char                    *Symbol;


    switch (AmlOpcode)
    {
    case AML_ADD_OP:
        Symbol = " += ";
        break;

    case AML_SUBTRACT_OP:
        Symbol = " -= ";
        break;

    case AML_MULTIPLY_OP:
        Symbol = " *= ";
        break;

    case AML_DIVIDE_OP:
        Symbol = " /= ";
        break;

    case AML_MOD_OP:
        Symbol = " %= ";
        break;

    case AML_SHIFT_LEFT_OP:
        Symbol = " <<= ";
        break;

    case AML_SHIFT_RIGHT_OP:
        Symbol = " >>= ";
        break;

    case AML_BIT_AND_OP:
        Symbol = " &= ";
        break;

    case AML_BIT_OR_OP:
        Symbol = " |= ";
        break;

    case AML_BIT_XOR_OP:
        Symbol = " ^= ";
        break;

    default:

        /* No operator string for all other opcodes */

        return (NULL);
    }

    return (Symbol);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPromoteTarget
 *
 * PARAMETERS:  Op                  - Operator parse object
 *              Target              - Target associate with the Op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Transform the parse tree by moving the target up to the first
 *              child of the Op.
 *
 ******************************************************************************/

static void
AcpiDmPromoteTarget (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *Target)
{
    ACPI_PARSE_OBJECT       *Child;


    /* Link target directly to the Op as first child */

    Child = Op->Common.Value.Arg;
    Op->Common.Value.Arg = Target;
    Target->Common.Next = Child;

    /* Find the last peer, it is linked to the target. Unlink it. */

    while (Child->Common.Next != Target)
    {
        Child = Child->Common.Next;
    }

    Child->Common.Next = NULL;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmIsValidTarget
 *
 * PARAMETERS:  Target              - Target Op from the parse tree
 *
 * RETURN:      TRUE if the Target is real. FALSE if it is just a placeholder
 *              Op that was inserted by the parser.
 *
 * DESCRIPTION: Determine if a Target Op is a placeholder Op or a real Target.
 *              In other words, determine if the optional target is used or
 *              not. Note: If Target is NULL, something is seriously wrong,
 *              probably with the parse tree.
 *
 ******************************************************************************/

static BOOLEAN
AcpiDmIsValidTarget (
    ACPI_PARSE_OBJECT       *Target)
{

    if (!Target)
    {
        return (FALSE);
    }

    if ((Target->Common.AmlOpcode == AML_INT_NAMEPATH_OP) &&
        (Target->Common.Value.Arg == NULL))
    {
        return (FALSE);
    }

    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmIsTargetAnOperand
 *
 * PARAMETERS:  Target              - Target associated with the expression
 *              Operand             - An operand associated with expression
 *
 * RETURN:      TRUE if expression can be converted to a compound assignment.
 *              FALSE otherwise.
 *
 * DESCRIPTION: Determine if the Target duplicates the operand, in order to
 *              detect if the expression can be converted to a compound
 *              assignment. (+=, *=, etc.)
 *
 ******************************************************************************/

static BOOLEAN
AcpiDmIsTargetAnOperand (
    ACPI_PARSE_OBJECT       *Target,
    ACPI_PARSE_OBJECT       *Operand,
    BOOLEAN                 TopLevel)
{
    const ACPI_OPCODE_INFO  *OpInfo;
    BOOLEAN                 Same;


    /*
     * Opcodes must match. Note: ignoring the difference between nameseg
     * and namepath for now. May be needed later.
     */
    if (Target->Common.AmlOpcode != Operand->Common.AmlOpcode)
    {
        return (FALSE);
    }

    /* Nodes should match, even if they are NULL */

    if (Target->Common.Node != Operand->Common.Node)
    {
        return (FALSE);
    }

    /* Determine if a child exists */

    OpInfo = AcpiPsGetOpcodeInfo (Operand->Common.AmlOpcode);
    if (OpInfo->Flags & AML_HAS_ARGS)
    {
        Same = AcpiDmIsTargetAnOperand (Target->Common.Value.Arg,
            Operand->Common.Value.Arg, FALSE);
        if (!Same)
        {
            return (FALSE);
        }
    }

    /* Check the next peer, as long as we are not at the top level */

    if ((!TopLevel) &&
         Target->Common.Next)
    {
        Same = AcpiDmIsTargetAnOperand (Target->Common.Next,
            Operand->Common.Next, FALSE);
        if (!Same)
        {
            return (FALSE);
        }
    }

    /* Suppress the duplicate operand at the top-level */

    if (TopLevel)
    {
        Operand->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
    }
    return (TRUE);
}
