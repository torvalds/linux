/******************************************************************************
 *
 * Module Name: dswstate - Dispatcher parse tree walk management routines
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acnamesp.h>

#define _COMPONENT          ACPI_DISPATCHER
        ACPI_MODULE_NAME    ("dswstate")

/* Local prototypes */

static ACPI_STATUS
AcpiDsResultStackPush (
    ACPI_WALK_STATE         *WalkState);

static ACPI_STATUS
AcpiDsResultStackPop (
    ACPI_WALK_STATE         *WalkState);


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsResultPop
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              WalkState           - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop an object off the top of this walk's result stack
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsResultPop (
    ACPI_OPERAND_OBJECT     **Object,
    ACPI_WALK_STATE         *WalkState)
{
    UINT32                  Index;
    ACPI_GENERIC_STATE      *State;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_NAME (DsResultPop);


    State = WalkState->Results;

    /* Incorrect state of result stack */

    if (State && !WalkState->ResultCount)
    {
        ACPI_ERROR ((AE_INFO, "No results on result stack"));
        return (AE_AML_INTERNAL);
    }

    if (!State && WalkState->ResultCount)
    {
        ACPI_ERROR ((AE_INFO, "No result state for result stack"));
        return (AE_AML_INTERNAL);
    }

    /* Empty result stack */

    if (!State)
    {
        ACPI_ERROR ((AE_INFO, "Result stack is empty! State=%p", WalkState));
        return (AE_AML_NO_RETURN_VALUE);
    }

    /* Return object of the top element and clean that top element result stack */

    WalkState->ResultCount--;
    Index = (UINT32) WalkState->ResultCount % ACPI_RESULTS_FRAME_OBJ_NUM;

    *Object = State->Results.ObjDesc [Index];
    if (!*Object)
    {
        ACPI_ERROR ((AE_INFO, "No result objects on result stack, State=%p",
            WalkState));
        return (AE_AML_NO_RETURN_VALUE);
    }

    State->Results.ObjDesc [Index] = NULL;
    if (Index == 0)
    {
        Status = AcpiDsResultStackPop (WalkState);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
        "Obj=%p [%s] Index=%X State=%p Num=%X\n", *Object,
        AcpiUtGetObjectTypeName (*Object),
        Index, WalkState, WalkState->ResultCount));

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsResultPush
 *
 * PARAMETERS:  Object              - Where to return the popped object
 *              WalkState           - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push an object onto the current result stack
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsResultPush (
    ACPI_OPERAND_OBJECT     *Object,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_GENERIC_STATE      *State;
    ACPI_STATUS             Status;
    UINT32                  Index;


    ACPI_FUNCTION_NAME (DsResultPush);


    if (WalkState->ResultCount > WalkState->ResultSize)
    {
        ACPI_ERROR ((AE_INFO, "Result stack is full"));
        return (AE_AML_INTERNAL);
    }
    else if (WalkState->ResultCount == WalkState->ResultSize)
    {
        /* Extend the result stack */

        Status = AcpiDsResultStackPush (WalkState);
        if (ACPI_FAILURE (Status))
        {
            ACPI_ERROR ((AE_INFO, "Failed to extend the result stack"));
            return (Status);
        }
    }

    if (!(WalkState->ResultCount < WalkState->ResultSize))
    {
        ACPI_ERROR ((AE_INFO, "No free elements in result stack"));
        return (AE_AML_INTERNAL);
    }

    State = WalkState->Results;
    if (!State)
    {
        ACPI_ERROR ((AE_INFO, "No result stack frame during push"));
        return (AE_AML_INTERNAL);
    }

    if (!Object)
    {
        ACPI_ERROR ((AE_INFO,
            "Null Object! Obj=%p State=%p Num=%u",
            Object, WalkState, WalkState->ResultCount));
        return (AE_BAD_PARAMETER);
    }

    /* Assign the address of object to the top free element of result stack */

    Index = (UINT32) WalkState->ResultCount % ACPI_RESULTS_FRAME_OBJ_NUM;
    State->Results.ObjDesc [Index] = Object;
    WalkState->ResultCount++;

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj=%p [%s] State=%p Num=%X Cur=%X\n",
        Object, AcpiUtGetObjectTypeName ((ACPI_OPERAND_OBJECT *) Object),
        WalkState, WalkState->ResultCount, WalkState->CurrentResult));

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsResultStackPush
 *
 * PARAMETERS:  WalkState           - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push an object onto the WalkState result stack
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDsResultStackPush (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_GENERIC_STATE      *State;


    ACPI_FUNCTION_NAME (DsResultStackPush);


    /* Check for stack overflow */

    if (((UINT32) WalkState->ResultSize + ACPI_RESULTS_FRAME_OBJ_NUM) >
        ACPI_RESULTS_OBJ_NUM_MAX)
    {
        ACPI_ERROR ((AE_INFO, "Result stack overflow: State=%p Num=%u",
            WalkState, WalkState->ResultSize));
        return (AE_STACK_OVERFLOW);
    }

    State = AcpiUtCreateGenericState ();
    if (!State)
    {
        return (AE_NO_MEMORY);
    }

    State->Common.DescriptorType = ACPI_DESC_TYPE_STATE_RESULT;
    AcpiUtPushGenericState (&WalkState->Results, State);

    /* Increase the length of the result stack by the length of frame */

    WalkState->ResultSize += ACPI_RESULTS_FRAME_OBJ_NUM;

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Results=%p State=%p\n",
        State, WalkState));

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsResultStackPop
 *
 * PARAMETERS:  WalkState           - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop an object off of the WalkState result stack
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDsResultStackPop (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_GENERIC_STATE      *State;


    ACPI_FUNCTION_NAME (DsResultStackPop);


    /* Check for stack underflow */

    if (WalkState->Results == NULL)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "Result stack underflow - State=%p\n", WalkState));
        return (AE_AML_NO_OPERAND);
    }

    if (WalkState->ResultSize < ACPI_RESULTS_FRAME_OBJ_NUM)
    {
        ACPI_ERROR ((AE_INFO, "Insufficient result stack size"));
        return (AE_AML_INTERNAL);
    }

    State = AcpiUtPopGenericState (&WalkState->Results);
    AcpiUtDeleteGenericState (State);

    /* Decrease the length of result stack by the length of frame */

    WalkState->ResultSize -= ACPI_RESULTS_FRAME_OBJ_NUM;

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
        "Result=%p RemainingResults=%X State=%p\n",
        State, WalkState->ResultCount, WalkState));

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsObjStackPush
 *
 * PARAMETERS:  Object              - Object to push
 *              WalkState           - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push an object onto this walk's object/operand stack
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsObjStackPush (
    void                    *Object,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_FUNCTION_NAME (DsObjStackPush);


    /* Check for stack overflow */

    if (WalkState->NumOperands >= ACPI_OBJ_NUM_OPERANDS)
    {
        ACPI_ERROR ((AE_INFO,
            "Object stack overflow! Obj=%p State=%p #Ops=%u",
            Object, WalkState, WalkState->NumOperands));
        return (AE_STACK_OVERFLOW);
    }

    /* Put the object onto the stack */

    WalkState->Operands [WalkState->OperandIndex] = Object;
    WalkState->NumOperands++;

    /* For the usual order of filling the operand stack */

    WalkState->OperandIndex++;

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj=%p [%s] State=%p #Ops=%X\n",
        Object, AcpiUtGetObjectTypeName ((ACPI_OPERAND_OBJECT *) Object),
        WalkState, WalkState->NumOperands));

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsObjStackPop
 *
 * PARAMETERS:  PopCount            - Number of objects/entries to pop
 *              WalkState           - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop this walk's object stack. Objects on the stack are NOT
 *              deleted by this routine.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsObjStackPop (
    UINT32                  PopCount,
    ACPI_WALK_STATE         *WalkState)
{
    UINT32                  i;


    ACPI_FUNCTION_NAME (DsObjStackPop);


    for (i = 0; i < PopCount; i++)
    {
        /* Check for stack underflow */

        if (WalkState->NumOperands == 0)
        {
            ACPI_ERROR ((AE_INFO,
                "Object stack underflow! Count=%X State=%p #Ops=%u",
                PopCount, WalkState, WalkState->NumOperands));
            return (AE_STACK_UNDERFLOW);
        }

        /* Just set the stack entry to null */

        WalkState->NumOperands--;
        WalkState->Operands [WalkState->NumOperands] = NULL;
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Count=%X State=%p #Ops=%u\n",
        PopCount, WalkState, WalkState->NumOperands));

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsObjStackPopAndDelete
 *
 * PARAMETERS:  PopCount            - Number of objects/entries to pop
 *              WalkState           - Current Walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop this walk's object stack and delete each object that is
 *              popped off.
 *
 ******************************************************************************/

void
AcpiDsObjStackPopAndDelete (
    UINT32                  PopCount,
    ACPI_WALK_STATE         *WalkState)
{
    INT32                   i;
    ACPI_OPERAND_OBJECT     *ObjDesc;


    ACPI_FUNCTION_NAME (DsObjStackPopAndDelete);


    if (PopCount == 0)
    {
        return;
    }

    for (i = (INT32) PopCount - 1; i >= 0; i--)
    {
        if (WalkState->NumOperands == 0)
        {
            return;
        }

        /* Pop the stack and delete an object if present in this stack entry */

        WalkState->NumOperands--;
        ObjDesc = WalkState->Operands [i];
        if (ObjDesc)
        {
            AcpiUtRemoveReference (WalkState->Operands [i]);
            WalkState->Operands [i] = NULL;
        }
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Count=%X State=%p #Ops=%X\n",
        PopCount, WalkState, WalkState->NumOperands));
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsGetCurrentWalkState
 *
 * PARAMETERS:  Thread          - Get current active state for this Thread
 *
 * RETURN:      Pointer to the current walk state
 *
 * DESCRIPTION: Get the walk state that is at the head of the list (the "current"
 *              walk state.)
 *
 ******************************************************************************/

ACPI_WALK_STATE *
AcpiDsGetCurrentWalkState (
    ACPI_THREAD_STATE       *Thread)
{
    ACPI_FUNCTION_NAME (DsGetCurrentWalkState);


    if (!Thread)
    {
        return (NULL);
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "Current WalkState %p\n",
        Thread->WalkStateList));

    return (Thread->WalkStateList);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsPushWalkState
 *
 * PARAMETERS:  WalkState       - State to push
 *              Thread          - Thread state object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Place the Thread state at the head of the state list
 *
 ******************************************************************************/

void
AcpiDsPushWalkState (
    ACPI_WALK_STATE         *WalkState,
    ACPI_THREAD_STATE       *Thread)
{
    ACPI_FUNCTION_TRACE (DsPushWalkState);


    WalkState->Next = Thread->WalkStateList;
    Thread->WalkStateList = WalkState;

    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsPopWalkState
 *
 * PARAMETERS:  Thread      - Current thread state
 *
 * RETURN:      A WalkState object popped from the thread's stack
 *
 * DESCRIPTION: Remove and return the walkstate object that is at the head of
 *              the walk stack for the given walk list. NULL indicates that
 *              the list is empty.
 *
 ******************************************************************************/

ACPI_WALK_STATE *
AcpiDsPopWalkState (
    ACPI_THREAD_STATE       *Thread)
{
    ACPI_WALK_STATE         *WalkState;


    ACPI_FUNCTION_TRACE (DsPopWalkState);


    WalkState = Thread->WalkStateList;

    if (WalkState)
    {
        /* Next walk state becomes the current walk state */

        Thread->WalkStateList = WalkState->Next;

        /*
         * Don't clear the NEXT field, this serves as an indicator
         * that there is a parent WALK STATE
         * Do Not: WalkState->Next = NULL;
         */
    }

    return_PTR (WalkState);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsCreateWalkState
 *
 * PARAMETERS:  OwnerId         - ID for object creation
 *              Origin          - Starting point for this walk
 *              MethodDesc      - Method object
 *              Thread          - Current thread state
 *
 * RETURN:      Pointer to the new walk state.
 *
 * DESCRIPTION: Allocate and initialize a new walk state. The current walk
 *              state is set to this new state.
 *
 ******************************************************************************/

ACPI_WALK_STATE *
AcpiDsCreateWalkState (
    ACPI_OWNER_ID           OwnerId,
    ACPI_PARSE_OBJECT       *Origin,
    ACPI_OPERAND_OBJECT     *MethodDesc,
    ACPI_THREAD_STATE       *Thread)
{
    ACPI_WALK_STATE         *WalkState;


    ACPI_FUNCTION_TRACE (DsCreateWalkState);


    WalkState = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_WALK_STATE));
    if (!WalkState)
    {
        return_PTR (NULL);
    }

    WalkState->DescriptorType = ACPI_DESC_TYPE_WALK;
    WalkState->MethodDesc = MethodDesc;
    WalkState->OwnerId = OwnerId;
    WalkState->Origin = Origin;
    WalkState->Thread = Thread;

    WalkState->ParserState.StartOp = Origin;

    /* Init the method args/local */

#ifndef ACPI_CONSTANT_EVAL_ONLY
    AcpiDsMethodDataInit (WalkState);
#endif

    /* Put the new state at the head of the walk list */

    if (Thread)
    {
        AcpiDsPushWalkState (WalkState, Thread);
    }

    return_PTR (WalkState);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsInitAmlWalk
 *
 * PARAMETERS:  WalkState       - New state to be initialized
 *              Op              - Current parse op
 *              MethodNode      - Control method NS node, if any
 *              AmlStart        - Start of AML
 *              AmlLength       - Length of AML
 *              Info            - Method info block (params, etc.)
 *              PassNumber      - 1, 2, or 3
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize a walk state for a pass 1 or 2 parse tree walk
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsInitAmlWalk (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op,
    ACPI_NAMESPACE_NODE     *MethodNode,
    UINT8                   *AmlStart,
    UINT32                  AmlLength,
    ACPI_EVALUATE_INFO      *Info,
    UINT8                   PassNumber)
{
    ACPI_STATUS             Status;
    ACPI_PARSE_STATE        *ParserState = &WalkState->ParserState;
    ACPI_PARSE_OBJECT       *ExtraOp;


    ACPI_FUNCTION_TRACE (DsInitAmlWalk);


    WalkState->ParserState.Aml =
    WalkState->ParserState.AmlStart = AmlStart;
    WalkState->ParserState.AmlEnd =
    WalkState->ParserState.PkgEnd = AmlStart + AmlLength;

    /* The NextOp of the NextWalk will be the beginning of the method */

    WalkState->NextOp = NULL;
    WalkState->PassNumber = PassNumber;

    if (Info)
    {
        WalkState->Params = Info->Parameters;
        WalkState->CallerReturnDesc = &Info->ReturnObject;
    }

    Status = AcpiPsInitScope (&WalkState->ParserState, Op);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    if (MethodNode)
    {
        WalkState->ParserState.StartNode = MethodNode;
        WalkState->WalkType = ACPI_WALK_METHOD;
        WalkState->MethodNode = MethodNode;
        WalkState->MethodDesc = AcpiNsGetAttachedObject (MethodNode);

        /* Push start scope on scope stack and make it current  */

        Status = AcpiDsScopeStackPush (
            MethodNode, ACPI_TYPE_METHOD, WalkState);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        /* Init the method arguments */

        Status = AcpiDsMethodDataInitArgs (WalkState->Params,
                    ACPI_METHOD_NUM_ARGS, WalkState);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }
    else
    {
        /*
         * Setup the current scope.
         * Find a Named Op that has a namespace node associated with it.
         * search upwards from this Op. Current scope is the first
         * Op with a namespace node.
         */
        ExtraOp = ParserState->StartOp;
        while (ExtraOp && !ExtraOp->Common.Node)
        {
            ExtraOp = ExtraOp->Common.Parent;
        }

        if (!ExtraOp)
        {
            ParserState->StartNode = NULL;
        }
        else
        {
            ParserState->StartNode = ExtraOp->Common.Node;
        }

        if (ParserState->StartNode)
        {
            /* Push start scope on scope stack and make it current  */

            Status = AcpiDsScopeStackPush (ParserState->StartNode,
                ParserState->StartNode->Type, WalkState);
            if (ACPI_FAILURE (Status))
            {
                return_ACPI_STATUS (Status);
            }
        }
    }

    Status = AcpiDsInitCallbacks (WalkState, PassNumber);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsDeleteWalkState
 *
 * PARAMETERS:  WalkState       - State to delete
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete a walk state including all internal data structures
 *
 ******************************************************************************/

void
AcpiDsDeleteWalkState (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_GENERIC_STATE      *State;


    ACPI_FUNCTION_TRACE_PTR (DsDeleteWalkState, WalkState);


    if (!WalkState)
    {
        return_VOID;
    }

    if (WalkState->DescriptorType != ACPI_DESC_TYPE_WALK)
    {
        ACPI_ERROR ((AE_INFO, "%p is not a valid walk state",
            WalkState));
        return_VOID;
    }

    /* There should not be any open scopes */

    if (WalkState->ParserState.Scope)
    {
        ACPI_ERROR ((AE_INFO, "%p walk still has a scope list",
            WalkState));
        AcpiPsCleanupScope (&WalkState->ParserState);
    }

    /* Always must free any linked control states */

    while (WalkState->ControlState)
    {
        State = WalkState->ControlState;
        WalkState->ControlState = State->Common.Next;

        AcpiUtDeleteGenericState (State);
    }

    /* Always must free any linked parse states */

    while (WalkState->ScopeInfo)
    {
        State = WalkState->ScopeInfo;
        WalkState->ScopeInfo = State->Common.Next;

        AcpiUtDeleteGenericState (State);
    }

    /* Always must free any stacked result states */

    while (WalkState->Results)
    {
        State = WalkState->Results;
        WalkState->Results = State->Common.Next;

        AcpiUtDeleteGenericState (State);
    }

    ACPI_FREE (WalkState);
    return_VOID;
}
