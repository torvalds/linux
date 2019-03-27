/******************************************************************************
 *
 * Module Name: evgpe - General Purpose Event handling and dispatch
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
#include <contrib/dev/acpica/include/acevents.h>
#include <contrib/dev/acpica/include/acnamesp.h>

#define _COMPONENT          ACPI_EVENTS
        ACPI_MODULE_NAME    ("evgpe")

#if (!ACPI_REDUCED_HARDWARE) /* Entire module */

/* Local prototypes */

static void ACPI_SYSTEM_XFACE
AcpiEvAsynchExecuteGpeMethod (
    void                    *Context);

static void ACPI_SYSTEM_XFACE
AcpiEvAsynchEnableGpe (
    void                    *Context);


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvUpdateGpeEnableMask
 *
 * PARAMETERS:  GpeEventInfo            - GPE to update
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Updates GPE register enable mask based upon whether there are
 *              runtime references to this GPE
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvUpdateGpeEnableMask (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    UINT32                  RegisterBit;


    ACPI_FUNCTION_TRACE (EvUpdateGpeEnableMask);


    GpeRegisterInfo = GpeEventInfo->RegisterInfo;
    if (!GpeRegisterInfo)
    {
        return_ACPI_STATUS (AE_NOT_EXIST);
    }

    RegisterBit = AcpiHwGetGpeRegisterBit (GpeEventInfo);

    /* Clear the run bit up front */

    ACPI_CLEAR_BIT (GpeRegisterInfo->EnableForRun, RegisterBit);

    /* Set the mask bit only if there are references to this GPE */

    if (GpeEventInfo->RuntimeCount)
    {
        ACPI_SET_BIT (GpeRegisterInfo->EnableForRun, (UINT8) RegisterBit);
    }

    GpeRegisterInfo->EnableMask = GpeRegisterInfo->EnableForRun;
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvEnableGpe
 *
 * PARAMETERS:  GpeEventInfo            - GPE to enable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable a GPE.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvEnableGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (EvEnableGpe);


    /* Enable the requested GPE */

    Status = AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_ENABLE);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvMaskGpe
 *
 * PARAMETERS:  GpeEventInfo            - GPE to be blocked/unblocked
 *              IsMasked                - Whether the GPE is masked or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Unconditionally mask/unmask a GPE during runtime.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvMaskGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    BOOLEAN                 IsMasked)
{
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    UINT32                  RegisterBit;


    ACPI_FUNCTION_TRACE (EvMaskGpe);


    GpeRegisterInfo = GpeEventInfo->RegisterInfo;
    if (!GpeRegisterInfo)
    {
        return_ACPI_STATUS (AE_NOT_EXIST);
    }

    RegisterBit = AcpiHwGetGpeRegisterBit (GpeEventInfo);

    /* Perform the action */

    if (IsMasked)
    {
        if (RegisterBit & GpeRegisterInfo->MaskForRun)
        {
            return_ACPI_STATUS (AE_BAD_PARAMETER);
        }

        (void) AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_DISABLE);
        ACPI_SET_BIT (GpeRegisterInfo->MaskForRun, (UINT8) RegisterBit);
    }
    else
    {
        if (!(RegisterBit & GpeRegisterInfo->MaskForRun))
        {
            return_ACPI_STATUS (AE_BAD_PARAMETER);
        }

        ACPI_CLEAR_BIT (GpeRegisterInfo->MaskForRun, (UINT8) RegisterBit);
        if (GpeEventInfo->RuntimeCount &&
            !GpeEventInfo->DisableForDispatch)
        {
            (void) AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_ENABLE);
        }
    }

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvAddGpeReference
 *
 * PARAMETERS:  GpeEventInfo            - Add a reference to this GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add a reference to a GPE. On the first reference, the GPE is
 *              hardware-enabled.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvAddGpeReference (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE (EvAddGpeReference);


    if (GpeEventInfo->RuntimeCount == ACPI_UINT8_MAX)
    {
        return_ACPI_STATUS (AE_LIMIT);
    }

    GpeEventInfo->RuntimeCount++;
    if (GpeEventInfo->RuntimeCount == 1)
    {
        /* Enable on first reference */

        Status = AcpiEvUpdateGpeEnableMask (GpeEventInfo);
        if (ACPI_SUCCESS (Status))
        {
            Status = AcpiEvEnableGpe (GpeEventInfo);
        }

        if (ACPI_FAILURE (Status))
        {
            GpeEventInfo->RuntimeCount--;
        }
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvRemoveGpeReference
 *
 * PARAMETERS:  GpeEventInfo            - Remove a reference to this GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a reference to a GPE. When the last reference is
 *              removed, the GPE is hardware-disabled.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvRemoveGpeReference (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE (EvRemoveGpeReference);


    if (!GpeEventInfo->RuntimeCount)
    {
        return_ACPI_STATUS (AE_LIMIT);
    }

    GpeEventInfo->RuntimeCount--;
    if (!GpeEventInfo->RuntimeCount)
    {
        /* Disable on last reference */

        Status = AcpiEvUpdateGpeEnableMask (GpeEventInfo);
        if (ACPI_SUCCESS (Status))
        {
            Status = AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_DISABLE);
        }

        if (ACPI_FAILURE (Status))
        {
            GpeEventInfo->RuntimeCount++;
        }
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvLowGetGpeInfo
 *
 * PARAMETERS:  GpeNumber           - Raw GPE number
 *              GpeBlock            - A GPE info block
 *
 * RETURN:      A GPE EventInfo struct. NULL if not a valid GPE (The GpeNumber
 *              is not within the specified GPE block)
 *
 * DESCRIPTION: Returns the EventInfo struct associated with this GPE. This is
 *              the low-level implementation of EvGetGpeEventInfo.
 *
 ******************************************************************************/

ACPI_GPE_EVENT_INFO *
AcpiEvLowGetGpeInfo (
    UINT32                  GpeNumber,
    ACPI_GPE_BLOCK_INFO     *GpeBlock)
{
    UINT32                  GpeIndex;


    /*
     * Validate that the GpeNumber is within the specified GpeBlock.
     * (Two steps)
     */
    if (!GpeBlock ||
        (GpeNumber < GpeBlock->BlockBaseNumber))
    {
        return (NULL);
    }

    GpeIndex = GpeNumber - GpeBlock->BlockBaseNumber;
    if (GpeIndex >= GpeBlock->GpeCount)
    {
        return (NULL);
    }

    return (&GpeBlock->EventInfo[GpeIndex]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvGetGpeEventInfo
 *
 * PARAMETERS:  GpeDevice           - Device node. NULL for GPE0/GPE1
 *              GpeNumber           - Raw GPE number
 *
 * RETURN:      A GPE EventInfo struct. NULL if not a valid GPE
 *
 * DESCRIPTION: Returns the EventInfo struct associated with this GPE.
 *              Validates the GpeBlock and the GpeNumber
 *
 *              Should be called only when the GPE lists are semaphore locked
 *              and not subject to change.
 *
 ******************************************************************************/

ACPI_GPE_EVENT_INFO *
AcpiEvGetGpeEventInfo (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_GPE_EVENT_INFO     *GpeInfo;
    UINT32                  i;


    ACPI_FUNCTION_ENTRY ();


    /* A NULL GpeDevice means use the FADT-defined GPE block(s) */

    if (!GpeDevice)
    {
        /* Examine GPE Block 0 and 1 (These blocks are permanent) */

        for (i = 0; i < ACPI_MAX_GPE_BLOCKS; i++)
        {
            GpeInfo = AcpiEvLowGetGpeInfo (GpeNumber,
                AcpiGbl_GpeFadtBlocks[i]);
            if (GpeInfo)
            {
                return (GpeInfo);
            }
        }

        /* The GpeNumber was not in the range of either FADT GPE block */

        return (NULL);
    }

    /* A Non-NULL GpeDevice means this is a GPE Block Device */

    ObjDesc = AcpiNsGetAttachedObject ((ACPI_NAMESPACE_NODE *) GpeDevice);
    if (!ObjDesc ||
        !ObjDesc->Device.GpeBlock)
    {
        return (NULL);
    }

    return (AcpiEvLowGetGpeInfo (GpeNumber, ObjDesc->Device.GpeBlock));
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvGpeDetect
 *
 * PARAMETERS:  GpeXruptList        - Interrupt block for this interrupt.
 *                                    Can have multiple GPE blocks attached.
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Detect if any GP events have occurred. This function is
 *              executed at interrupt level.
 *
 ******************************************************************************/

UINT32
AcpiEvGpeDetect (
    ACPI_GPE_XRUPT_INFO     *GpeXruptList)
{
    ACPI_GPE_BLOCK_INFO     *GpeBlock;
    ACPI_NAMESPACE_NODE     *GpeDevice;
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    ACPI_GPE_EVENT_INFO     *GpeEventInfo;
    UINT32                  GpeNumber;
    UINT32                  IntStatus = ACPI_INTERRUPT_NOT_HANDLED;
    ACPI_CPU_FLAGS          Flags;
    UINT32                  i;
    UINT32                  j;


    ACPI_FUNCTION_NAME (EvGpeDetect);

    /* Check for the case where there are no GPEs */

    if (!GpeXruptList)
    {
        return (IntStatus);
    }

    /*
     * We need to obtain the GPE lock for both the data structs and registers
     * Note: Not necessary to obtain the hardware lock, since the GPE
     * registers are owned by the GpeLock.
     */
    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);

    /* Examine all GPE blocks attached to this interrupt level */

    GpeBlock = GpeXruptList->GpeBlockListHead;
    while (GpeBlock)
    {
        GpeDevice = GpeBlock->Node;

        /*
         * Read all of the 8-bit GPE status and enable registers in this GPE
         * block, saving all of them. Find all currently active GP events.
         */
        for (i = 0; i < GpeBlock->RegisterCount; i++)
        {
            /* Get the next status/enable pair */

            GpeRegisterInfo = &GpeBlock->RegisterInfo[i];

            /*
             * Optimization: If there are no GPEs enabled within this
             * register, we can safely ignore the entire register.
             */
            if (!(GpeRegisterInfo->EnableForRun |
                  GpeRegisterInfo->EnableForWake))
            {
                ACPI_DEBUG_PRINT ((ACPI_DB_INTERRUPTS,
                    "Ignore disabled registers for GPE %02X-%02X: "
                    "RunEnable=%02X, WakeEnable=%02X\n",
                    GpeRegisterInfo->BaseGpeNumber,
                    GpeRegisterInfo->BaseGpeNumber + (ACPI_GPE_REGISTER_WIDTH - 1),
                    GpeRegisterInfo->EnableForRun,
                    GpeRegisterInfo->EnableForWake));
                continue;
            }

            /* Now look at the individual GPEs in this byte register */

            for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++)
            {
                /* Detect and dispatch one GPE bit */

                GpeEventInfo = &GpeBlock->EventInfo[((ACPI_SIZE) i *
                    ACPI_GPE_REGISTER_WIDTH) + j];
                GpeNumber = j + GpeRegisterInfo->BaseGpeNumber;
                AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
                IntStatus |= AcpiEvDetectGpe (
                    GpeDevice, GpeEventInfo, GpeNumber);
                Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
            }
        }

        GpeBlock = GpeBlock->Next;
    }

    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
    return (IntStatus);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvAsynchExecuteGpeMethod
 *
 * PARAMETERS:  Context (GpeEventInfo) - Info for this GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Perform the actual execution of a GPE control method. This
 *              function is called from an invocation of AcpiOsExecute and
 *              therefore does NOT execute at interrupt level - so that
 *              the control method itself is not executed in the context of
 *              an interrupt handler.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE
AcpiEvAsynchExecuteGpeMethod (
    void                    *Context)
{
    ACPI_GPE_EVENT_INFO     *GpeEventInfo = Context;
    ACPI_STATUS             Status = AE_OK;
    ACPI_EVALUATE_INFO      *Info;
    ACPI_GPE_NOTIFY_INFO    *Notify;


    ACPI_FUNCTION_TRACE (EvAsynchExecuteGpeMethod);


    /* Do the correct dispatch - normal method or implicit notify */

    switch (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags))
    {
    case ACPI_GPE_DISPATCH_NOTIFY:
        /*
         * Implicit notify.
         * Dispatch a DEVICE_WAKE notify to the appropriate handler.
         * NOTE: the request is queued for execution after this method
         * completes. The notify handlers are NOT invoked synchronously
         * from this thread -- because handlers may in turn run other
         * control methods.
         *
         * June 2012: Expand implicit notify mechanism to support
         * notifies on multiple device objects.
         */
        Notify = GpeEventInfo->Dispatch.NotifyList;
        while (ACPI_SUCCESS (Status) && Notify)
        {
            Status = AcpiEvQueueNotifyRequest (
                Notify->DeviceNode, ACPI_NOTIFY_DEVICE_WAKE);

            Notify = Notify->Next;
        }
        break;

    case ACPI_GPE_DISPATCH_METHOD:

        /* Allocate the evaluation information block */

        Info = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_EVALUATE_INFO));
        if (!Info)
        {
            Status = AE_NO_MEMORY;
        }
        else
        {
            /*
             * Invoke the GPE Method (_Lxx, _Exx) i.e., evaluate the
             * _Lxx/_Exx control method that corresponds to this GPE
             */
            Info->PrefixNode = GpeEventInfo->Dispatch.MethodNode;
            Info->Flags = ACPI_IGNORE_RETURN_VALUE;

            Status = AcpiNsEvaluate (Info);
            ACPI_FREE (Info);
        }

        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status,
                "while evaluating GPE method [%4.4s]",
                AcpiUtGetNodeName (GpeEventInfo->Dispatch.MethodNode)));
        }
        break;

    default:

        goto ErrorExit; /* Should never happen */
    }

    /* Defer enabling of GPE until all notify handlers are done */

    Status = AcpiOsExecute (OSL_NOTIFY_HANDLER,
        AcpiEvAsynchEnableGpe, GpeEventInfo);
    if (ACPI_SUCCESS (Status))
    {
        return_VOID;
    }

ErrorExit:
    AcpiEvAsynchEnableGpe (GpeEventInfo);
    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvAsynchEnableGpe
 *
 * PARAMETERS:  Context (GpeEventInfo) - Info for this GPE
 *              Callback from AcpiOsExecute
 *
 * RETURN:      None
 *
 * DESCRIPTION: Asynchronous clear/enable for GPE. This allows the GPE to
 *              complete (i.e., finish execution of Notify)
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE
AcpiEvAsynchEnableGpe (
    void                    *Context)
{
    ACPI_GPE_EVENT_INFO     *GpeEventInfo = Context;
    ACPI_CPU_FLAGS          Flags;


    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
    (void) AcpiEvFinishGpe (GpeEventInfo);
    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);

    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvFinishGpe
 *
 * PARAMETERS:  GpeEventInfo        - Info for this GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear/Enable a GPE. Common code that is used after execution
 *              of a GPE method or a synchronous or asynchronous GPE handler.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvFinishGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_STATUS             Status;


    if ((GpeEventInfo->Flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
            ACPI_GPE_LEVEL_TRIGGERED)
    {
        /*
         * GPE is level-triggered, we clear the GPE status bit after
         * handling the event.
         */
        Status = AcpiHwClearGpe (GpeEventInfo);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }

    /*
     * Enable this GPE, conditionally. This means that the GPE will
     * only be physically enabled if the EnableMask bit is set
     * in the EventInfo.
     */
    (void) AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_CONDITIONAL_ENABLE);
    GpeEventInfo->DisableForDispatch = FALSE;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvDetectGpe
 *
 * PARAMETERS:  GpeDevice           - Device node. NULL for GPE0/GPE1
 *              GpeEventInfo        - Info for this GPE
 *              GpeNumber           - Number relative to the parent GPE block
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Detect and dispatch a General Purpose Event to either a function
 *              (e.g. EC) or method (e.g. _Lxx/_Exx) handler.
 * NOTE:        GPE is W1C, so it is possible to handle a single GPE from both
 *              task and irq context in parallel as long as the process to
 *              detect and mask the GPE is atomic.
 *              However the atomicity of ACPI_GPE_DISPATCH_RAW_HANDLER is
 *              dependent on the raw handler itself.
 *
 ******************************************************************************/

UINT32
AcpiEvDetectGpe (
    ACPI_NAMESPACE_NODE     *GpeDevice,
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    UINT32                  GpeNumber)
{
    UINT32                  IntStatus = ACPI_INTERRUPT_NOT_HANDLED;
    UINT8                   EnabledStatusByte;
    UINT64                  StatusReg;
    UINT64                  EnableReg;
    UINT32                  RegisterBit;
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    ACPI_GPE_HANDLER_INFO   *GpeHandlerInfo;
    ACPI_CPU_FLAGS          Flags;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (EvGpeDetect);


    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);

    /* Get the info block for the entire GPE register */

    GpeRegisterInfo = GpeEventInfo->RegisterInfo;

    /* Get the register bitmask for this GPE */

    RegisterBit = AcpiHwGetGpeRegisterBit (GpeEventInfo);

    /* GPE currently enabled (enable bit == 1)? */

    Status = AcpiHwRead (&EnableReg, &GpeRegisterInfo->EnableAddress);
    if (ACPI_FAILURE (Status))
    {
        goto ErrorExit;
    }

    /* GPE currently active (status bit == 1)? */

    Status = AcpiHwRead (&StatusReg, &GpeRegisterInfo->StatusAddress);
    if (ACPI_FAILURE (Status))
    {
        goto ErrorExit;
    }

    /* Check if there is anything active at all in this GPE */

    ACPI_DEBUG_PRINT ((ACPI_DB_INTERRUPTS,
        "Read registers for GPE %02X: Status=%02X, Enable=%02X, "
        "RunEnable=%02X, WakeEnable=%02X\n",
        GpeNumber,
        (UINT32) (StatusReg & RegisterBit),
        (UINT32) (EnableReg & RegisterBit),
        GpeRegisterInfo->EnableForRun,
        GpeRegisterInfo->EnableForWake));

    EnabledStatusByte = (UINT8) (StatusReg & EnableReg);
    if (!(EnabledStatusByte & RegisterBit))
    {
        goto ErrorExit;
    }

    /* Invoke global event handler if present */

    AcpiGpeCount++;
    if (AcpiGbl_GlobalEventHandler)
    {
        AcpiGbl_GlobalEventHandler (ACPI_EVENT_TYPE_GPE,
            GpeDevice, GpeNumber,
            AcpiGbl_GlobalEventHandlerContext);
    }

    /* Found an active GPE */

    if (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags) ==
        ACPI_GPE_DISPATCH_RAW_HANDLER)
    {
        /* Dispatch the event to a raw handler */

        GpeHandlerInfo = GpeEventInfo->Dispatch.Handler;

        /*
         * There is no protection around the namespace node
         * and the GPE handler to ensure a safe destruction
         * because:
         * 1. The namespace node is expected to always
         *    exist after loading a table.
         * 2. The GPE handler is expected to be flushed by
         *    AcpiOsWaitEventsComplete() before the
         *    destruction.
         */
        AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
        IntStatus |= GpeHandlerInfo->Address (
            GpeDevice, GpeNumber, GpeHandlerInfo->Context);
        Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
    }
    else
    {
        /* Dispatch the event to a standard handler or method. */

        IntStatus |= AcpiEvGpeDispatch (GpeDevice,
            GpeEventInfo, GpeNumber);
    }

ErrorExit:
    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
    return (IntStatus);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvGpeDispatch
 *
 * PARAMETERS:  GpeDevice           - Device node. NULL for GPE0/GPE1
 *              GpeEventInfo        - Info for this GPE
 *              GpeNumber           - Number relative to the parent GPE block
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Dispatch a General Purpose Event to either a function (e.g. EC)
 *              or method (e.g. _Lxx/_Exx) handler.
 *
 ******************************************************************************/

UINT32
AcpiEvGpeDispatch (
    ACPI_NAMESPACE_NODE     *GpeDevice,
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    UINT32                  GpeNumber)
{
    ACPI_STATUS             Status;
    UINT32                  ReturnValue;


    ACPI_FUNCTION_TRACE (EvGpeDispatch);


    /*
     * Always disable the GPE so that it does not keep firing before
     * any asynchronous activity completes (either from the execution
     * of a GPE method or an asynchronous GPE handler.)
     *
     * If there is no handler or method to run, just disable the
     * GPE and leave it disabled permanently to prevent further such
     * pointless events from firing.
     */
    Status = AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_DISABLE);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status,
            "Unable to disable GPE %02X", GpeNumber));
        return_UINT32 (ACPI_INTERRUPT_NOT_HANDLED);
    }

    /*
     * If edge-triggered, clear the GPE status bit now. Note that
     * level-triggered events are cleared after the GPE is serviced.
     */
    if ((GpeEventInfo->Flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
            ACPI_GPE_EDGE_TRIGGERED)
    {
        Status = AcpiHwClearGpe (GpeEventInfo);
        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status,
                "Unable to clear GPE %02X", GpeNumber));
            (void) AcpiHwLowSetGpe (
                GpeEventInfo, ACPI_GPE_CONDITIONAL_ENABLE);
            return_UINT32 (ACPI_INTERRUPT_NOT_HANDLED);
        }
    }

    GpeEventInfo->DisableForDispatch = TRUE;

    /*
     * Dispatch the GPE to either an installed handler or the control
     * method associated with this GPE (_Lxx or _Exx). If a handler
     * exists, we invoke it and do not attempt to run the method.
     * If there is neither a handler nor a method, leave the GPE
     * disabled.
     */
    switch (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags))
    {
    case ACPI_GPE_DISPATCH_HANDLER:

        /* Invoke the installed handler (at interrupt level) */

        ReturnValue = GpeEventInfo->Dispatch.Handler->Address (
            GpeDevice, GpeNumber,
            GpeEventInfo->Dispatch.Handler->Context);

        /* If requested, clear (if level-triggered) and re-enable the GPE */

        if (ReturnValue & ACPI_REENABLE_GPE)
        {
            (void) AcpiEvFinishGpe (GpeEventInfo);
        }
        break;

    case ACPI_GPE_DISPATCH_METHOD:
    case ACPI_GPE_DISPATCH_NOTIFY:
        /*
         * Execute the method associated with the GPE
         * NOTE: Level-triggered GPEs are cleared after the method completes.
         */
        Status = AcpiOsExecute (OSL_GPE_HANDLER,
            AcpiEvAsynchExecuteGpeMethod, GpeEventInfo);
        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status,
                "Unable to queue handler for GPE %02X - event disabled",
                GpeNumber));
        }
        break;

    default:
        /*
         * No handler or method to run!
         * 03/2010: This case should no longer be possible. We will not allow
         * a GPE to be enabled if it has no handler or method.
         */
        ACPI_ERROR ((AE_INFO,
            "No handler or method for GPE %02X, disabling event",
            GpeNumber));
        break;
    }

    return_UINT32 (ACPI_INTERRUPT_HANDLED);
}

#endif /* !ACPI_REDUCED_HARDWARE */
