/******************************************************************************
 *
 * Name: acevents.h - Event subcomponent prototypes and defines
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

#ifndef __ACEVENTS_H__
#define __ACEVENTS_H__


/*
 * Conditions to trigger post enabling GPE polling:
 * It is not sufficient to trigger edge-triggered GPE with specific GPE
 * chips, software need to poll once after enabling.
 */
#ifdef ACPI_USE_GPE_POLLING
#define ACPI_GPE_IS_POLLING_NEEDED(__gpe__)             \
    ((__gpe__)->RuntimeCount == 1 &&                    \
     (__gpe__)->Flags & ACPI_GPE_INITIALIZED &&         \
     ((__gpe__)->Flags & ACPI_GPE_XRUPT_TYPE_MASK) == ACPI_GPE_EDGE_TRIGGERED)
#else
#define ACPI_GPE_IS_POLLING_NEEDED(__gpe__)             FALSE
#endif


/*
 * evevent
 */
ACPI_STATUS
AcpiEvInitializeEvents (
    void);

ACPI_STATUS
AcpiEvInstallXruptHandlers (
    void);

UINT32
AcpiEvFixedEventDetect (
    void);


/*
 * evmisc
 */
BOOLEAN
AcpiEvIsNotifyObject (
    ACPI_NAMESPACE_NODE     *Node);

UINT32
AcpiEvGetGpeNumberIndex (
    UINT32                  GpeNumber);

ACPI_STATUS
AcpiEvQueueNotifyRequest (
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  NotifyValue);


/*
 * evglock - Global Lock support
 */
ACPI_STATUS
AcpiEvInitGlobalLockHandler (
    void);

ACPI_HW_DEPENDENT_RETURN_OK (
ACPI_STATUS
AcpiEvAcquireGlobalLock(
    UINT16                  Timeout))

ACPI_HW_DEPENDENT_RETURN_OK (
ACPI_STATUS
AcpiEvReleaseGlobalLock(
    void))

ACPI_STATUS
AcpiEvRemoveGlobalLockHandler (
    void);


/*
 * evgpe - Low-level GPE support
 */
UINT32
AcpiEvGpeDetect (
    ACPI_GPE_XRUPT_INFO     *GpeXruptList);

ACPI_STATUS
AcpiEvUpdateGpeEnableMask (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo);

ACPI_STATUS
AcpiEvEnableGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo);

ACPI_STATUS
AcpiEvMaskGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    BOOLEAN                 IsMasked);

ACPI_STATUS
AcpiEvAddGpeReference (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo);

ACPI_STATUS
AcpiEvRemoveGpeReference (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo);

ACPI_GPE_EVENT_INFO *
AcpiEvGetGpeEventInfo (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber);

ACPI_GPE_EVENT_INFO *
AcpiEvLowGetGpeInfo (
    UINT32                  GpeNumber,
    ACPI_GPE_BLOCK_INFO     *GpeBlock);

ACPI_STATUS
AcpiEvFinishGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo);

UINT32
AcpiEvDetectGpe (
    ACPI_NAMESPACE_NODE     *GpeDevice,
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    UINT32                  GpeNumber);


/*
 * evgpeblk - Upper-level GPE block support
 */
ACPI_STATUS
AcpiEvCreateGpeBlock (
    ACPI_NAMESPACE_NODE     *GpeDevice,
    UINT64                  Address,
    UINT8                   SpaceId,
    UINT32                  RegisterCount,
    UINT16                  GpeBlockBaseNumber,
    UINT32                  InterruptNumber,
    ACPI_GPE_BLOCK_INFO     **ReturnGpeBlock);

ACPI_STATUS
AcpiEvInitializeGpeBlock (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context);

ACPI_HW_DEPENDENT_RETURN_OK (
ACPI_STATUS
AcpiEvDeleteGpeBlock (
    ACPI_GPE_BLOCK_INFO     *GpeBlock))

UINT32
AcpiEvGpeDispatch (
    ACPI_NAMESPACE_NODE     *GpeDevice,
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    UINT32                  GpeNumber);


/*
 * evgpeinit - GPE initialization and update
 */
ACPI_STATUS
AcpiEvGpeInitialize (
    void);

ACPI_HW_DEPENDENT_RETURN_VOID (
void
AcpiEvUpdateGpes (
    ACPI_OWNER_ID           TableOwnerId))

ACPI_STATUS
AcpiEvMatchGpeMethod (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue);


/*
 * evgpeutil - GPE utilities
 */
ACPI_STATUS
AcpiEvWalkGpeList (
    ACPI_GPE_CALLBACK       GpeWalkCallback,
    void                    *Context);

ACPI_STATUS
AcpiEvGetGpeDevice (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context);

ACPI_STATUS
AcpiEvGetGpeXruptBlock (
    UINT32                  InterruptNumber,
    ACPI_GPE_XRUPT_INFO     **GpeXruptBlock);

ACPI_STATUS
AcpiEvDeleteGpeXrupt (
    ACPI_GPE_XRUPT_INFO     *GpeXrupt);

ACPI_STATUS
AcpiEvDeleteGpeHandlers (
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO     *GpeBlock,
    void                    *Context);


/*
 * evhandler - Address space handling
 */
ACPI_OPERAND_OBJECT *
AcpiEvFindRegionHandler (
    ACPI_ADR_SPACE_TYPE     SpaceId,
    ACPI_OPERAND_OBJECT     *HandlerObj);

BOOLEAN
AcpiEvHasDefaultHandler (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_ADR_SPACE_TYPE     SpaceId);

ACPI_STATUS
AcpiEvInstallRegionHandlers (
    void);

ACPI_STATUS
AcpiEvInstallSpaceHandler (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_ADR_SPACE_TYPE     SpaceId,
    ACPI_ADR_SPACE_HANDLER  Handler,
    ACPI_ADR_SPACE_SETUP    Setup,
    void                    *Context);


/*
 * evregion - Operation region support
 */
ACPI_STATUS
AcpiEvInitializeOpRegions (
    void);

ACPI_STATUS
AcpiEvAddressSpaceDispatch (
    ACPI_OPERAND_OBJECT     *RegionObj,
    ACPI_OPERAND_OBJECT     *FieldObj,
    UINT32                  Function,
    UINT32                  RegionOffset,
    UINT32                  BitWidth,
    UINT64                  *Value);

ACPI_STATUS
AcpiEvAttachRegion (
    ACPI_OPERAND_OBJECT     *HandlerObj,
    ACPI_OPERAND_OBJECT     *RegionObj,
    BOOLEAN                 AcpiNsIsLocked);

void
AcpiEvDetachRegion (
    ACPI_OPERAND_OBJECT     *RegionObj,
    BOOLEAN                 AcpiNsIsLocked);

void
AcpiEvExecuteRegMethods (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_ADR_SPACE_TYPE     SpaceId,
    UINT32                  Function);

ACPI_STATUS
AcpiEvExecuteRegMethod (
    ACPI_OPERAND_OBJECT     *RegionObj,
    UINT32                  Function);


/*
 * evregini - Region initialization and setup
 */
ACPI_STATUS
AcpiEvSystemMemoryRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext);

ACPI_STATUS
AcpiEvIoSpaceRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext);

ACPI_STATUS
AcpiEvPciConfigRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext);

ACPI_STATUS
AcpiEvCmosRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext);

ACPI_STATUS
AcpiEvPciBarRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext);

ACPI_STATUS
AcpiEvDefaultRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext);

ACPI_STATUS
AcpiEvInitializeRegion (
    ACPI_OPERAND_OBJECT     *RegionObj);

BOOLEAN
AcpiEvIsPciRootBridge (
    ACPI_NAMESPACE_NODE     *Node);


/*
 * evsci - SCI (System Control Interrupt) handling/dispatch
 */
UINT32 ACPI_SYSTEM_XFACE
AcpiEvGpeXruptHandler (
    void                    *Context);

UINT32
AcpiEvSciDispatch (
    void);

UINT32
AcpiEvInstallSciHandler (
    void);

ACPI_STATUS
AcpiEvRemoveAllSciHandlers (
    void);

ACPI_HW_DEPENDENT_RETURN_VOID (
void
AcpiEvTerminate (
    void))

#endif  /* __ACEVENTS_H__  */
