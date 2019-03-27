/******************************************************************************
 *
 * Module Name: utxface - External interfaces, miscellaneous utility functions
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

#define EXPORT_ACPI_INTERFACES

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acdebug.h>

#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utxface")


/*******************************************************************************
 *
 * FUNCTION:    AcpiTerminate
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Shutdown the ACPICA subsystem and release all resources.
 *
 ******************************************************************************/

ACPI_STATUS ACPI_INIT_FUNCTION
AcpiTerminate (
    void)
{
    ACPI_STATUS         Status;


    ACPI_FUNCTION_TRACE (AcpiTerminate);


    /* Shutdown and free all resources */

    AcpiUtSubsystemShutdown ();

    /* Free the mutex objects */

    AcpiUtMutexTerminate ();

    /* Now we can shutdown the OS-dependent layer */

    Status = AcpiOsTerminate ();
    return_ACPI_STATUS (Status);
}

ACPI_EXPORT_SYMBOL_INIT (AcpiTerminate)


#ifndef ACPI_ASL_COMPILER
/*******************************************************************************
 *
 * FUNCTION:    AcpiSubsystemStatus
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status of the ACPI subsystem
 *
 * DESCRIPTION: Other drivers that use the ACPI subsystem should call this
 *              before making any other calls, to ensure the subsystem
 *              initialized successfully.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiSubsystemStatus (
    void)
{

    if (AcpiGbl_StartupFlags & ACPI_INITIALIZED_OK)
    {
        return (AE_OK);
    }
    else
    {
        return (AE_ERROR);
    }
}

ACPI_EXPORT_SYMBOL (AcpiSubsystemStatus)


/*******************************************************************************
 *
 * FUNCTION:    AcpiGetSystemInfo
 *
 * PARAMETERS:  OutBuffer       - A buffer to receive the resources for the
 *                                device
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to get information about the current
 *              state of the ACPI subsystem. It will return system information
 *              in the OutBuffer.
 *
 *              If the function fails an appropriate status will be returned
 *              and the value of OutBuffer is undefined.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiGetSystemInfo (
    ACPI_BUFFER             *OutBuffer)
{
    ACPI_SYSTEM_INFO        *InfoPtr;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (AcpiGetSystemInfo);


    /* Parameter validation */

    Status = AcpiUtValidateBuffer (OutBuffer);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Validate/Allocate/Clear caller buffer */

    Status = AcpiUtInitializeBuffer (OutBuffer, sizeof (ACPI_SYSTEM_INFO));
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /*
     * Populate the return buffer
     */
    InfoPtr = (ACPI_SYSTEM_INFO *) OutBuffer->Pointer;
    InfoPtr->AcpiCaVersion = ACPI_CA_VERSION;

    /* System flags (ACPI capabilities) */

    InfoPtr->Flags = ACPI_SYS_MODE_ACPI;

    /* Timer resolution - 24 or 32 bits  */

    if (AcpiGbl_FADT.Flags & ACPI_FADT_32BIT_TIMER)
    {
        InfoPtr->TimerResolution = 24;
    }
    else
    {
        InfoPtr->TimerResolution = 32;
    }

    /* Clear the reserved fields */

    InfoPtr->Reserved1 = 0;
    InfoPtr->Reserved2 = 0;

    /* Current debug levels */

    InfoPtr->DebugLayer = AcpiDbgLayer;
    InfoPtr->DebugLevel = AcpiDbgLevel;

    return_ACPI_STATUS (AE_OK);
}

ACPI_EXPORT_SYMBOL (AcpiGetSystemInfo)


/*******************************************************************************
 *
 * FUNCTION:    AcpiGetStatistics
 *
 * PARAMETERS:  Stats           - Where the statistics are returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: Get the contents of the various system counters
 *
 ******************************************************************************/

ACPI_STATUS
AcpiGetStatistics (
    ACPI_STATISTICS         *Stats)
{
    ACPI_FUNCTION_TRACE (AcpiGetStatistics);


    /* Parameter validation */

    if (!Stats)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /* Various interrupt-based event counters */

    Stats->SciCount = AcpiSciCount;
    Stats->GpeCount = AcpiGpeCount;

    memcpy (Stats->FixedEventCount, AcpiFixedEventCount,
        sizeof (AcpiFixedEventCount));

    /* Other counters */

    Stats->MethodCount = AcpiMethodCount;
    return_ACPI_STATUS (AE_OK);
}

ACPI_EXPORT_SYMBOL (AcpiGetStatistics)


/*****************************************************************************
 *
 * FUNCTION:    AcpiInstallInitializationHandler
 *
 * PARAMETERS:  Handler             - Callback procedure
 *              Function            - Not (currently) used, see below
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install an initialization handler
 *
 * TBD: When a second function is added, must save the Function also.
 *
 ****************************************************************************/

ACPI_STATUS
AcpiInstallInitializationHandler (
    ACPI_INIT_HANDLER       Handler,
    UINT32                  Function)
{

    if (!Handler)
    {
        return (AE_BAD_PARAMETER);
    }

    if (AcpiGbl_InitHandler)
    {
        return (AE_ALREADY_EXISTS);
    }

    AcpiGbl_InitHandler = Handler;
    return (AE_OK);
}

ACPI_EXPORT_SYMBOL (AcpiInstallInitializationHandler)


/*****************************************************************************
 *
 * FUNCTION:    AcpiPurgeCachedObjects
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Empty all caches (delete the cached objects)
 *
 ****************************************************************************/

ACPI_STATUS
AcpiPurgeCachedObjects (
    void)
{
    ACPI_FUNCTION_TRACE (AcpiPurgeCachedObjects);


    (void) AcpiOsPurgeCache (AcpiGbl_StateCache);
    (void) AcpiOsPurgeCache (AcpiGbl_OperandCache);
    (void) AcpiOsPurgeCache (AcpiGbl_PsNodeCache);
    (void) AcpiOsPurgeCache (AcpiGbl_PsNodeExtCache);

    return_ACPI_STATUS (AE_OK);
}

ACPI_EXPORT_SYMBOL (AcpiPurgeCachedObjects)


/*****************************************************************************
 *
 * FUNCTION:    AcpiInstallInterface
 *
 * PARAMETERS:  InterfaceName       - The interface to install
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install an _OSI interface to the global list
 *
 ****************************************************************************/

ACPI_STATUS
AcpiInstallInterface (
    ACPI_STRING             InterfaceName)
{
    ACPI_STATUS             Status;
    ACPI_INTERFACE_INFO     *InterfaceInfo;


    /* Parameter validation */

    if (!InterfaceName || (strlen (InterfaceName) == 0))
    {
        return (AE_BAD_PARAMETER);
    }

    Status = AcpiOsAcquireMutex (AcpiGbl_OsiMutex, ACPI_WAIT_FOREVER);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* Check if the interface name is already in the global list */

    InterfaceInfo = AcpiUtGetInterface (InterfaceName);
    if (InterfaceInfo)
    {
        /*
         * The interface already exists in the list. This is OK if the
         * interface has been marked invalid -- just clear the bit.
         */
        if (InterfaceInfo->Flags & ACPI_OSI_INVALID)
        {
            InterfaceInfo->Flags &= ~ACPI_OSI_INVALID;
            Status = AE_OK;
        }
        else
        {
            Status = AE_ALREADY_EXISTS;
        }
    }
    else
    {
        /* New interface name, install into the global list */

        Status = AcpiUtInstallInterface (InterfaceName);
    }

    AcpiOsReleaseMutex (AcpiGbl_OsiMutex);
    return (Status);
}

ACPI_EXPORT_SYMBOL (AcpiInstallInterface)


/*****************************************************************************
 *
 * FUNCTION:    AcpiRemoveInterface
 *
 * PARAMETERS:  InterfaceName       - The interface to remove
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove an _OSI interface from the global list
 *
 ****************************************************************************/

ACPI_STATUS
AcpiRemoveInterface (
    ACPI_STRING             InterfaceName)
{
    ACPI_STATUS             Status;


    /* Parameter validation */

    if (!InterfaceName || (strlen (InterfaceName) == 0))
    {
        return (AE_BAD_PARAMETER);
    }

    Status = AcpiOsAcquireMutex (AcpiGbl_OsiMutex, ACPI_WAIT_FOREVER);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    Status = AcpiUtRemoveInterface (InterfaceName);

    AcpiOsReleaseMutex (AcpiGbl_OsiMutex);
    return (Status);
}

ACPI_EXPORT_SYMBOL (AcpiRemoveInterface)


/*****************************************************************************
 *
 * FUNCTION:    AcpiInstallInterfaceHandler
 *
 * PARAMETERS:  Handler             - The _OSI interface handler to install
 *                                    NULL means "remove existing handler"
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for the predefined _OSI ACPI method.
 *              invoked during execution of the internal implementation of
 *              _OSI. A NULL handler simply removes any existing handler.
 *
 ****************************************************************************/

ACPI_STATUS
AcpiInstallInterfaceHandler (
    ACPI_INTERFACE_HANDLER  Handler)
{
    ACPI_STATUS             Status;


    Status = AcpiOsAcquireMutex (AcpiGbl_OsiMutex, ACPI_WAIT_FOREVER);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    if (Handler && AcpiGbl_InterfaceHandler)
    {
        Status = AE_ALREADY_EXISTS;
    }
    else
    {
        AcpiGbl_InterfaceHandler = Handler;
    }

    AcpiOsReleaseMutex (AcpiGbl_OsiMutex);
    return (Status);
}

ACPI_EXPORT_SYMBOL (AcpiInstallInterfaceHandler)


/*****************************************************************************
 *
 * FUNCTION:    AcpiUpdateInterfaces
 *
 * PARAMETERS:  Action              - Actions to be performed during the
 *                                    update
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Update _OSI interface strings, disabling or enabling OS vendor
 *              string or/and feature group strings.
 *
 ****************************************************************************/

ACPI_STATUS
AcpiUpdateInterfaces (
    UINT8                   Action)
{
    ACPI_STATUS             Status;


    Status = AcpiOsAcquireMutex (AcpiGbl_OsiMutex, ACPI_WAIT_FOREVER);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    Status = AcpiUtUpdateInterfaces (Action);

    AcpiOsReleaseMutex (AcpiGbl_OsiMutex);
    return (Status);
}


/*****************************************************************************
 *
 * FUNCTION:    AcpiCheckAddressRange
 *
 * PARAMETERS:  SpaceId             - Address space ID
 *              Address             - Start address
 *              Length              - Length
 *              Warn                - TRUE if warning on overlap desired
 *
 * RETURN:      Count of the number of conflicts detected.
 *
 * DESCRIPTION: Check if the input address range overlaps any of the
 *              ASL operation region address ranges.
 *
 ****************************************************************************/

UINT32
AcpiCheckAddressRange (
    ACPI_ADR_SPACE_TYPE     SpaceId,
    ACPI_PHYSICAL_ADDRESS   Address,
    ACPI_SIZE               Length,
    BOOLEAN                 Warn)
{
    UINT32                  Overlaps;
    ACPI_STATUS             Status;


    Status = AcpiUtAcquireMutex (ACPI_MTX_NAMESPACE);
    if (ACPI_FAILURE (Status))
    {
        return (0);
    }

    Overlaps = AcpiUtCheckAddressRange (SpaceId, Address,
        (UINT32) Length, Warn);

    (void) AcpiUtReleaseMutex (ACPI_MTX_NAMESPACE);
    return (Overlaps);
}

ACPI_EXPORT_SYMBOL (AcpiCheckAddressRange)

#endif /* !ACPI_ASL_COMPILER */


/*******************************************************************************
 *
 * FUNCTION:    AcpiDecodePldBuffer
 *
 * PARAMETERS:  InBuffer            - Buffer returned by _PLD method
 *              Length              - Length of the InBuffer
 *              ReturnBuffer        - Where the decode buffer is returned
 *
 * RETURN:      Status and the decoded _PLD buffer. User must deallocate
 *              the buffer via ACPI_FREE.
 *
 * DESCRIPTION: Decode the bit-packed buffer returned by the _PLD method into
 *              a local struct that is much more useful to an ACPI driver.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDecodePldBuffer (
    UINT8                   *InBuffer,
    ACPI_SIZE               Length,
    ACPI_PLD_INFO           **ReturnBuffer)
{
    ACPI_PLD_INFO           *PldInfo;
    UINT32                  *Buffer = ACPI_CAST_PTR (UINT32, InBuffer);
    UINT32                  Dword;


    /* Parameter validation */

    if (!InBuffer || !ReturnBuffer || (Length < ACPI_PLD_REV1_BUFFER_SIZE))
    {
        return (AE_BAD_PARAMETER);
    }

    PldInfo = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_PLD_INFO));
    if (!PldInfo)
    {
        return (AE_NO_MEMORY);
    }

    /* First 32-bit DWord */

    ACPI_MOVE_32_TO_32 (&Dword, &Buffer[0]);
    PldInfo->Revision =             ACPI_PLD_GET_REVISION (&Dword);
    PldInfo->IgnoreColor =          ACPI_PLD_GET_IGNORE_COLOR (&Dword);
    PldInfo->Red =                  ACPI_PLD_GET_RED (&Dword);
    PldInfo->Green =                ACPI_PLD_GET_GREEN (&Dword);
    PldInfo->Blue =                 ACPI_PLD_GET_BLUE (&Dword);

    /* Second 32-bit DWord */

    ACPI_MOVE_32_TO_32 (&Dword, &Buffer[1]);
    PldInfo->Width =                ACPI_PLD_GET_WIDTH (&Dword);
    PldInfo->Height =               ACPI_PLD_GET_HEIGHT(&Dword);

    /* Third 32-bit DWord */

    ACPI_MOVE_32_TO_32 (&Dword, &Buffer[2]);
    PldInfo->UserVisible =          ACPI_PLD_GET_USER_VISIBLE (&Dword);
    PldInfo->Dock =                 ACPI_PLD_GET_DOCK (&Dword);
    PldInfo->Lid =                  ACPI_PLD_GET_LID (&Dword);
    PldInfo->Panel =                ACPI_PLD_GET_PANEL (&Dword);
    PldInfo->VerticalPosition =     ACPI_PLD_GET_VERTICAL (&Dword);
    PldInfo->HorizontalPosition =   ACPI_PLD_GET_HORIZONTAL (&Dword);
    PldInfo->Shape =                ACPI_PLD_GET_SHAPE (&Dword);
    PldInfo->GroupOrientation =     ACPI_PLD_GET_ORIENTATION (&Dword);
    PldInfo->GroupToken =           ACPI_PLD_GET_TOKEN (&Dword);
    PldInfo->GroupPosition =        ACPI_PLD_GET_POSITION (&Dword);
    PldInfo->Bay =                  ACPI_PLD_GET_BAY (&Dword);

    /* Fourth 32-bit DWord */

    ACPI_MOVE_32_TO_32 (&Dword, &Buffer[3]);
    PldInfo->Ejectable =            ACPI_PLD_GET_EJECTABLE (&Dword);
    PldInfo->OspmEjectRequired =    ACPI_PLD_GET_OSPM_EJECT (&Dword);
    PldInfo->CabinetNumber =        ACPI_PLD_GET_CABINET (&Dword);
    PldInfo->CardCageNumber =       ACPI_PLD_GET_CARD_CAGE (&Dword);
    PldInfo->Reference =            ACPI_PLD_GET_REFERENCE (&Dword);
    PldInfo->Rotation =             ACPI_PLD_GET_ROTATION (&Dword);
    PldInfo->Order =                ACPI_PLD_GET_ORDER (&Dword);

    if (Length >= ACPI_PLD_REV2_BUFFER_SIZE)
    {
        /* Fifth 32-bit DWord (Revision 2 of _PLD) */

        ACPI_MOVE_32_TO_32 (&Dword, &Buffer[4]);
        PldInfo->VerticalOffset =       ACPI_PLD_GET_VERT_OFFSET (&Dword);
        PldInfo->HorizontalOffset =     ACPI_PLD_GET_HORIZ_OFFSET (&Dword);
    }

    *ReturnBuffer = PldInfo;
    return (AE_OK);
}

ACPI_EXPORT_SYMBOL (AcpiDecodePldBuffer)
