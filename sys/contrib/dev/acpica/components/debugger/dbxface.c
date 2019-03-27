/*******************************************************************************
 *
 * Module Name: dbxface - AML Debugger external interfaces
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
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acdebug.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acparser.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dbxface")


/* Local prototypes */

static ACPI_STATUS
AcpiDbStartCommand (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op);

#ifdef ACPI_OBSOLETE_FUNCTIONS
void
AcpiDbMethodEnd (
    ACPI_WALK_STATE         *WalkState);
#endif

#ifdef ACPI_DISASSEMBLER
static ACPI_PARSE_OBJECT *
AcpiDbGetDisplayOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op);
#endif

/*******************************************************************************
 *
 * FUNCTION:    AcpiDbStartCommand
 *
 * PARAMETERS:  WalkState       - Current walk
 *              Op              - Current executing Op, from AML interpreter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter debugger command loop
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbStartCommand (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_STATUS             Status;


    /* TBD: [Investigate] are there namespace locking issues here? */

    /* AcpiUtReleaseMutex (ACPI_MTX_NAMESPACE); */

    /* Go into the command loop and await next user command */


    AcpiGbl_MethodExecuting = TRUE;
    Status = AE_CTRL_TRUE;

    while (Status == AE_CTRL_TRUE)
    {
        /* Notify the completion of the command */

        Status = AcpiOsNotifyCommandComplete ();
        if (ACPI_FAILURE (Status))
        {
            goto ErrorExit;
        }

        /* Wait the readiness of the command */

        Status = AcpiOsWaitCommandReady ();
        if (ACPI_FAILURE (Status))
        {
            goto ErrorExit;
        }

        Status = AcpiDbCommandDispatch (AcpiGbl_DbLineBuf, WalkState, Op);
    }

    /* AcpiUtAcquireMutex (ACPI_MTX_NAMESPACE); */

ErrorExit:
    if (ACPI_FAILURE (Status) && Status != AE_CTRL_TERMINATE)
    {
        ACPI_EXCEPTION ((AE_INFO, Status,
            "While parsing/handling command line"));
    }
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbSignalBreakPoint
 *
 * PARAMETERS:  WalkState       - Current walk
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called for AML_BREAKPOINT_OP
 *
 ******************************************************************************/

void
AcpiDbSignalBreakPoint (
    ACPI_WALK_STATE         *WalkState)
{

#ifndef ACPI_APPLICATION
    if (AcpiGbl_DbThreadId != AcpiOsGetThreadId ())
    {
        return;
    }
#endif

    /*
     * Set the single-step flag. This will cause the debugger (if present)
     * to break to the console within the AML debugger at the start of the
     * next AML instruction.
     */
    AcpiGbl_CmSingleStep = TRUE;
    AcpiOsPrintf ("**break** Executed AML BreakPoint opcode\n");
}


#ifdef ACPI_DISASSEMBLER
/*******************************************************************************
 *
 * FUNCTION:    AcpiDbGetDisplayOp
 *
 * PARAMETERS:  WalkState       - Current walk
 *              Op              - Current executing op (from aml interpreter)
 *
 * RETURN:      Opcode to display
 *
 * DESCRIPTION: Find the opcode to display during single stepping
 *
 ******************************************************************************/

static ACPI_PARSE_OBJECT *
AcpiDbGetDisplayOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *DisplayOp;
    ACPI_PARSE_OBJECT       *ParentOp;

    DisplayOp = Op;
    ParentOp = Op->Common.Parent;
    if (ParentOp)
    {
        if ((WalkState->ControlState) &&
            (WalkState->ControlState->Common.State ==
                ACPI_CONTROL_PREDICATE_EXECUTING))
        {
            /*
             * We are executing the predicate of an IF or WHILE statement
             * Search upwards for the containing IF or WHILE so that the
             * entire predicate can be displayed.
             */
            while (ParentOp)
            {
                if ((ParentOp->Common.AmlOpcode == AML_IF_OP) ||
                    (ParentOp->Common.AmlOpcode == AML_WHILE_OP))
                {
                    DisplayOp = ParentOp;
                    break;
                }
                ParentOp = ParentOp->Common.Parent;
            }
        }
        else
        {
            while (ParentOp)
            {
                if ((ParentOp->Common.AmlOpcode == AML_IF_OP)     ||
                    (ParentOp->Common.AmlOpcode == AML_ELSE_OP)   ||
                    (ParentOp->Common.AmlOpcode == AML_SCOPE_OP)  ||
                    (ParentOp->Common.AmlOpcode == AML_METHOD_OP) ||
                    (ParentOp->Common.AmlOpcode == AML_WHILE_OP))
                {
                    break;
                }
                DisplayOp = ParentOp;
                ParentOp = ParentOp->Common.Parent;
            }
        }
    }
    return DisplayOp;
}
#endif


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbSingleStep
 *
 * PARAMETERS:  WalkState       - Current walk
 *              Op              - Current executing op (from aml interpreter)
 *              OpcodeClass     - Class of the current AML Opcode
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called just before execution of an AML opcode.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbSingleStep (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  OpcodeClass)
{
    ACPI_PARSE_OBJECT       *Next;
    ACPI_STATUS             Status = AE_OK;
    UINT32                  OriginalDebugLevel;
    UINT32                  AmlOffset;


    ACPI_FUNCTION_ENTRY ();


#ifndef ACPI_APPLICATION
    if (AcpiGbl_DbThreadId != AcpiOsGetThreadId ())
    {
        return (AE_OK);
    }
#endif

    /* Check the abort flag */

    if (AcpiGbl_AbortMethod)
    {
        AcpiGbl_AbortMethod = FALSE;
        return (AE_ABORT_METHOD);
    }

    AmlOffset = (UINT32) ACPI_PTR_DIFF (Op->Common.Aml,
        WalkState->ParserState.AmlStart);

    /* Check for single-step breakpoint */

    if (WalkState->MethodBreakpoint &&
       (WalkState->MethodBreakpoint <= AmlOffset))
    {
        /* Check if the breakpoint has been reached or passed */
        /* Hit the breakpoint, resume single step, reset breakpoint */

        AcpiOsPrintf ("***Break*** at AML offset %X\n", AmlOffset);
        AcpiGbl_CmSingleStep = TRUE;
        AcpiGbl_StepToNextCall = FALSE;
        WalkState->MethodBreakpoint = 0;
    }

    /* Check for user breakpoint (Must be on exact Aml offset) */

    else if (WalkState->UserBreakpoint &&
            (WalkState->UserBreakpoint == AmlOffset))
    {
        AcpiOsPrintf ("***UserBreakpoint*** at AML offset %X\n",
            AmlOffset);
        AcpiGbl_CmSingleStep = TRUE;
        AcpiGbl_StepToNextCall = FALSE;
        WalkState->MethodBreakpoint = 0;
    }

    /*
     * Check if this is an opcode that we are interested in --
     * namely, opcodes that have arguments
     */
    if (Op->Common.AmlOpcode == AML_INT_NAMEDFIELD_OP)
    {
        return (AE_OK);
    }

    switch (OpcodeClass)
    {
    case AML_CLASS_UNKNOWN:
    case AML_CLASS_ARGUMENT:    /* constants, literals, etc. do nothing */

        return (AE_OK);

    default:

        /* All other opcodes -- continue */
        break;
    }

    /*
     * Under certain debug conditions, display this opcode and its operands
     */
    if ((AcpiGbl_DbOutputToFile)            ||
        (AcpiGbl_CmSingleStep)              ||
        (AcpiDbgLevel & ACPI_LV_PARSE))
    {
        if ((AcpiGbl_DbOutputToFile)        ||
            (AcpiDbgLevel & ACPI_LV_PARSE))
        {
            AcpiOsPrintf ("\nAML Debug: Next AML Opcode to execute:\n");
        }

        /*
         * Display this op (and only this op - zero out the NEXT field
         * temporarily, and disable parser trace output for the duration of
         * the display because we don't want the extraneous debug output)
         */
        OriginalDebugLevel = AcpiDbgLevel;
        AcpiDbgLevel &= ~(ACPI_LV_PARSE | ACPI_LV_FUNCTIONS);
        Next = Op->Common.Next;
        Op->Common.Next = NULL;

        /* Now we can disassemble and display it */

#ifdef ACPI_DISASSEMBLER
        AcpiDmDisassemble (WalkState, AcpiDbGetDisplayOp (WalkState, Op),
            ACPI_UINT32_MAX);
#else
        /*
         * The AML Disassembler is not configured - at least we can
         * display the opcode value and name
         */
        AcpiOsPrintf ("AML Opcode: %4.4X  %s\n", Op->Common.AmlOpcode,
            AcpiPsGetOpcodeName (Op->Common.AmlOpcode));
#endif

        if ((Op->Common.AmlOpcode == AML_IF_OP) ||
            (Op->Common.AmlOpcode == AML_WHILE_OP))
        {
            if (WalkState->ControlState->Common.Value)
            {
                AcpiOsPrintf ("Predicate = [True], IF block was executed\n");
            }
            else
            {
                AcpiOsPrintf ("Predicate = [False], Skipping IF block\n");
            }
        }
        else if (Op->Common.AmlOpcode == AML_ELSE_OP)
        {
            AcpiOsPrintf ("Predicate = [False], ELSE block was executed\n");
        }

        /* Restore everything */

        Op->Common.Next = Next;
        AcpiOsPrintf ("\n");
        if ((AcpiGbl_DbOutputToFile)        ||
            (AcpiDbgLevel & ACPI_LV_PARSE))
        {
            AcpiOsPrintf ("\n");
        }
        AcpiDbgLevel = OriginalDebugLevel;
    }

    /* If we are not single stepping, just continue executing the method */

    if (!AcpiGbl_CmSingleStep)
    {
        return (AE_OK);
    }

    /*
     * If we are executing a step-to-call command,
     * Check if this is a method call.
     */
    if (AcpiGbl_StepToNextCall)
    {
        if (Op->Common.AmlOpcode != AML_INT_METHODCALL_OP)
        {
            /* Not a method call, just keep executing */

            return (AE_OK);
        }

        /* Found a method call, stop executing */

        AcpiGbl_StepToNextCall = FALSE;
    }

    /*
     * If the next opcode is a method call, we will "step over" it
     * by default.
     */
    if (Op->Common.AmlOpcode == AML_INT_METHODCALL_OP)
    {
        /* Force no more single stepping while executing called method */

        AcpiGbl_CmSingleStep = FALSE;

        /*
         * Set the breakpoint on/before the call, it will stop execution
         * as soon as we return
         */
        WalkState->MethodBreakpoint = 1;  /* Must be non-zero! */
    }


    AcpiExExitInterpreter ();
    Status = AcpiDbStartCommand (WalkState, Op);
    AcpiExEnterInterpreter ();

    /* User commands complete, continue execution of the interrupted method */

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiInitializeDebugger
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Init and start debugger
 *
 ******************************************************************************/

ACPI_STATUS
AcpiInitializeDebugger (
    void)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (AcpiInitializeDebugger);


    /* Init globals */

    AcpiGbl_DbBuffer            = NULL;
    AcpiGbl_DbFilename          = NULL;
    AcpiGbl_DbOutputToFile      = FALSE;

    AcpiGbl_DbDebugLevel        = ACPI_LV_VERBOSITY2;
    AcpiGbl_DbConsoleDebugLevel = ACPI_NORMAL_DEFAULT | ACPI_LV_TABLES;
    AcpiGbl_DbOutputFlags       = ACPI_DB_CONSOLE_OUTPUT;

    AcpiGbl_DbOpt_NoIniMethods  = FALSE;

    AcpiGbl_DbBuffer = AcpiOsAllocate (ACPI_DEBUG_BUFFER_SIZE);
    if (!AcpiGbl_DbBuffer)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }
    memset (AcpiGbl_DbBuffer, 0, ACPI_DEBUG_BUFFER_SIZE);

    /* Initial scope is the root */

    AcpiGbl_DbScopeBuf [0] = AML_ROOT_PREFIX;
    AcpiGbl_DbScopeBuf [1] =  0;
    AcpiGbl_DbScopeNode = AcpiGbl_RootNode;

    /* Initialize user commands loop */

    AcpiGbl_DbTerminateLoop = FALSE;

    /*
     * If configured for multi-thread support, the debug executor runs in
     * a separate thread so that the front end can be in another address
     * space, environment, or even another machine.
     */
    if (AcpiGbl_DebuggerConfiguration & DEBUGGER_MULTI_THREADED)
    {
        /* These were created with one unit, grab it */

        Status = AcpiOsInitializeDebugger ();
        if (ACPI_FAILURE (Status))
        {
            AcpiOsPrintf ("Could not get debugger mutex\n");
            return_ACPI_STATUS (Status);
        }

        /* Create the debug execution thread to execute commands */

        AcpiGbl_DbThreadsTerminated = FALSE;
        Status = AcpiOsExecute (OSL_DEBUGGER_MAIN_THREAD,
            AcpiDbExecuteThread, NULL);
        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status,
                "Could not start debugger thread"));
            AcpiGbl_DbThreadsTerminated = TRUE;
            return_ACPI_STATUS (Status);
        }
    }
    else
    {
        AcpiGbl_DbThreadId = AcpiOsGetThreadId ();
    }

    return_ACPI_STATUS (AE_OK);
}

ACPI_EXPORT_SYMBOL (AcpiInitializeDebugger)


/*******************************************************************************
 *
 * FUNCTION:    AcpiTerminateDebugger
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Stop debugger
 *
 ******************************************************************************/

void
AcpiTerminateDebugger (
    void)
{

    /* Terminate the AML Debugger */

    AcpiGbl_DbTerminateLoop = TRUE;

    if (AcpiGbl_DebuggerConfiguration & DEBUGGER_MULTI_THREADED)
    {
        /* Wait the AML Debugger threads */

        while (!AcpiGbl_DbThreadsTerminated)
        {
            AcpiOsSleep (100);
        }

        AcpiOsTerminateDebugger ();
    }

    if (AcpiGbl_DbBuffer)
    {
        AcpiOsFree (AcpiGbl_DbBuffer);
        AcpiGbl_DbBuffer = NULL;
    }

    /* Ensure that debug output is now disabled */

    AcpiGbl_DbOutputFlags = ACPI_DB_DISABLE_OUTPUT;
}

ACPI_EXPORT_SYMBOL (AcpiTerminateDebugger)


/*******************************************************************************
 *
 * FUNCTION:    AcpiSetDebuggerThreadId
 *
 * PARAMETERS:  ThreadId        - Debugger thread ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set debugger thread ID
 *
 ******************************************************************************/

void
AcpiSetDebuggerThreadId (
    ACPI_THREAD_ID          ThreadId)
{
    AcpiGbl_DbThreadId = ThreadId;
}

ACPI_EXPORT_SYMBOL (AcpiSetDebuggerThreadId)
