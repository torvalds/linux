/******************************************************************************
 *
 * Module Name: evgpeutil - GPE utilities
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

#define _COMPONENT          ACPI_EVENTS
        ACPI_MODULE_NAME    ("evgpeutil")


#if (!ACPI_REDUCED_HARDWARE) /* Entire module */
/*******************************************************************************
 *
 * FUNCTION:    AcpiEvWalkGpeList
 *
 * PARAMETERS:  GpeWalkCallback     - Routine called for each GPE block
 *              Context             - Value passed to callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the GPE lists.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvWalkGpeList (
    ACPI_GPE_CALLBACK       GpeWalkCallback,
    void                    *Context)
{
    ACPI_GPE_BLOCK_INFO     *GpeBlock;
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo;
    ACPI_STATUS             Status = AE_OK;
    ACPI_CPU_FLAGS          Flags;


    ACPI_FUNCTION_TRACE (EvWalkGpeList);


    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);

    /* Walk the interrupt level descriptor list */

    GpeXruptInfo = AcpiGbl_GpeXruptListHead;
    while (GpeXruptInfo)
    {
        /* Walk all Gpe Blocks attached to this interrupt level */

        GpeBlock = GpeXruptInfo->GpeBlockListHead;
        while (GpeBlock)
        {
            /* One callback per GPE block */

            Status = GpeWalkCallback (GpeXruptInfo, GpeBlock, Context);
            if (ACPI_FAILURE (Status))
            {
                if (Status == AE_CTRL_END) /* Callback abort */
                {
                    Status = AE_OK;
                }
                goto UnlockAndExit;
            }

            GpeBlock = GpeBlock->Next;
        }

        GpeXruptInfo = GpeXruptInfo->Next;
    }

UnlockAndExit:
    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvGetGpeDevice
 *
 * PARAMETERS:  GPE_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Matches the input GPE index (0-CurrentGpeCount) with a GPE
 *              block device. NULL if the GPE is one of the FADT-defined GPEs.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvGetGpeDevice (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context)
{
    ACPI_GPE_DEVICE_INFO    *Info = Context;


    /* Increment Index by the number of GPEs in this block */

    Info->NextBlockBaseIndex += GpeBlock->GpeCount;

    if (Info->Index < Info->NextBlockBaseIndex)
    {
        /*
         * The GPE index is within this block, get the node. Leave the node
         * NULL for the FADT-defined GPEs
         */
        if ((GpeBlock->Node)->Type == ACPI_TYPE_DEVICE)
        {
            Info->GpeDevice = GpeBlock->Node;
        }

        Info->Status = AE_OK;
        return (AE_CTRL_END);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvGetGpeXruptBlock
 *
 * PARAMETERS:  InterruptNumber             - Interrupt for a GPE block
 *              GpeXruptBlock               - Where the block is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get or Create a GPE interrupt block. There is one interrupt
 *              block per unique interrupt level used for GPEs. Should be
 *              called only when the GPE lists are semaphore locked and not
 *              subject to change.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvGetGpeXruptBlock (
    UINT32                  InterruptNumber,
    ACPI_GPE_XRUPT_INFO     **GpeXruptBlock)
{
    ACPI_GPE_XRUPT_INFO     *NextGpeXrupt;
    ACPI_GPE_XRUPT_INFO     *GpeXrupt;
    ACPI_STATUS             Status;
    ACPI_CPU_FLAGS          Flags;


    ACPI_FUNCTION_TRACE (EvGetGpeXruptBlock);


    /* No need for lock since we are not changing any list elements here */

    NextGpeXrupt = AcpiGbl_GpeXruptListHead;
    while (NextGpeXrupt)
    {
        if (NextGpeXrupt->InterruptNumber == InterruptNumber)
        {
            *GpeXruptBlock = NextGpeXrupt;
            return_ACPI_STATUS (AE_OK);
        }

        NextGpeXrupt = NextGpeXrupt->Next;
    }

    /* Not found, must allocate a new xrupt descriptor */

    GpeXrupt = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_GPE_XRUPT_INFO));
    if (!GpeXrupt)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    GpeXrupt->InterruptNumber = InterruptNumber;

    /* Install new interrupt descriptor with spin lock */

    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
    if (AcpiGbl_GpeXruptListHead)
    {
        NextGpeXrupt = AcpiGbl_GpeXruptListHead;
        while (NextGpeXrupt->Next)
        {
            NextGpeXrupt = NextGpeXrupt->Next;
        }

        NextGpeXrupt->Next = GpeXrupt;
        GpeXrupt->Previous = NextGpeXrupt;
    }
    else
    {
        AcpiGbl_GpeXruptListHead = GpeXrupt;
    }

    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);

    /* Install new interrupt handler if not SCI_INT */

    if (InterruptNumber != AcpiGbl_FADT.SciInterrupt)
    {
        Status = AcpiOsInstallInterruptHandler (InterruptNumber,
            AcpiEvGpeXruptHandler, GpeXrupt);
        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status,
                "Could not install GPE interrupt handler at level 0x%X",
                InterruptNumber));
            return_ACPI_STATUS (Status);
        }
    }

    *GpeXruptBlock = GpeXrupt;
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvDeleteGpeXrupt
 *
 * PARAMETERS:  GpeXrupt        - A GPE interrupt info block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove and free a GpeXrupt block. Remove an associated
 *              interrupt handler if not the SCI interrupt.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvDeleteGpeXrupt (
    ACPI_GPE_XRUPT_INFO     *GpeXrupt)
{
    ACPI_STATUS             Status;
    ACPI_CPU_FLAGS          Flags;


    ACPI_FUNCTION_TRACE (EvDeleteGpeXrupt);


    /* We never want to remove the SCI interrupt handler */

    if (GpeXrupt->InterruptNumber == AcpiGbl_FADT.SciInterrupt)
    {
        GpeXrupt->GpeBlockListHead = NULL;
        return_ACPI_STATUS (AE_OK);
    }

    /* Disable this interrupt */

    Status = AcpiOsRemoveInterruptHandler (
        GpeXrupt->InterruptNumber, AcpiEvGpeXruptHandler);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Unlink the interrupt block with lock */

    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
    if (GpeXrupt->Previous)
    {
        GpeXrupt->Previous->Next = GpeXrupt->Next;
    }
    else
    {
        /* No previous, update list head */

        AcpiGbl_GpeXruptListHead = GpeXrupt->Next;
    }

    if (GpeXrupt->Next)
    {
        GpeXrupt->Next->Previous = GpeXrupt->Previous;
    }
    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);

    /* Free the block */

    ACPI_FREE (GpeXrupt);
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvDeleteGpeHandlers
 *
 * PARAMETERS:  GpeXruptInfo        - GPE Interrupt info
 *              GpeBlock            - Gpe Block info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete all Handler objects found in the GPE data structs.
 *              Used only prior to termination.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvDeleteGpeHandlers (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context)
{
    ACPI_GPE_EVENT_INFO     *GpeEventInfo;
    ACPI_GPE_NOTIFY_INFO    *Notify;
    ACPI_GPE_NOTIFY_INFO    *Next;
    UINT32                  i;
    UINT32                  j;


    ACPI_FUNCTION_TRACE (EvDeleteGpeHandlers);


    /* Examine each GPE Register within the block */

    for (i = 0; i < GpeBlock->RegisterCount; i++)
    {
        /* Now look at the individual GPEs in this byte register */

        for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++)
        {
            GpeEventInfo = &GpeBlock->EventInfo[((ACPI_SIZE) i *
                ACPI_GPE_REGISTER_WIDTH) + j];

            if ((ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags) ==
                    ACPI_GPE_DISPATCH_HANDLER) ||
                (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags) ==
                    ACPI_GPE_DISPATCH_RAW_HANDLER))
            {
                /* Delete an installed handler block */

                ACPI_FREE (GpeEventInfo->Dispatch.Handler);
                GpeEventInfo->Dispatch.Handler = NULL;
                GpeEventInfo->Flags &= ~ACPI_GPE_DISPATCH_MASK;
            }
            else if (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags) ==
                ACPI_GPE_DISPATCH_NOTIFY)
            {
                /* Delete the implicit notification device list */

                Notify = GpeEventInfo->Dispatch.NotifyList;
                while (Notify)
                {
                    Next = Notify->Next;
                    ACPI_FREE (Notify);
                    Notify = Next;
                }

                GpeEventInfo->Dispatch.NotifyList = NULL;
                GpeEventInfo->Flags &= ~ACPI_GPE_DISPATCH_MASK;
            }
        }
    }

    return_ACPI_STATUS (AE_OK);
}

#endif /* !ACPI_REDUCED_HARDWARE */
