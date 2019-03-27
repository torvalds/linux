/******************************************************************************
 *
 * Module Name: evrgnini- ACPI AddressSpace (OpRegion) init
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
#include <contrib/dev/acpica/include/acinterp.h>

#define _COMPONENT          ACPI_EVENTS
        ACPI_MODULE_NAME    ("evrgnini")


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvSystemMemoryRegionSetup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              HandlerContext      - Address space handler context
 *              RegionContext       - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a SystemMemory operation region
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvSystemMemoryRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext)
{
    ACPI_OPERAND_OBJECT     *RegionDesc = (ACPI_OPERAND_OBJECT *) Handle;
    ACPI_MEM_SPACE_CONTEXT  *LocalRegionContext;


    ACPI_FUNCTION_TRACE (EvSystemMemoryRegionSetup);


    if (Function == ACPI_REGION_DEACTIVATE)
    {
        if (*RegionContext)
        {
            LocalRegionContext = (ACPI_MEM_SPACE_CONTEXT *) *RegionContext;

            /* Delete a cached mapping if present */

            if (LocalRegionContext->MappedLength)
            {
                AcpiOsUnmapMemory (LocalRegionContext->MappedLogicalAddress,
                    LocalRegionContext->MappedLength);
            }
            ACPI_FREE (LocalRegionContext);
            *RegionContext = NULL;
        }
        return_ACPI_STATUS (AE_OK);
    }

    /* Create a new context */

    LocalRegionContext = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_MEM_SPACE_CONTEXT));
    if (!(LocalRegionContext))
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Save the region length and address for use in the handler */

    LocalRegionContext->Length  = RegionDesc->Region.Length;
    LocalRegionContext->Address = RegionDesc->Region.Address;

    *RegionContext = LocalRegionContext;
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvIoSpaceRegionSetup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              HandlerContext      - Address space handler context
 *              RegionContext       - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a IO operation region
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvIoSpaceRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext)
{
    ACPI_FUNCTION_TRACE (EvIoSpaceRegionSetup);


    if (Function == ACPI_REGION_DEACTIVATE)
    {
        *RegionContext = NULL;
    }
    else
    {
        *RegionContext = HandlerContext;
    }

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvPciConfigRegionSetup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              HandlerContext      - Address space handler context
 *              RegionContext       - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a PCI_Config operation region
 *
 * MUTEX:       Assumes namespace is not locked
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvPciConfigRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext)
{
    ACPI_STATUS             Status = AE_OK;
    UINT64                  PciValue;
    ACPI_PCI_ID             *PciId = *RegionContext;
    ACPI_OPERAND_OBJECT     *HandlerObj;
    ACPI_NAMESPACE_NODE     *ParentNode;
    ACPI_NAMESPACE_NODE     *PciRootNode;
    ACPI_NAMESPACE_NODE     *PciDeviceNode;
    ACPI_OPERAND_OBJECT     *RegionObj = (ACPI_OPERAND_OBJECT  *) Handle;


    ACPI_FUNCTION_TRACE (EvPciConfigRegionSetup);


    HandlerObj = RegionObj->Region.Handler;
    if (!HandlerObj)
    {
        /*
         * No installed handler. This shouldn't happen because the dispatch
         * routine checks before we get here, but we check again just in case.
         */
        ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
            "Attempting to init a region %p, with no handler\n", RegionObj));
        return_ACPI_STATUS (AE_NOT_EXIST);
    }

    *RegionContext = NULL;
    if (Function == ACPI_REGION_DEACTIVATE)
    {
        if (PciId)
        {
            ACPI_FREE (PciId);
        }
        return_ACPI_STATUS (Status);
    }

    ParentNode = RegionObj->Region.Node->Parent;

    /*
     * Get the _SEG and _BBN values from the device upon which the handler
     * is installed.
     *
     * We need to get the _SEG and _BBN objects relative to the PCI BUS device.
     * This is the device the handler has been registered to handle.
     */

    /*
     * If the AddressSpace.Node is still pointing to the root, we need
     * to scan upward for a PCI Root bridge and re-associate the OpRegion
     * handlers with that device.
     */
    if (HandlerObj->AddressSpace.Node == AcpiGbl_RootNode)
    {
        /* Start search from the parent object */

        PciRootNode = ParentNode;
        while (PciRootNode != AcpiGbl_RootNode)
        {
            /* Get the _HID/_CID in order to detect a RootBridge */

            if (AcpiEvIsPciRootBridge (PciRootNode))
            {
                /* Install a handler for this PCI root bridge */

                Status = AcpiInstallAddressSpaceHandler (
                    (ACPI_HANDLE) PciRootNode,
                    ACPI_ADR_SPACE_PCI_CONFIG,
                    ACPI_DEFAULT_HANDLER, NULL, NULL);
                if (ACPI_FAILURE (Status))
                {
                    if (Status == AE_SAME_HANDLER)
                    {
                        /*
                         * It is OK if the handler is already installed on the
                         * root bridge. Still need to return a context object
                         * for the new PCI_Config operation region, however.
                         */
                        Status = AE_OK;
                    }
                    else
                    {
                        ACPI_EXCEPTION ((AE_INFO, Status,
                            "Could not install PciConfig handler "
                            "for Root Bridge %4.4s",
                            AcpiUtGetNodeName (PciRootNode)));
                    }
                }
                break;
            }

            PciRootNode = PciRootNode->Parent;
        }

        /* PCI root bridge not found, use namespace root node */
    }
    else
    {
        PciRootNode = HandlerObj->AddressSpace.Node;
    }

    /*
     * If this region is now initialized, we are done.
     * (InstallAddressSpaceHandler could have initialized it)
     */
    if (RegionObj->Region.Flags & AOPOBJ_SETUP_COMPLETE)
    {
        return_ACPI_STATUS (AE_OK);
    }

    /* Region is still not initialized. Create a new context */

    PciId = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_PCI_ID));
    if (!PciId)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /*
     * For PCI_Config space access, we need the segment, bus, device and
     * function numbers. Acquire them here.
     *
     * Find the parent device object. (This allows the operation region to be
     * within a subscope under the device, such as a control method.)
     */
    PciDeviceNode = RegionObj->Region.Node;
    while (PciDeviceNode && (PciDeviceNode->Type != ACPI_TYPE_DEVICE))
    {
        PciDeviceNode = PciDeviceNode->Parent;
    }

    if (!PciDeviceNode)
    {
        ACPI_FREE (PciId);
        return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
    }

    /*
     * Get the PCI device and function numbers from the _ADR object
     * contained in the parent's scope.
     */
    Status = AcpiUtEvaluateNumericObject (METHOD_NAME__ADR,
        PciDeviceNode, &PciValue);

    /*
     * The default is zero, and since the allocation above zeroed the data,
     * just do nothing on failure.
     */
    if (ACPI_SUCCESS (Status))
    {
        PciId->Device   = ACPI_HIWORD (ACPI_LODWORD (PciValue));
        PciId->Function = ACPI_LOWORD (ACPI_LODWORD (PciValue));
    }

    /* The PCI segment number comes from the _SEG method */

    Status = AcpiUtEvaluateNumericObject (METHOD_NAME__SEG,
        PciRootNode, &PciValue);
    if (ACPI_SUCCESS (Status))
    {
        PciId->Segment = ACPI_LOWORD (PciValue);
    }

    /* The PCI bus number comes from the _BBN method */

    Status = AcpiUtEvaluateNumericObject (METHOD_NAME__BBN,
        PciRootNode, &PciValue);
    if (ACPI_SUCCESS (Status))
    {
        PciId->Bus = ACPI_LOWORD (PciValue);
    }

    /* Complete/update the PCI ID for this device */

    Status = AcpiHwDerivePciId (PciId, PciRootNode, RegionObj->Region.Node);
    if (ACPI_FAILURE (Status))
    {
        ACPI_FREE (PciId);
        return_ACPI_STATUS (Status);
    }

    *RegionContext = PciId;
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvIsPciRootBridge
 *
 * PARAMETERS:  Node            - Device node being examined
 *
 * RETURN:      TRUE if device is a PCI/PCI-Express Root Bridge
 *
 * DESCRIPTION: Determine if the input device represents a PCI Root Bridge by
 *              examining the _HID and _CID for the device.
 *
 ******************************************************************************/

BOOLEAN
AcpiEvIsPciRootBridge (
    ACPI_NAMESPACE_NODE     *Node)
{
    ACPI_STATUS             Status;
    ACPI_PNP_DEVICE_ID      *Hid;
    ACPI_PNP_DEVICE_ID_LIST *Cid;
    UINT32                  i;
    BOOLEAN                 Match;


    /* Get the _HID and check for a PCI Root Bridge */

    Status = AcpiUtExecute_HID (Node, &Hid);
    if (ACPI_FAILURE (Status))
    {
        return (FALSE);
    }

    Match = AcpiUtIsPciRootBridge (Hid->String);
    ACPI_FREE (Hid);

    if (Match)
    {
        return (TRUE);
    }

    /* The _HID did not match. Get the _CID and check for a PCI Root Bridge */

    Status = AcpiUtExecute_CID (Node, &Cid);
    if (ACPI_FAILURE (Status))
    {
        return (FALSE);
    }

    /* Check all _CIDs in the returned list */

    for (i = 0; i < Cid->Count; i++)
    {
        if (AcpiUtIsPciRootBridge (Cid->Ids[i].String))
        {
            ACPI_FREE (Cid);
            return (TRUE);
        }
    }

    ACPI_FREE (Cid);
    return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvPciBarRegionSetup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              HandlerContext      - Address space handler context
 *              RegionContext       - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a PciBAR operation region
 *
 * MUTEX:       Assumes namespace is not locked
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvPciBarRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext)
{
    ACPI_FUNCTION_TRACE (EvPciBarRegionSetup);


    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvCmosRegionSetup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              HandlerContext      - Address space handler context
 *              RegionContext       - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a CMOS operation region
 *
 * MUTEX:       Assumes namespace is not locked
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvCmosRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext)
{
    ACPI_FUNCTION_TRACE (EvCmosRegionSetup);


    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvDefaultRegionSetup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              HandlerContext      - Address space handler context
 *              RegionContext       - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Default region initialization
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvDefaultRegionSetup (
    ACPI_HANDLE             Handle,
    UINT32                  Function,
    void                    *HandlerContext,
    void                    **RegionContext)
{
    ACPI_FUNCTION_TRACE (EvDefaultRegionSetup);


    if (Function == ACPI_REGION_DEACTIVATE)
    {
        *RegionContext = NULL;
    }
    else
    {
        *RegionContext = HandlerContext;
    }

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvInitializeRegion
 *
 * PARAMETERS:  RegionObj       - Region we are initializing
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes the region, finds any _REG methods and saves them
 *              for execution at a later time
 *
 *              Get the appropriate address space handler for a newly
 *              created region.
 *
 *              This also performs address space specific initialization. For
 *              example, PCI regions must have an _ADR object that contains
 *              a PCI address in the scope of the definition. This address is
 *              required to perform an access to PCI config space.
 *
 * MUTEX:       Interpreter should be unlocked, because we may run the _REG
 *              method for this region.
 *
 * NOTE:        Possible incompliance:
 *              There is a behavior conflict in automatic _REG execution:
 *              1. When the interpreter is evaluating a method, we can only
 *                 automatically run _REG for the following case:
 *                   Method(_REG, 2) {}
 *                   OperationRegion (OPR1, 0x80, 0x1000010, 0x4)
 *              2. When the interpreter is loading a table, we can also
 *                 automatically run _REG for the following case:
 *                   OperationRegion (OPR1, 0x80, 0x1000010, 0x4)
 *                   Method(_REG, 2) {}
 *              Though this may not be compliant to the de-facto standard, the
 *              logic is kept in order not to trigger regressions. And keeping
 *              this logic should be taken care by the caller of this function.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvInitializeRegion (
    ACPI_OPERAND_OBJECT     *RegionObj)
{
    ACPI_OPERAND_OBJECT     *HandlerObj;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_ADR_SPACE_TYPE     SpaceId;
    ACPI_NAMESPACE_NODE     *Node;


    ACPI_FUNCTION_TRACE (EvInitializeRegion);


    if (!RegionObj)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    if (RegionObj->Common.Flags & AOPOBJ_OBJECT_INITIALIZED)
    {
        return_ACPI_STATUS (AE_OK);
    }

    RegionObj->Common.Flags |= AOPOBJ_OBJECT_INITIALIZED;

    Node = RegionObj->Region.Node->Parent;
    SpaceId = RegionObj->Region.SpaceId;

    /*
     * The following loop depends upon the root Node having no parent
     * ie: AcpiGbl_RootNode->Parent being set to NULL
     */
    while (Node)
    {
        /* Check to see if a handler exists */

        HandlerObj = NULL;
        ObjDesc = AcpiNsGetAttachedObject (Node);
        if (ObjDesc)
        {
            /* Can only be a handler if the object exists */

            switch (Node->Type)
            {
            case ACPI_TYPE_DEVICE:
            case ACPI_TYPE_PROCESSOR:
            case ACPI_TYPE_THERMAL:

                HandlerObj = ObjDesc->CommonNotify.Handler;
                break;

            default:

                /* Ignore other objects */

                break;
            }

            HandlerObj = AcpiEvFindRegionHandler (SpaceId, HandlerObj);
            if (HandlerObj)
            {
                /* Found correct handler */

                ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
                    "Found handler %p for region %p in obj %p\n",
                    HandlerObj, RegionObj, ObjDesc));

                (void) AcpiEvAttachRegion (HandlerObj, RegionObj, FALSE);

                /*
                 * Tell all users that this region is usable by
                 * running the _REG method
                 */
                AcpiExExitInterpreter ();
                (void) AcpiEvExecuteRegMethod (RegionObj, ACPI_REG_CONNECT);
                AcpiExEnterInterpreter ();
                return_ACPI_STATUS (AE_OK);
            }
        }

        /* This node does not have the handler we need; Pop up one level */

        Node = Node->Parent;
    }

    /*
     * If we get here, there is no handler for this region. This is not
     * fatal because many regions get created before a handler is installed
     * for said region.
     */
    ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
        "No handler for RegionType %s(%X) (RegionObj %p)\n",
        AcpiUtGetRegionName (SpaceId), SpaceId, RegionObj));

    return_ACPI_STATUS (AE_OK);
}
