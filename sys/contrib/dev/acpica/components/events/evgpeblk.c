/******************************************************************************
 *
 * Module Name: evgpeblk - GPE block creation and initialization.
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
        ACPI_MODULE_NAME    ("evgpeblk")

#if (!ACPI_REDUCED_HARDWARE) /* Entire module */

/* Local prototypes */

static ACPI_STATUS
AcpiEvInstallGpeBlock (
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    UINT32                  InterruptNumber);

static ACPI_STATUS
AcpiEvCreateGpeInfoBlocks (
    ACPI_GPE_BLOCK_INFO     *GpeBlock);


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvInstallGpeBlock
 *
 * PARAMETERS:  GpeBlock                - New GPE block
 *              InterruptNumber         - Xrupt to be associated with this
 *                                        GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install new GPE block with mutex support
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiEvInstallGpeBlock (
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    UINT32                  InterruptNumber)
{
    ACPI_GPE_BLOCK_INFO     *NextGpeBlock;
    ACPI_GPE_XRUPT_INFO     *GpeXruptBlock;
    ACPI_STATUS             Status;
    ACPI_CPU_FLAGS          Flags;


    ACPI_FUNCTION_TRACE (EvInstallGpeBlock);


    Status = AcpiUtAcquireMutex (ACPI_MTX_EVENTS);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    Status = AcpiEvGetGpeXruptBlock (InterruptNumber, &GpeXruptBlock);
    if (ACPI_FAILURE (Status))
    {
        goto UnlockAndExit;
    }

    /* Install the new block at the end of the list with lock */

    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
    if (GpeXruptBlock->GpeBlockListHead)
    {
        NextGpeBlock = GpeXruptBlock->GpeBlockListHead;
        while (NextGpeBlock->Next)
        {
            NextGpeBlock = NextGpeBlock->Next;
        }

        NextGpeBlock->Next = GpeBlock;
        GpeBlock->Previous = NextGpeBlock;
    }
    else
    {
        GpeXruptBlock->GpeBlockListHead = GpeBlock;
    }

    GpeBlock->XruptBlock = GpeXruptBlock;
    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);


UnlockAndExit:
    (void) AcpiUtReleaseMutex (ACPI_MTX_EVENTS);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvDeleteGpeBlock
 *
 * PARAMETERS:  GpeBlock            - Existing GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a GPE block
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvDeleteGpeBlock (
    ACPI_GPE_BLOCK_INFO     *GpeBlock)
{
    ACPI_STATUS             Status;
    ACPI_CPU_FLAGS          Flags;


    ACPI_FUNCTION_TRACE (EvInstallGpeBlock);


    Status = AcpiUtAcquireMutex (ACPI_MTX_EVENTS);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Disable all GPEs in this block */

    Status = AcpiHwDisableGpeBlock (GpeBlock->XruptBlock, GpeBlock, NULL);

    if (!GpeBlock->Previous && !GpeBlock->Next)
    {
        /* This is the last GpeBlock on this interrupt */

        Status = AcpiEvDeleteGpeXrupt (GpeBlock->XruptBlock);
        if (ACPI_FAILURE (Status))
        {
            goto UnlockAndExit;
        }
    }
    else
    {
        /* Remove the block on this interrupt with lock */

        Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
        if (GpeBlock->Previous)
        {
            GpeBlock->Previous->Next = GpeBlock->Next;
        }
        else
        {
            GpeBlock->XruptBlock->GpeBlockListHead = GpeBlock->Next;
        }

        if (GpeBlock->Next)
        {
            GpeBlock->Next->Previous = GpeBlock->Previous;
        }

        AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
    }

    AcpiCurrentGpeCount -= GpeBlock->GpeCount;

    /* Free the GpeBlock */

    ACPI_FREE (GpeBlock->RegisterInfo);
    ACPI_FREE (GpeBlock->EventInfo);
    ACPI_FREE (GpeBlock);

UnlockAndExit:
    Status = AcpiUtReleaseMutex (ACPI_MTX_EVENTS);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvCreateGpeInfoBlocks
 *
 * PARAMETERS:  GpeBlock    - New GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the RegisterInfo and EventInfo blocks for this GPE block
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiEvCreateGpeInfoBlocks (
    ACPI_GPE_BLOCK_INFO     *GpeBlock)
{
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo = NULL;
    ACPI_GPE_EVENT_INFO     *GpeEventInfo = NULL;
    ACPI_GPE_EVENT_INFO     *ThisEvent;
    ACPI_GPE_REGISTER_INFO  *ThisRegister;
    UINT32                  i;
    UINT32                  j;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (EvCreateGpeInfoBlocks);


    /* Allocate the GPE register information block */

    GpeRegisterInfo = ACPI_ALLOCATE_ZEROED (
        (ACPI_SIZE) GpeBlock->RegisterCount *
        sizeof (ACPI_GPE_REGISTER_INFO));
    if (!GpeRegisterInfo)
    {
        ACPI_ERROR ((AE_INFO,
            "Could not allocate the GpeRegisterInfo table"));
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /*
     * Allocate the GPE EventInfo block. There are eight distinct GPEs
     * per register. Initialization to zeros is sufficient.
     */
    GpeEventInfo = ACPI_ALLOCATE_ZEROED ((ACPI_SIZE) GpeBlock->GpeCount *
        sizeof (ACPI_GPE_EVENT_INFO));
    if (!GpeEventInfo)
    {
        ACPI_ERROR ((AE_INFO,
            "Could not allocate the GpeEventInfo table"));
        Status = AE_NO_MEMORY;
        goto ErrorExit;
    }

    /* Save the new Info arrays in the GPE block */

    GpeBlock->RegisterInfo = GpeRegisterInfo;
    GpeBlock->EventInfo = GpeEventInfo;

    /*
     * Initialize the GPE Register and Event structures. A goal of these
     * tables is to hide the fact that there are two separate GPE register
     * sets in a given GPE hardware block, the status registers occupy the
     * first half, and the enable registers occupy the second half.
     */
    ThisRegister = GpeRegisterInfo;
    ThisEvent = GpeEventInfo;

    for (i = 0; i < GpeBlock->RegisterCount; i++)
    {
        /* Init the RegisterInfo for this GPE register (8 GPEs) */

        ThisRegister->BaseGpeNumber = (UINT16)
            (GpeBlock->BlockBaseNumber + (i * ACPI_GPE_REGISTER_WIDTH));

        ThisRegister->StatusAddress.Address =
            GpeBlock->Address + i;

        ThisRegister->EnableAddress.Address =
            GpeBlock->Address + i + GpeBlock->RegisterCount;

        ThisRegister->StatusAddress.SpaceId   = GpeBlock->SpaceId;
        ThisRegister->EnableAddress.SpaceId   = GpeBlock->SpaceId;
        ThisRegister->StatusAddress.BitWidth  = ACPI_GPE_REGISTER_WIDTH;
        ThisRegister->EnableAddress.BitWidth  = ACPI_GPE_REGISTER_WIDTH;
        ThisRegister->StatusAddress.BitOffset = 0;
        ThisRegister->EnableAddress.BitOffset = 0;

        /* Init the EventInfo for each GPE within this register */

        for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++)
        {
            ThisEvent->GpeNumber = (UINT8) (ThisRegister->BaseGpeNumber + j);
            ThisEvent->RegisterInfo = ThisRegister;
            ThisEvent++;
        }

        /* Disable all GPEs within this register */

        Status = AcpiHwWrite (0x00, &ThisRegister->EnableAddress);
        if (ACPI_FAILURE (Status))
        {
            goto ErrorExit;
        }

        /* Clear any pending GPE events within this register */

        Status = AcpiHwWrite (0xFF, &ThisRegister->StatusAddress);
        if (ACPI_FAILURE (Status))
        {
            goto ErrorExit;
        }

        ThisRegister++;
    }

    return_ACPI_STATUS (AE_OK);


ErrorExit:
    if (GpeRegisterInfo)
    {
        ACPI_FREE (GpeRegisterInfo);
    }
    if (GpeEventInfo)
    {
        ACPI_FREE (GpeEventInfo);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvCreateGpeBlock
 *
 * PARAMETERS:  GpeDevice           - Handle to the parent GPE block
 *              GpeBlockAddress     - Address and SpaceID
 *              RegisterCount       - Number of GPE register pairs in the block
 *              GpeBlockBaseNumber  - Starting GPE number for the block
 *              InterruptNumber     - H/W interrupt for the block
 *              ReturnGpeBlock      - Where the new block descriptor is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create and Install a block of GPE registers. All GPEs within
 *              the block are disabled at exit.
 *              Note: Assumes namespace is locked.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvCreateGpeBlock (
    ACPI_NAMESPACE_NODE     *GpeDevice,
    UINT64                  Address,
    UINT8                   SpaceId,
    UINT32                  RegisterCount,
    UINT16                  GpeBlockBaseNumber,
    UINT32                  InterruptNumber,
    ACPI_GPE_BLOCK_INFO     **ReturnGpeBlock)
{
    ACPI_STATUS             Status;
    ACPI_GPE_BLOCK_INFO     *GpeBlock;
    ACPI_GPE_WALK_INFO      WalkInfo;


    ACPI_FUNCTION_TRACE (EvCreateGpeBlock);


    if (!RegisterCount)
    {
        return_ACPI_STATUS (AE_OK);
    }

    /* Allocate a new GPE block */

    GpeBlock = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_GPE_BLOCK_INFO));
    if (!GpeBlock)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Initialize the new GPE block */

    GpeBlock->Address = Address;
    GpeBlock->SpaceId = SpaceId;
    GpeBlock->Node = GpeDevice;
    GpeBlock->GpeCount = (UINT16) (RegisterCount * ACPI_GPE_REGISTER_WIDTH);
    GpeBlock->Initialized = FALSE;
    GpeBlock->RegisterCount = RegisterCount;
    GpeBlock->BlockBaseNumber = GpeBlockBaseNumber;

    /*
     * Create the RegisterInfo and EventInfo sub-structures
     * Note: disables and clears all GPEs in the block
     */
    Status = AcpiEvCreateGpeInfoBlocks (GpeBlock);
    if (ACPI_FAILURE (Status))
    {
        ACPI_FREE (GpeBlock);
        return_ACPI_STATUS (Status);
    }

    /* Install the new block in the global lists */

    Status = AcpiEvInstallGpeBlock (GpeBlock, InterruptNumber);
    if (ACPI_FAILURE (Status))
    {
        ACPI_FREE (GpeBlock->RegisterInfo);
        ACPI_FREE (GpeBlock->EventInfo);
        ACPI_FREE (GpeBlock);
        return_ACPI_STATUS (Status);
    }

    AcpiGbl_AllGpesInitialized = FALSE;

    /* Find all GPE methods (_Lxx or_Exx) for this block */

    WalkInfo.GpeBlock = GpeBlock;
    WalkInfo.GpeDevice = GpeDevice;
    WalkInfo.ExecuteByOwnerId = FALSE;

    Status = AcpiNsWalkNamespace (ACPI_TYPE_METHOD, GpeDevice,
        ACPI_UINT32_MAX, ACPI_NS_WALK_NO_UNLOCK,
        AcpiEvMatchGpeMethod, NULL, &WalkInfo, NULL);

    /* Return the new block */

    if (ReturnGpeBlock)
    {
        (*ReturnGpeBlock) = GpeBlock;
    }

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
        "    Initialized GPE %02X to %02X [%4.4s] %u regs on interrupt 0x%X%s\n",
        (UINT32) GpeBlock->BlockBaseNumber,
        (UINT32) (GpeBlock->BlockBaseNumber + (GpeBlock->GpeCount - 1)),
        GpeDevice->Name.Ascii, GpeBlock->RegisterCount, InterruptNumber,
        InterruptNumber == AcpiGbl_FADT.SciInterrupt ? " (SCI)" : ""));

    /* Update global count of currently available GPEs */

    AcpiCurrentGpeCount += GpeBlock->GpeCount;
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvInitializeGpeBlock
 *
 * PARAMETERS:  ACPI_GPE_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize and enable a GPE block. Enable GPEs that have
 *              associated methods.
 *              Note: Assumes namespace is locked.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvInitializeGpeBlock (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context)
{
    ACPI_STATUS             Status;
    ACPI_GPE_EVENT_INFO     *GpeEventInfo;
    UINT32                  GpeEnabledCount;
    UINT32                  GpeIndex;
    UINT32                  i;
    UINT32                  j;
    BOOLEAN                 *IsPollingNeeded = Context;
    ACPI_ERROR_ONLY (UINT32 GpeNumber);


    ACPI_FUNCTION_TRACE (EvInitializeGpeBlock);


    /*
     * Ignore a null GPE block (e.g., if no GPE block 1 exists), and
     * any GPE blocks that have been initialized already.
     */
    if (!GpeBlock || GpeBlock->Initialized)
    {
        return_ACPI_STATUS (AE_OK);
    }

    /*
     * Enable all GPEs that have a corresponding method and have the
     * ACPI_GPE_CAN_WAKE flag unset. Any other GPEs within this block
     * must be enabled via the acpi_enable_gpe() interface.
     */
    GpeEnabledCount = 0;

    for (i = 0; i < GpeBlock->RegisterCount; i++)
    {
        for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++)
        {
            /* Get the info block for this particular GPE */

            GpeIndex = (i * ACPI_GPE_REGISTER_WIDTH) + j;
            GpeEventInfo = &GpeBlock->EventInfo[GpeIndex];
            ACPI_ERROR_ONLY(GpeNumber = GpeBlock->BlockBaseNumber + GpeIndex);
            GpeEventInfo->Flags |= ACPI_GPE_INITIALIZED;

            /*
             * Ignore GPEs that have no corresponding _Lxx/_Exx method
             * and GPEs that are used to wake the system
             */
            if ((ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags) != ACPI_GPE_DISPATCH_METHOD) ||
                (GpeEventInfo->Flags & ACPI_GPE_CAN_WAKE))
            {
                continue;
            }

            Status = AcpiEvAddGpeReference (GpeEventInfo);
            if (ACPI_FAILURE (Status))
            {
                ACPI_EXCEPTION ((AE_INFO, Status,
                    "Could not enable GPE 0x%02X",
                    GpeNumber));
                continue;
            }

            GpeEventInfo->Flags |= ACPI_GPE_AUTO_ENABLED;

            if (IsPollingNeeded &&
                ACPI_GPE_IS_POLLING_NEEDED (GpeEventInfo))
            {
                *IsPollingNeeded = TRUE;
            }

            GpeEnabledCount++;
        }
    }

    if (GpeEnabledCount)
    {
        ACPI_INFO ((
            "Enabled %u GPEs in block %02X to %02X", GpeEnabledCount,
            (UINT32) GpeBlock->BlockBaseNumber,
            (UINT32) (GpeBlock->BlockBaseNumber + (GpeBlock->GpeCount - 1))));
    }

    GpeBlock->Initialized = TRUE;
    return_ACPI_STATUS (AE_OK);
}

#endif /* !ACPI_REDUCED_HARDWARE */
