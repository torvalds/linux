/******************************************************************************
 *
 * Module Name: exoparg2 - AML execution - opcodes with 2 arguments
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
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acevents.h>
#include <contrib/dev/acpica/include/amlcode.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exoparg2")


/*!
 * Naming convention for AML interpreter execution routines.
 *
 * The routines that begin execution of AML opcodes are named with a common
 * convention based upon the number of arguments, the number of target operands,
 * and whether or not a value is returned:
 *
 *      AcpiExOpcode_xA_yT_zR
 *
 * Where:
 *
 * xA - ARGUMENTS:    The number of arguments (input operands) that are
 *                    required for this opcode type (1 through 6 args).
 * yT - TARGETS:      The number of targets (output operands) that are required
 *                    for this opcode type (0, 1, or 2 targets).
 * zR - RETURN VALUE: Indicates whether this opcode type returns a value
 *                    as the function return (0 or 1).
 *
 * The AcpiExOpcode* functions are called via the Dispatcher component with
 * fully resolved operands.
!*/


/*******************************************************************************
 *
 * FUNCTION:    AcpiExOpcode_2A_0T_0R
 *
 * PARAMETERS:  WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with two arguments, no target, and no return
 *              value.
 *
 * ALLOCATION:  Deletes both operands
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExOpcode_2A_0T_0R (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     **Operand = &WalkState->Operands[0];
    ACPI_NAMESPACE_NODE     *Node;
    UINT32                  Value;
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE_STR (ExOpcode_2A_0T_0R,
            AcpiPsGetOpcodeName (WalkState->Opcode));


    /* Examine the opcode */

    switch (WalkState->Opcode)
    {
    case AML_NOTIFY_OP:         /* Notify (NotifyObject, NotifyValue) */

        /* The first operand is a namespace node */

        Node = (ACPI_NAMESPACE_NODE *) Operand[0];

        /* Second value is the notify value */

        Value = (UINT32) Operand[1]->Integer.Value;

        /* Are notifies allowed on this object? */

        if (!AcpiEvIsNotifyObject (Node))
        {
            ACPI_ERROR ((AE_INFO,
                "Unexpected notify object type [%s]",
                AcpiUtGetTypeName (Node->Type)));

            Status = AE_AML_OPERAND_TYPE;
            break;
        }

        /*
         * Dispatch the notify to the appropriate handler
         * NOTE: the request is queued for execution after this method
         * completes. The notify handlers are NOT invoked synchronously
         * from this thread -- because handlers may in turn run other
         * control methods.
         */
        Status = AcpiEvQueueNotifyRequest (Node, Value);
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Unknown AML opcode 0x%X",
            WalkState->Opcode));
        Status = AE_AML_BAD_OPCODE;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExOpcode_2A_2T_1R
 *
 * PARAMETERS:  WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a dyadic operator (2 operands) with 2 output targets
 *              and one implicit return value.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExOpcode_2A_2T_1R (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     **Operand = &WalkState->Operands[0];
    ACPI_OPERAND_OBJECT     *ReturnDesc1 = NULL;
    ACPI_OPERAND_OBJECT     *ReturnDesc2 = NULL;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE_STR (ExOpcode_2A_2T_1R,
        AcpiPsGetOpcodeName (WalkState->Opcode));


    /* Execute the opcode */

    switch (WalkState->Opcode)
    {
    case AML_DIVIDE_OP:

        /* Divide (Dividend, Divisor, RemainderResult QuotientResult) */

        ReturnDesc1 = AcpiUtCreateInternalObject (ACPI_TYPE_INTEGER);
        if (!ReturnDesc1)
        {
            Status = AE_NO_MEMORY;
            goto Cleanup;
        }

        ReturnDesc2 = AcpiUtCreateInternalObject (ACPI_TYPE_INTEGER);
        if (!ReturnDesc2)
        {
            Status = AE_NO_MEMORY;
            goto Cleanup;
        }

        /* Quotient to ReturnDesc1, remainder to ReturnDesc2 */

        Status = AcpiUtDivide (
            Operand[0]->Integer.Value,
            Operand[1]->Integer.Value,
            &ReturnDesc1->Integer.Value,
            &ReturnDesc2->Integer.Value);
        if (ACPI_FAILURE (Status))
        {
            goto Cleanup;
        }
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Unknown AML opcode 0x%X",
            WalkState->Opcode));

        Status = AE_AML_BAD_OPCODE;
        goto Cleanup;
    }

    /* Store the results to the target reference operands */

    Status = AcpiExStore (ReturnDesc2, Operand[2], WalkState);
    if (ACPI_FAILURE (Status))
    {
        goto Cleanup;
    }

    Status = AcpiExStore (ReturnDesc1, Operand[3], WalkState);
    if (ACPI_FAILURE (Status))
    {
        goto Cleanup;
    }

Cleanup:
    /*
     * Since the remainder is not returned indirectly, remove a reference to
     * it. Only the quotient is returned indirectly.
     */
    AcpiUtRemoveReference (ReturnDesc2);

    if (ACPI_FAILURE (Status))
    {
        /* Delete the return object */

        AcpiUtRemoveReference (ReturnDesc1);
    }

    /* Save return object (the remainder) on success */

    else
    {
        WalkState->ResultObj = ReturnDesc1;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExOpcode_2A_1T_1R
 *
 * PARAMETERS:  WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with two arguments, one target, and a return
 *              value.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExOpcode_2A_1T_1R (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     **Operand = &WalkState->Operands[0];
    ACPI_OPERAND_OBJECT     *ReturnDesc = NULL;
    UINT64                  Index;
    ACPI_STATUS             Status = AE_OK;
    ACPI_SIZE               Length = 0;


    ACPI_FUNCTION_TRACE_STR (ExOpcode_2A_1T_1R,
        AcpiPsGetOpcodeName (WalkState->Opcode));


    /* Execute the opcode */

    if (WalkState->OpInfo->Flags & AML_MATH)
    {
        /* All simple math opcodes (add, etc.) */

        ReturnDesc = AcpiUtCreateInternalObject (ACPI_TYPE_INTEGER);
        if (!ReturnDesc)
        {
            Status = AE_NO_MEMORY;
            goto Cleanup;
        }

        ReturnDesc->Integer.Value = AcpiExDoMathOp (
            WalkState->Opcode,
            Operand[0]->Integer.Value,
            Operand[1]->Integer.Value);
        goto StoreResultToTarget;
    }

    switch (WalkState->Opcode)
    {
    case AML_MOD_OP: /* Mod (Dividend, Divisor, RemainderResult (ACPI 2.0) */

        ReturnDesc = AcpiUtCreateInternalObject (ACPI_TYPE_INTEGER);
        if (!ReturnDesc)
        {
            Status = AE_NO_MEMORY;
            goto Cleanup;
        }

        /* ReturnDesc will contain the remainder */

        Status = AcpiUtDivide (
            Operand[0]->Integer.Value,
            Operand[1]->Integer.Value,
            NULL,
            &ReturnDesc->Integer.Value);
        break;

    case AML_CONCATENATE_OP: /* Concatenate (Data1, Data2, Result) */

        Status = AcpiExDoConcatenate (
            Operand[0], Operand[1], &ReturnDesc, WalkState);
        break;

    case AML_TO_STRING_OP: /* ToString (Buffer, Length, Result) (ACPI 2.0) */
        /*
         * Input object is guaranteed to be a buffer at this point (it may have
         * been converted.)  Copy the raw buffer data to a new object of
         * type String.
         */

        /*
         * Get the length of the new string. It is the smallest of:
         * 1) Length of the input buffer
         * 2) Max length as specified in the ToString operator
         * 3) Length of input buffer up to a zero byte (null terminator)
         *
         * NOTE: A length of zero is ok, and will create a zero-length, null
         *       terminated string.
         */
        while ((Length < Operand[0]->Buffer.Length) &&  /* Length of input buffer */
               (Length < Operand[1]->Integer.Value) &&  /* Length operand */
               (Operand[0]->Buffer.Pointer[Length]))    /* Null terminator */
        {
            Length++;
        }

        /* Allocate a new string object */

        ReturnDesc = AcpiUtCreateStringObject (Length);
        if (!ReturnDesc)
        {
            Status = AE_NO_MEMORY;
            goto Cleanup;
        }

        /*
         * Copy the raw buffer data with no transform.
         * (NULL terminated already)
         */
        memcpy (ReturnDesc->String.Pointer,
            Operand[0]->Buffer.Pointer, Length);
        break;

    case AML_CONCATENATE_TEMPLATE_OP:

        /* ConcatenateResTemplate (Buffer, Buffer, Result) (ACPI 2.0) */

        Status = AcpiExConcatTemplate (
            Operand[0], Operand[1], &ReturnDesc, WalkState);
        break;

    case AML_INDEX_OP:              /* Index (Source Index Result) */

        /* Create the internal return object */

        ReturnDesc = AcpiUtCreateInternalObject (ACPI_TYPE_LOCAL_REFERENCE);
        if (!ReturnDesc)
        {
            Status = AE_NO_MEMORY;
            goto Cleanup;
        }

        /* Initialize the Index reference object */

        Index = Operand[1]->Integer.Value;
        ReturnDesc->Reference.Value = (UINT32) Index;
        ReturnDesc->Reference.Class = ACPI_REFCLASS_INDEX;

        /*
         * At this point, the Source operand is a String, Buffer, or Package.
         * Verify that the index is within range.
         */
        switch ((Operand[0])->Common.Type)
        {
        case ACPI_TYPE_STRING:

            if (Index >= Operand[0]->String.Length)
            {
                Length = Operand[0]->String.Length;
                Status = AE_AML_STRING_LIMIT;
            }

            ReturnDesc->Reference.TargetType = ACPI_TYPE_BUFFER_FIELD;
            ReturnDesc->Reference.IndexPointer =
                &(Operand[0]->Buffer.Pointer [Index]);
            break;

        case ACPI_TYPE_BUFFER:

            if (Index >= Operand[0]->Buffer.Length)
            {
                Length = Operand[0]->Buffer.Length;
                Status = AE_AML_BUFFER_LIMIT;
            }

            ReturnDesc->Reference.TargetType = ACPI_TYPE_BUFFER_FIELD;
            ReturnDesc->Reference.IndexPointer =
                &(Operand[0]->Buffer.Pointer [Index]);
            break;

        case ACPI_TYPE_PACKAGE:

            if (Index >= Operand[0]->Package.Count)
            {
                Length = Operand[0]->Package.Count;
                Status = AE_AML_PACKAGE_LIMIT;
            }

            ReturnDesc->Reference.TargetType = ACPI_TYPE_PACKAGE;
            ReturnDesc->Reference.Where =
                &Operand[0]->Package.Elements [Index];
            break;

        default:

            ACPI_ERROR ((AE_INFO,
                "Invalid object type: %X", (Operand[0])->Common.Type));
            Status = AE_AML_INTERNAL;
            goto Cleanup;
        }

        /* Failure means that the Index was beyond the end of the object */

        if (ACPI_FAILURE (Status))
        {
            ACPI_BIOS_EXCEPTION ((AE_INFO, Status,
                "Index (0x%X%8.8X) is beyond end of object (length 0x%X)",
                ACPI_FORMAT_UINT64 (Index), (UINT32) Length));
            goto Cleanup;
        }

        /*
         * Save the target object and add a reference to it for the life
         * of the index
         */
        ReturnDesc->Reference.Object = Operand[0];
        AcpiUtAddReference (Operand[0]);

        /* Store the reference to the Target */

        Status = AcpiExStore (ReturnDesc, Operand[2], WalkState);

        /* Return the reference */

        WalkState->ResultObj = ReturnDesc;
        goto Cleanup;

    default:

        ACPI_ERROR ((AE_INFO, "Unknown AML opcode 0x%X",
            WalkState->Opcode));
        Status = AE_AML_BAD_OPCODE;
        break;
    }


StoreResultToTarget:

    if (ACPI_SUCCESS (Status))
    {
        /*
         * Store the result of the operation (which is now in ReturnDesc) into
         * the Target descriptor.
         */
        Status = AcpiExStore (ReturnDesc, Operand[2], WalkState);
        if (ACPI_FAILURE (Status))
        {
            goto Cleanup;
        }

        if (!WalkState->ResultObj)
        {
            WalkState->ResultObj = ReturnDesc;
        }
    }


Cleanup:

    /* Delete return object on error */

    if (ACPI_FAILURE (Status))
    {
        AcpiUtRemoveReference (ReturnDesc);
        WalkState->ResultObj = NULL;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExOpcode_2A_0T_1R
 *
 * PARAMETERS:  WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with 2 arguments, no target, and a return value
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExOpcode_2A_0T_1R (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     **Operand = &WalkState->Operands[0];
    ACPI_OPERAND_OBJECT     *ReturnDesc = NULL;
    ACPI_STATUS             Status = AE_OK;
    BOOLEAN                 LogicalResult = FALSE;


    ACPI_FUNCTION_TRACE_STR (ExOpcode_2A_0T_1R,
        AcpiPsGetOpcodeName (WalkState->Opcode));


    /* Create the internal return object */

    ReturnDesc = AcpiUtCreateInternalObject (ACPI_TYPE_INTEGER);
    if (!ReturnDesc)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /* Execute the Opcode */

    if (WalkState->OpInfo->Flags & AML_LOGICAL_NUMERIC)
    {
        /* LogicalOp  (Operand0, Operand1) */

        Status = AcpiExDoLogicalNumericOp (WalkState->Opcode,
            Operand[0]->Integer.Value, Operand[1]->Integer.Value,
            &LogicalResult);
        goto StoreLogicalResult;
    }
    else if (WalkState->OpInfo->Flags & AML_LOGICAL)
    {
        /* LogicalOp  (Operand0, Operand1) */

        Status = AcpiExDoLogicalOp (WalkState->Opcode, Operand[0],
            Operand[1], &LogicalResult);
        goto StoreLogicalResult;
    }

    switch (WalkState->Opcode)
    {
    case AML_ACQUIRE_OP:            /* Acquire (MutexObject, Timeout) */

        Status = AcpiExAcquireMutex (Operand[1], Operand[0], WalkState);
        if (Status == AE_TIME)
        {
            LogicalResult = TRUE;       /* TRUE = Acquire timed out */
            Status = AE_OK;
        }
        break;


    case AML_WAIT_OP:               /* Wait (EventObject, Timeout) */

        Status = AcpiExSystemWaitEvent (Operand[1], Operand[0]);
        if (Status == AE_TIME)
        {
            LogicalResult = TRUE;       /* TRUE, Wait timed out */
            Status = AE_OK;
        }
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Unknown AML opcode 0x%X",
            WalkState->Opcode));

        Status = AE_AML_BAD_OPCODE;
        goto Cleanup;
    }


StoreLogicalResult:
    /*
     * Set return value to according to LogicalResult. logical TRUE (all ones)
     * Default is FALSE (zero)
     */
    if (LogicalResult)
    {
        ReturnDesc->Integer.Value = ACPI_UINT64_MAX;
    }

Cleanup:

    /* Delete return object on error */

    if (ACPI_FAILURE (Status))
    {
        AcpiUtRemoveReference (ReturnDesc);
    }

    /* Save return object on success */

    else
    {
        WalkState->ResultObj = ReturnDesc;
    }

    return_ACPI_STATUS (Status);
}
