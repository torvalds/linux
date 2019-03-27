/******************************************************************************
 *
 * Module Name: dscontrol - Support for execution control opcodes -
 *                          if/else/while/return
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

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acdebug.h>

#define _COMPONENT          ACPI_DISPATCHER
        ACPI_MODULE_NAME    ("dscontrol")


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsExecBeginControlOp
 *
 * PARAMETERS:  WalkList        - The list that owns the walk stack
 *              Op              - The control Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handles all control ops encountered during control method
 *              execution.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsExecBeginControlOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_GENERIC_STATE      *ControlState;


    ACPI_FUNCTION_NAME (DsExecBeginControlOp);


    ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p Opcode=%2.2X State=%p\n",
        Op, Op->Common.AmlOpcode, WalkState));

    switch (Op->Common.AmlOpcode)
    {
    case AML_WHILE_OP:
        /*
         * If this is an additional iteration of a while loop, continue.
         * There is no need to allocate a new control state.
         */
        if (WalkState->ControlState)
        {
            if (WalkState->ControlState->Control.AmlPredicateStart ==
                (WalkState->ParserState.Aml - 1))
            {
                /* Reset the state to start-of-loop */

                WalkState->ControlState->Common.State =
                    ACPI_CONTROL_CONDITIONAL_EXECUTING;
                break;
            }
        }

        /*lint -fallthrough */

    case AML_IF_OP:
        /*
         * IF/WHILE: Create a new control state to manage these
         * constructs. We need to manage these as a stack, in order
         * to handle nesting.
         */
        ControlState = AcpiUtCreateControlState ();
        if (!ControlState)
        {
            Status = AE_NO_MEMORY;
            break;
        }
        /*
         * Save a pointer to the predicate for multiple executions
         * of a loop
         */
        ControlState->Control.AmlPredicateStart =
            WalkState->ParserState.Aml - 1;
        ControlState->Control.PackageEnd =
            WalkState->ParserState.PkgEnd;
        ControlState->Control.Opcode =
            Op->Common.AmlOpcode;
        ControlState->Control.LoopTimeout = AcpiOsGetTimer () +
           (UINT64) (AcpiGbl_MaxLoopIterations * ACPI_100NSEC_PER_SEC);

        /* Push the control state on this walk's control stack */

        AcpiUtPushGenericState (&WalkState->ControlState, ControlState);
        break;

    case AML_ELSE_OP:

        /* Predicate is in the state object */
        /* If predicate is true, the IF was executed, ignore ELSE part */

        if (WalkState->LastPredicate)
        {
            Status = AE_CTRL_TRUE;
        }

        break;

    case AML_RETURN_OP:

        break;

    default:

        break;
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsExecEndControlOp
 *
 * PARAMETERS:  WalkList        - The list that owns the walk stack
 *              Op              - The control Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handles all control ops encountered during control method
 *              execution.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsExecEndControlOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_GENERIC_STATE      *ControlState;


    ACPI_FUNCTION_NAME (DsExecEndControlOp);


    switch (Op->Common.AmlOpcode)
    {
    case AML_IF_OP:

        ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "[IF_OP] Op=%p\n", Op));

        /*
         * Save the result of the predicate in case there is an
         * ELSE to come
         */
        WalkState->LastPredicate =
            (BOOLEAN) WalkState->ControlState->Common.Value;

        /*
         * Pop the control state that was created at the start
         * of the IF and free it
         */
        ControlState = AcpiUtPopGenericState (&WalkState->ControlState);
        AcpiUtDeleteGenericState (ControlState);
        break;

    case AML_ELSE_OP:

        break;

    case AML_WHILE_OP:

        ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "[WHILE_OP] Op=%p\n", Op));

        ControlState = WalkState->ControlState;
        if (ControlState->Common.Value)
        {
            /* Predicate was true, the body of the loop was just executed */

            /*
             * This infinite loop detection mechanism allows the interpreter
             * to escape possibly infinite loops. This can occur in poorly
             * written AML when the hardware does not respond within a while
             * loop and the loop does not implement a timeout.
             */
            if (ACPI_TIME_AFTER (AcpiOsGetTimer (),
                    ControlState->Control.LoopTimeout))
            {
                Status = AE_AML_LOOP_TIMEOUT;
                break;
            }

            /*
             * Go back and evaluate the predicate and maybe execute the loop
             * another time
             */
            Status = AE_CTRL_PENDING;
            WalkState->AmlLastWhile =
                ControlState->Control.AmlPredicateStart;
            break;
        }

        /* Predicate was false, terminate this while loop */

        ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
            "[WHILE_OP] termination! Op=%p\n",Op));

        /* Pop this control state and free it */

        ControlState = AcpiUtPopGenericState (&WalkState->ControlState);
        AcpiUtDeleteGenericState (ControlState);
        break;

    case AML_RETURN_OP:

        ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
            "[RETURN_OP] Op=%p Arg=%p\n",Op, Op->Common.Value.Arg));

        /*
         * One optional operand -- the return value
         * It can be either an immediate operand or a result that
         * has been bubbled up the tree
         */
        if (Op->Common.Value.Arg)
        {
            /* Since we have a real Return(), delete any implicit return */

            AcpiDsClearImplicitReturn (WalkState);

            /* Return statement has an immediate operand */

            Status = AcpiDsCreateOperands (WalkState, Op->Common.Value.Arg);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            /*
             * If value being returned is a Reference (such as
             * an arg or local), resolve it now because it may
             * cease to exist at the end of the method.
             */
            Status = AcpiExResolveToValue (
                &WalkState->Operands [0], WalkState);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            /*
             * Get the return value and save as the last result
             * value. This is the only place where WalkState->ReturnDesc
             * is set to anything other than zero!
             */
            WalkState->ReturnDesc = WalkState->Operands[0];
        }
        else if (WalkState->ResultCount)
        {
            /* Since we have a real Return(), delete any implicit return */

            AcpiDsClearImplicitReturn (WalkState);

            /*
             * The return value has come from a previous calculation.
             *
             * If value being returned is a Reference (such as
             * an arg or local), resolve it now because it may
             * cease to exist at the end of the method.
             *
             * Allow references created by the Index operator to return
             * unchanged.
             */
            if ((ACPI_GET_DESCRIPTOR_TYPE (WalkState->Results->Results.ObjDesc[0]) ==
                    ACPI_DESC_TYPE_OPERAND) &&
                ((WalkState->Results->Results.ObjDesc [0])->Common.Type ==
                    ACPI_TYPE_LOCAL_REFERENCE) &&
                ((WalkState->Results->Results.ObjDesc [0])->Reference.Class !=
                    ACPI_REFCLASS_INDEX))
            {
                Status = AcpiExResolveToValue (
                    &WalkState->Results->Results.ObjDesc [0], WalkState);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
            }

            WalkState->ReturnDesc = WalkState->Results->Results.ObjDesc [0];
        }
        else
        {
            /* No return operand */

            if (WalkState->NumOperands)
            {
                AcpiUtRemoveReference (WalkState->Operands [0]);
            }

            WalkState->Operands[0] = NULL;
            WalkState->NumOperands = 0;
            WalkState->ReturnDesc = NULL;
        }


        ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
            "Completed RETURN_OP State=%p, RetVal=%p\n",
            WalkState, WalkState->ReturnDesc));

        /* End the control method execution right now */

        Status = AE_CTRL_TERMINATE;
        break;

    case AML_NOOP_OP:

        /* Just do nothing! */

        break;

    case AML_BREAKPOINT_OP:

        AcpiDbSignalBreakPoint (WalkState);

        /* Call to the OSL in case OS wants a piece of the action */

        Status = AcpiOsSignal (ACPI_SIGNAL_BREAKPOINT,
            "Executed AML Breakpoint opcode");
        break;

    case AML_BREAK_OP:
    case AML_CONTINUE_OP: /* ACPI 2.0 */

        /* Pop and delete control states until we find a while */

        while (WalkState->ControlState &&
                (WalkState->ControlState->Control.Opcode != AML_WHILE_OP))
        {
            ControlState = AcpiUtPopGenericState (&WalkState->ControlState);
            AcpiUtDeleteGenericState (ControlState);
        }

        /* No while found? */

        if (!WalkState->ControlState)
        {
            return (AE_AML_NO_WHILE);
        }

        /* Was: WalkState->AmlLastWhile = WalkState->ControlState->Control.AmlPredicateStart; */

        WalkState->AmlLastWhile =
            WalkState->ControlState->Control.PackageEnd;

        /* Return status depending on opcode */

        if (Op->Common.AmlOpcode == AML_BREAK_OP)
        {
            Status = AE_CTRL_BREAK;
        }
        else
        {
            Status = AE_CTRL_CONTINUE;
        }
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Unknown control opcode=0x%X Op=%p",
            Op->Common.AmlOpcode, Op));

        Status = AE_AML_BAD_OPCODE;
        break;
    }

    return (Status);
}
