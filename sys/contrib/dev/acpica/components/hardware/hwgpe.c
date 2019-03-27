/******************************************************************************
 *
 * Module Name: hwgpe - Low level GPE enable/disable/clear functions
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

#define _COMPONENT          ACPI_HARDWARE
        ACPI_MODULE_NAME    ("hwgpe")

#if (!ACPI_REDUCED_HARDWARE) /* Entire module */

/* Local prototypes */

static ACPI_STATUS
AcpiHwEnableWakeupGpeBlock (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context);

static ACPI_STATUS
AcpiHwGpeEnableWrite (
    UINT8                   EnableMask,
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo);


/******************************************************************************
 *
 * FUNCTION:    AcpiHwGetGpeRegisterBit
 *
 * PARAMETERS:  GpeEventInfo        - Info block for the GPE
 *
 * RETURN:      Register mask with a one in the GPE bit position
 *
 * DESCRIPTION: Compute the register mask for this GPE. One bit is set in the
 *              correct position for the input GPE.
 *
 ******************************************************************************/

UINT32
AcpiHwGetGpeRegisterBit (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{

    return ((UINT32) 1 <<
        (GpeEventInfo->GpeNumber - GpeEventInfo->RegisterInfo->BaseGpeNumber));
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwLowSetGpe
 *
 * PARAMETERS:  GpeEventInfo        - Info block for the GPE to be disabled
 *              Action              - Enable or disable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable or disable a single GPE in the parent enable register.
 *              The EnableMask field of the involved GPE register must be
 *              updated by the caller if necessary.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwLowSetGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    UINT32                  Action)
{
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    ACPI_STATUS             Status = AE_OK;
    UINT64                  EnableMask;
    UINT32                  RegisterBit;


    ACPI_FUNCTION_ENTRY ();


    /* Get the info block for the entire GPE register */

    GpeRegisterInfo = GpeEventInfo->RegisterInfo;
    if (!GpeRegisterInfo)
    {
        return (AE_NOT_EXIST);
    }

    /* Get current value of the enable register that contains this GPE */

    Status = AcpiHwRead (&EnableMask, &GpeRegisterInfo->EnableAddress);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* Set or clear just the bit that corresponds to this GPE */

    RegisterBit = AcpiHwGetGpeRegisterBit (GpeEventInfo);
    switch (Action)
    {
    case ACPI_GPE_CONDITIONAL_ENABLE:

        /* Only enable if the corresponding EnableMask bit is set */

        if (!(RegisterBit & GpeRegisterInfo->EnableMask))
        {
            return (AE_BAD_PARAMETER);
        }

        /*lint -fallthrough */

    case ACPI_GPE_ENABLE:

        ACPI_SET_BIT (EnableMask, RegisterBit);
        break;

    case ACPI_GPE_DISABLE:

        ACPI_CLEAR_BIT (EnableMask, RegisterBit);
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Invalid GPE Action, %u", Action));
        return (AE_BAD_PARAMETER);
    }

    if (!(RegisterBit & GpeRegisterInfo->MaskForRun))
    {
        /* Write the updated enable mask */

        Status = AcpiHwWrite (EnableMask, &GpeRegisterInfo->EnableAddress);
    }
    return (Status);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwClearGpe
 *
 * PARAMETERS:  GpeEventInfo        - Info block for the GPE to be cleared
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear the status bit for a single GPE.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwClearGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    ACPI_STATUS             Status;
    UINT32                  RegisterBit;


    ACPI_FUNCTION_ENTRY ();

    /* Get the info block for the entire GPE register */

    GpeRegisterInfo = GpeEventInfo->RegisterInfo;
    if (!GpeRegisterInfo)
    {
        return (AE_NOT_EXIST);
    }

    /*
     * Write a one to the appropriate bit in the status register to
     * clear this GPE.
     */
    RegisterBit = AcpiHwGetGpeRegisterBit (GpeEventInfo);

    Status = AcpiHwWrite (RegisterBit, &GpeRegisterInfo->StatusAddress);
    return (Status);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwGetGpeStatus
 *
 * PARAMETERS:  GpeEventInfo        - Info block for the GPE to queried
 *              EventStatus         - Where the GPE status is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the status of a single GPE.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwGetGpeStatus (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    ACPI_EVENT_STATUS       *EventStatus)
{
    UINT64                  InByte;
    UINT32                  RegisterBit;
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    ACPI_EVENT_STATUS       LocalEventStatus = 0;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_ENTRY ();


    if (!EventStatus)
    {
        return (AE_BAD_PARAMETER);
    }

    /* GPE currently handled? */

    if (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags) !=
        ACPI_GPE_DISPATCH_NONE)
    {
        LocalEventStatus |= ACPI_EVENT_FLAG_HAS_HANDLER;
    }

    /* Get the info block for the entire GPE register */

    GpeRegisterInfo = GpeEventInfo->RegisterInfo;

    /* Get the register bitmask for this GPE */

    RegisterBit = AcpiHwGetGpeRegisterBit (GpeEventInfo);

    /* GPE currently enabled? (enabled for runtime?) */

    if (RegisterBit & GpeRegisterInfo->EnableForRun)
    {
        LocalEventStatus |= ACPI_EVENT_FLAG_ENABLED;
    }

    /* GPE currently masked? (masked for runtime?) */

    if (RegisterBit & GpeRegisterInfo->MaskForRun)
    {
        LocalEventStatus |= ACPI_EVENT_FLAG_MASKED;
    }

    /* GPE enabled for wake? */

    if (RegisterBit & GpeRegisterInfo->EnableForWake)
    {
        LocalEventStatus |= ACPI_EVENT_FLAG_WAKE_ENABLED;
    }

    /* GPE currently enabled (enable bit == 1)? */

    Status = AcpiHwRead (&InByte, &GpeRegisterInfo->EnableAddress);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    if (RegisterBit & InByte)
    {
        LocalEventStatus |= ACPI_EVENT_FLAG_ENABLE_SET;
    }

    /* GPE currently active (status bit == 1)? */

    Status = AcpiHwRead (&InByte, &GpeRegisterInfo->StatusAddress);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    if (RegisterBit & InByte)
    {
        LocalEventStatus |= ACPI_EVENT_FLAG_STATUS_SET;
    }

    /* Set return value */

    (*EventStatus) = LocalEventStatus;
    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwGpeEnableWrite
 *
 * PARAMETERS:  EnableMask          - Bit mask to write to the GPE register
 *              GpeRegisterInfo     - Gpe Register info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write the enable mask byte to the given GPE register.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiHwGpeEnableWrite (
    UINT8                   EnableMask,
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo)
{
    ACPI_STATUS             Status;


    GpeRegisterInfo->EnableMask = EnableMask;

    Status = AcpiHwWrite (EnableMask, &GpeRegisterInfo->EnableAddress);
    return (Status);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwDisableGpeBlock
 *
 * PARAMETERS:  GpeXruptInfo        - GPE Interrupt info
 *              GpeBlock            - Gpe Block info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disable all GPEs within a single GPE block
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwDisableGpeBlock (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context)
{
    UINT32                  i;
    ACPI_STATUS             Status;


    /* Examine each GPE Register within the block */

    for (i = 0; i < GpeBlock->RegisterCount; i++)
    {
        /* Disable all GPEs in this register */

        Status = AcpiHwGpeEnableWrite (0x00, &GpeBlock->RegisterInfo[i]);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwClearGpeBlock
 *
 * PARAMETERS:  GpeXruptInfo        - GPE Interrupt info
 *              GpeBlock            - Gpe Block info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear status bits for all GPEs within a single GPE block
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwClearGpeBlock (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context)
{
    UINT32                  i;
    ACPI_STATUS             Status;


    /* Examine each GPE Register within the block */

    for (i = 0; i < GpeBlock->RegisterCount; i++)
    {
        /* Clear status on all GPEs in this register */

        Status = AcpiHwWrite (0xFF, &GpeBlock->RegisterInfo[i].StatusAddress);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwEnableRuntimeGpeBlock
 *
 * PARAMETERS:  GpeXruptInfo        - GPE Interrupt info
 *              GpeBlock            - Gpe Block info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable all "runtime" GPEs within a single GPE block. Includes
 *              combination wake/run GPEs.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwEnableRuntimeGpeBlock (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context)
{
    UINT32                  i;
    ACPI_STATUS             Status;
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    UINT8                   EnableMask;


    /* NOTE: assumes that all GPEs are currently disabled */

    /* Examine each GPE Register within the block */

    for (i = 0; i < GpeBlock->RegisterCount; i++)
    {
        GpeRegisterInfo = &GpeBlock->RegisterInfo[i];
        if (!GpeRegisterInfo->EnableForRun)
        {
            continue;
        }

        /* Enable all "runtime" GPEs in this register */

        EnableMask = GpeRegisterInfo->EnableForRun &
            ~GpeRegisterInfo->MaskForRun;
        Status = AcpiHwGpeEnableWrite (EnableMask, GpeRegisterInfo);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwEnableWakeupGpeBlock
 *
 * PARAMETERS:  GpeXruptInfo        - GPE Interrupt info
 *              GpeBlock            - Gpe Block info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable all "wake" GPEs within a single GPE block. Includes
 *              combination wake/run GPEs.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiHwEnableWakeupGpeBlock (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context)
{
    UINT32                  i;
    ACPI_STATUS             Status;
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;


    /* Examine each GPE Register within the block */

    for (i = 0; i < GpeBlock->RegisterCount; i++)
    {
        GpeRegisterInfo = &GpeBlock->RegisterInfo[i];

        /*
         * Enable all "wake" GPEs in this register and disable the
         * remaining ones.
         */
        Status = AcpiHwGpeEnableWrite (GpeRegisterInfo->EnableForWake,
            GpeRegisterInfo);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwDisableAllGpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disable and clear all GPEs in all GPE blocks
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwDisableAllGpes (
    void)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (HwDisableAllGpes);


    Status = AcpiEvWalkGpeList (AcpiHwDisableGpeBlock, NULL);
    return_ACPI_STATUS (Status);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwEnableAllRuntimeGpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable all "runtime" GPEs, in all GPE blocks
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwEnableAllRuntimeGpes (
    void)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (HwEnableAllRuntimeGpes);


    Status = AcpiEvWalkGpeList (AcpiHwEnableRuntimeGpeBlock, NULL);
    return_ACPI_STATUS (Status);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwEnableAllWakeupGpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable all "wakeup" GPEs, in all GPE blocks
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwEnableAllWakeupGpes (
    void)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (HwEnableAllWakeupGpes);


    Status = AcpiEvWalkGpeList (AcpiHwEnableWakeupGpeBlock, NULL);
    return_ACPI_STATUS (Status);
}

#endif /* !ACPI_REDUCED_HARDWARE */
