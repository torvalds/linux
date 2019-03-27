/******************************************************************************
 *
 * Module Name: utinit - Common ACPI subsystem initialization
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
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acevents.h>
#include <contrib/dev/acpica/include/actables.h>

#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utinit")

/* Local prototypes */

static void AcpiUtTerminate (
    void);

#if (!ACPI_REDUCED_HARDWARE)

static void
AcpiUtFreeGpeLists (
    void);

#else

#define AcpiUtFreeGpeLists()
#endif /* !ACPI_REDUCED_HARDWARE */


#if (!ACPI_REDUCED_HARDWARE)
/******************************************************************************
 *
 * FUNCTION:    AcpiUtFreeGpeLists
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Free global GPE lists
 *
 ******************************************************************************/

static void
AcpiUtFreeGpeLists (
    void)
{
    ACPI_GPE_BLOCK_INFO     *GpeBlock;
    ACPI_GPE_BLOCK_INFO     *NextGpeBlock;
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo;
    ACPI_GPE_XRUPT_INFO     *NextGpeXruptInfo;


    /* Free global GPE blocks and related info structures */

    GpeXruptInfo = AcpiGbl_GpeXruptListHead;
    while (GpeXruptInfo)
    {
        GpeBlock = GpeXruptInfo->GpeBlockListHead;
        while (GpeBlock)
        {
            NextGpeBlock = GpeBlock->Next;
            ACPI_FREE (GpeBlock->EventInfo);
            ACPI_FREE (GpeBlock->RegisterInfo);
            ACPI_FREE (GpeBlock);

            GpeBlock = NextGpeBlock;
        }
        NextGpeXruptInfo = GpeXruptInfo->Next;
        ACPI_FREE (GpeXruptInfo);
        GpeXruptInfo = NextGpeXruptInfo;
    }
}
#endif /* !ACPI_REDUCED_HARDWARE */


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtInitGlobals
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize ACPICA globals. All globals that require specific
 *              initialization should be initialized here. This allows for
 *              a warm restart.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtInitGlobals (
    void)
{
    ACPI_STATUS             Status;
    UINT32                  i;


    ACPI_FUNCTION_TRACE (UtInitGlobals);


    /* Create all memory caches */

    Status = AcpiUtCreateCaches ();
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Address Range lists */

    for (i = 0; i < ACPI_ADDRESS_RANGE_MAX; i++)
    {
        AcpiGbl_AddressRangeList[i] = NULL;
    }

    /* Mutex locked flags */

    for (i = 0; i < ACPI_NUM_MUTEX; i++)
    {
        AcpiGbl_MutexInfo[i].Mutex          = NULL;
        AcpiGbl_MutexInfo[i].ThreadId       = ACPI_MUTEX_NOT_ACQUIRED;
        AcpiGbl_MutexInfo[i].UseCount       = 0;
    }

    for (i = 0; i < ACPI_NUM_OWNERID_MASKS; i++)
    {
        AcpiGbl_OwnerIdMask[i]              = 0;
    }

    /* Last OwnerID is never valid */

    AcpiGbl_OwnerIdMask[ACPI_NUM_OWNERID_MASKS - 1] = 0x80000000;

    /* Event counters */

    AcpiMethodCount                     = 0;
    AcpiSciCount                        = 0;
    AcpiGpeCount                        = 0;

    for (i = 0; i < ACPI_NUM_FIXED_EVENTS; i++)
    {
        AcpiFixedEventCount[i]              = 0;
    }

#if (!ACPI_REDUCED_HARDWARE)

    /* GPE/SCI support */

    AcpiGbl_AllGpesInitialized          = FALSE;
    AcpiGbl_GpeXruptListHead            = NULL;
    AcpiGbl_GpeFadtBlocks[0]            = NULL;
    AcpiGbl_GpeFadtBlocks[1]            = NULL;
    AcpiCurrentGpeCount                 = 0;

    AcpiGbl_GlobalEventHandler          = NULL;
    AcpiGbl_SciHandlerList              = NULL;

#endif /* !ACPI_REDUCED_HARDWARE */

    /* Global handlers */

    AcpiGbl_GlobalNotify[0].Handler     = NULL;
    AcpiGbl_GlobalNotify[1].Handler     = NULL;
    AcpiGbl_ExceptionHandler            = NULL;
    AcpiGbl_InitHandler                 = NULL;
    AcpiGbl_TableHandler                = NULL;
    AcpiGbl_InterfaceHandler            = NULL;

    /* Global Lock support */

    AcpiGbl_GlobalLockSemaphore         = NULL;
    AcpiGbl_GlobalLockMutex             = NULL;
    AcpiGbl_GlobalLockAcquired          = FALSE;
    AcpiGbl_GlobalLockHandle            = 0;
    AcpiGbl_GlobalLockPresent           = FALSE;

    /* Miscellaneous variables */

    AcpiGbl_DSDT                        = NULL;
    AcpiGbl_CmSingleStep                = FALSE;
    AcpiGbl_Shutdown                    = FALSE;
    AcpiGbl_NsLookupCount               = 0;
    AcpiGbl_PsFindCount                 = 0;
    AcpiGbl_AcpiHardwarePresent         = TRUE;
    AcpiGbl_LastOwnerIdIndex            = 0;
    AcpiGbl_NextOwnerIdOffset           = 0;
    AcpiGbl_DebuggerConfiguration       = DEBUGGER_THREADING;
    AcpiGbl_OsiMutex                    = NULL;

    /* Hardware oriented */

    AcpiGbl_EventsInitialized           = FALSE;
    AcpiGbl_SystemAwakeAndRunning       = TRUE;

    /* Namespace */

    AcpiGbl_ModuleCodeList              = NULL;
    AcpiGbl_RootNode                    = NULL;
    AcpiGbl_RootNodeStruct.Name.Integer = ACPI_ROOT_NAME;
    AcpiGbl_RootNodeStruct.DescriptorType = ACPI_DESC_TYPE_NAMED;
    AcpiGbl_RootNodeStruct.Type         = ACPI_TYPE_DEVICE;
    AcpiGbl_RootNodeStruct.Parent       = NULL;
    AcpiGbl_RootNodeStruct.Child        = NULL;
    AcpiGbl_RootNodeStruct.Peer         = NULL;
    AcpiGbl_RootNodeStruct.Object       = NULL;


#ifdef ACPI_DISASSEMBLER
    AcpiGbl_ExternalList                = NULL;
    AcpiGbl_NumExternalMethods          = 0;
    AcpiGbl_ResolvedExternalMethods     = 0;
#endif

#ifdef ACPI_DEBUG_OUTPUT
    AcpiGbl_LowestStackPointer          = ACPI_CAST_PTR (ACPI_SIZE, ACPI_SIZE_MAX);
#endif

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
    AcpiGbl_DisplayFinalMemStats        = FALSE;
    AcpiGbl_DisableMemTracking          = FALSE;
#endif

    return_ACPI_STATUS (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiUtTerminate
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Free global memory
 *
 ******************************************************************************/

static void
AcpiUtTerminate (
    void)
{
    ACPI_FUNCTION_TRACE (UtTerminate);

    AcpiUtFreeGpeLists ();
    AcpiUtDeleteAddressLists ();
    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtSubsystemShutdown
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Shutdown the various components. Do not delete the mutex
 *              objects here, because the AML debugger may be still running.
 *
 ******************************************************************************/

void
AcpiUtSubsystemShutdown (
    void)
{
    ACPI_FUNCTION_TRACE (UtSubsystemShutdown);


    /* Just exit if subsystem is already shutdown */

    if (AcpiGbl_Shutdown)
    {
        ACPI_ERROR ((AE_INFO, "ACPI Subsystem is already terminated"));
        return_VOID;
    }

    /* Subsystem appears active, go ahead and shut it down */

    AcpiGbl_Shutdown = TRUE;
    AcpiGbl_StartupFlags = 0;
    ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Shutting down ACPI Subsystem\n"));

#ifndef ACPI_ASL_COMPILER

    /* Close the AcpiEvent Handling */

    AcpiEvTerminate ();

    /* Delete any dynamic _OSI interfaces */

    AcpiUtInterfaceTerminate ();
#endif

    /* Close the Namespace */

    AcpiNsTerminate ();

    /* Delete the ACPI tables */

    AcpiTbTerminate ();

    /* Close the globals */

    AcpiUtTerminate ();

    /* Purge the local caches */

    (void) AcpiUtDeleteCaches ();
    return_VOID;
}
