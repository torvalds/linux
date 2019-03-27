/******************************************************************************
 *
 * Module Name: nsinit - namespace initialization
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
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acevents.h>

#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nsinit")

/* Local prototypes */

static ACPI_STATUS
AcpiNsInitOneObject (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiNsInitOneDevice (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiNsFindIniMethods (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsInitializeObjects
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the entire namespace and perform any necessary
 *              initialization on the objects found therein
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsInitializeObjects (
    void)
{
    ACPI_STATUS             Status;
    ACPI_INIT_WALK_INFO     Info;


    ACPI_FUNCTION_TRACE (NsInitializeObjects);


    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
        "[Init] Completing Initialization of ACPI Objects\n"));
    ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
        "**** Starting initialization of namespace objects ****\n"));
    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
        "Completing Region/Field/Buffer/Package initialization:\n"));

    /* Set all init info to zero */

    memset (&Info, 0, sizeof (ACPI_INIT_WALK_INFO));

    /* Walk entire namespace from the supplied root */

    Status = AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, AcpiNsInitOneObject, NULL,
        &Info, NULL);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "During WalkNamespace"));
    }

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
        "    Initialized %u/%u Regions %u/%u Fields %u/%u "
        "Buffers %u/%u Packages (%u nodes)\n",
        Info.OpRegionInit,  Info.OpRegionCount,
        Info.FieldInit,     Info.FieldCount,
        Info.BufferInit,    Info.BufferCount,
        Info.PackageInit,   Info.PackageCount, Info.ObjectCount));

    ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
        "%u Control Methods found\n%u Op Regions found\n",
        Info.MethodCount, Info.OpRegionCount));

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsInitializeDevices
 *
 * PARAMETERS:  None
 *
 * RETURN:      ACPI_STATUS
 *
 * DESCRIPTION: Walk the entire namespace and initialize all ACPI devices.
 *              This means running _INI on all present devices.
 *
 *              Note: We install PCI config space handler on region access,
 *              not here.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsInitializeDevices (
    UINT32                  Flags)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_DEVICE_WALK_INFO   Info;
    ACPI_HANDLE             Handle;


    ACPI_FUNCTION_TRACE (NsInitializeDevices);


    if (!(Flags & ACPI_NO_DEVICE_INIT))
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "[Init] Initializing ACPI Devices\n"));

        /* Init counters */

        Info.DeviceCount = 0;
        Info.Num_STA = 0;
        Info.Num_INI = 0;

        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
            "Initializing Device/Processor/Thermal objects "
            "and executing _INI/_STA methods:\n"));

        /* Tree analysis: find all subtrees that contain _INI methods */

        Status = AcpiNsWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
            ACPI_UINT32_MAX, FALSE, AcpiNsFindIniMethods, NULL, &Info, NULL);
        if (ACPI_FAILURE (Status))
        {
            goto ErrorExit;
        }

        /* Allocate the evaluation information block */

        Info.EvaluateInfo = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_EVALUATE_INFO));
        if (!Info.EvaluateInfo)
        {
            Status = AE_NO_MEMORY;
            goto ErrorExit;
        }

        /*
         * Execute the "global" _INI method that may appear at the root.
         * This support is provided for Windows compatibility (Vista+) and
         * is not part of the ACPI specification.
         */
        Info.EvaluateInfo->PrefixNode = AcpiGbl_RootNode;
        Info.EvaluateInfo->RelativePathname = METHOD_NAME__INI;
        Info.EvaluateInfo->Parameters = NULL;
        Info.EvaluateInfo->Flags = ACPI_IGNORE_RETURN_VALUE;

        Status = AcpiNsEvaluate (Info.EvaluateInfo);
        if (ACPI_SUCCESS (Status))
        {
            Info.Num_INI++;
        }

        /*
         * Execute \_SB._INI.
         * There appears to be a strict order requirement for \_SB._INI,
         * which should be evaluated before any _REG evaluations.
         */
        Status = AcpiGetHandle (NULL, "\\_SB", &Handle);
        if (ACPI_SUCCESS (Status))
        {
            memset (Info.EvaluateInfo, 0, sizeof (ACPI_EVALUATE_INFO));
            Info.EvaluateInfo->PrefixNode = Handle;
            Info.EvaluateInfo->RelativePathname = METHOD_NAME__INI;
            Info.EvaluateInfo->Parameters = NULL;
            Info.EvaluateInfo->Flags = ACPI_IGNORE_RETURN_VALUE;

            Status = AcpiNsEvaluate (Info.EvaluateInfo);
            if (ACPI_SUCCESS (Status))
            {
                Info.Num_INI++;
            }
        }
    }

    /*
     * Run all _REG methods
     *
     * Note: Any objects accessed by the _REG methods will be automatically
     * initialized, even if they contain executable AML (see the call to
     * AcpiNsInitializeObjects below).
     *
     * Note: According to the ACPI specification, we actually needn't execute
     * _REG for SystemMemory/SystemIo operation regions, but for PCI_Config
     * operation regions, it is required to evaluate _REG for those on a PCI
     * root bus that doesn't contain _BBN object. So this code is kept here
     * in order not to break things.
     */
    if (!(Flags & ACPI_NO_ADDRESS_SPACE_INIT))
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "[Init] Executing _REG OpRegion methods\n"));

        Status = AcpiEvInitializeOpRegions ();
        if (ACPI_FAILURE (Status))
        {
            goto ErrorExit;
        }
    }

    if (!(Flags & ACPI_NO_DEVICE_INIT))
    {
        /* Walk namespace to execute all _INIs on present devices */

        Status = AcpiNsWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
            ACPI_UINT32_MAX, FALSE, AcpiNsInitOneDevice, NULL, &Info, NULL);

        /*
         * Any _OSI requests should be completed by now. If the BIOS has
         * requested any Windows OSI strings, we will always truncate
         * I/O addresses to 16 bits -- for Windows compatibility.
         */
        if (AcpiGbl_OsiData >= ACPI_OSI_WIN_2000)
        {
            AcpiGbl_TruncateIoAddresses = TRUE;
        }

        ACPI_FREE (Info.EvaluateInfo);
        if (ACPI_FAILURE (Status))
        {
            goto ErrorExit;
        }

        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
            "    Executed %u _INI methods requiring %u _STA executions "
            "(examined %u objects)\n",
            Info.Num_INI, Info.Num_STA, Info.DeviceCount));
    }

    return_ACPI_STATUS (Status);


ErrorExit:
    ACPI_EXCEPTION ((AE_INFO, Status, "During device initialization"));
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsInitOnePackage
 *
 * PARAMETERS:  ObjHandle       - Node
 *              Level           - Current nesting level
 *              Context         - Not used
 *              ReturnValue     - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from AcpiWalkNamespace. Invoked for every package
 *              within the namespace. Used during dynamic load of an SSDT.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsInitOnePackage (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;


    ObjDesc = AcpiNsGetAttachedObject (Node);
    if (!ObjDesc)
    {
        return (AE_OK);
    }

    /* Exit if package is already initialized */

    if (ObjDesc->Package.Flags & AOPOBJ_DATA_VALID)
    {
        return (AE_OK);
    }

    Status = AcpiDsGetPackageArguments (ObjDesc);
    if (ACPI_FAILURE (Status))
    {
        return (AE_OK);
    }

    Status = AcpiUtWalkPackageTree (ObjDesc, NULL, AcpiDsInitPackageElement,
        NULL);
    if (ACPI_FAILURE (Status))
    {
        return (AE_OK);
    }

    ObjDesc->Package.Flags |= AOPOBJ_DATA_VALID;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsInitOneObject
 *
 * PARAMETERS:  ObjHandle       - Node
 *              Level           - Current nesting level
 *              Context         - Points to a init info struct
 *              ReturnValue     - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from AcpiWalkNamespace. Invoked for every object
 *              within the namespace.
 *
 *              Currently, the only objects that require initialization are:
 *              1) Methods
 *              2) Op Regions
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiNsInitOneObject (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_OBJECT_TYPE        Type;
    ACPI_STATUS             Status = AE_OK;
    ACPI_INIT_WALK_INFO     *Info = (ACPI_INIT_WALK_INFO *) Context;
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;
    ACPI_OPERAND_OBJECT     *ObjDesc;


    ACPI_FUNCTION_NAME (NsInitOneObject);


    Info->ObjectCount++;

    /* And even then, we are only interested in a few object types */

    Type = AcpiNsGetType (ObjHandle);
    ObjDesc = AcpiNsGetAttachedObject (Node);
    if (!ObjDesc)
    {
        return (AE_OK);
    }

    /* Increment counters for object types we are looking for */

    switch (Type)
    {
    case ACPI_TYPE_REGION:

        Info->OpRegionCount++;
        break;

    case ACPI_TYPE_BUFFER_FIELD:

        Info->FieldCount++;
        break;

    case ACPI_TYPE_LOCAL_BANK_FIELD:

        Info->FieldCount++;
        break;

    case ACPI_TYPE_BUFFER:

        Info->BufferCount++;
        break;

    case ACPI_TYPE_PACKAGE:

        Info->PackageCount++;
        break;

    default:

        /* No init required, just exit now */

        return (AE_OK);
    }

    /* If the object is already initialized, nothing else to do */

    if (ObjDesc->Common.Flags & AOPOBJ_DATA_VALID)
    {
        return (AE_OK);
    }

    /* Must lock the interpreter before executing AML code */

    AcpiExEnterInterpreter ();

    /*
     * Each of these types can contain executable AML code within the
     * declaration.
     */
    switch (Type)
    {
    case ACPI_TYPE_REGION:

        Info->OpRegionInit++;
        Status = AcpiDsGetRegionArguments (ObjDesc);
        break;

    case ACPI_TYPE_BUFFER_FIELD:

        Info->FieldInit++;
        Status = AcpiDsGetBufferFieldArguments (ObjDesc);
        break;

    case ACPI_TYPE_LOCAL_BANK_FIELD:

        Info->FieldInit++;
        Status = AcpiDsGetBankFieldArguments (ObjDesc);
        break;

    case ACPI_TYPE_BUFFER:

        Info->BufferInit++;
        Status = AcpiDsGetBufferArguments (ObjDesc);
        break;

    case ACPI_TYPE_PACKAGE:

        /* Complete the initialization/resolution of the package object */

        Info->PackageInit++;
        Status = AcpiNsInitOnePackage (ObjHandle, Level, NULL, NULL);
        break;

    default:

        /* No other types can get here */

        break;
    }

    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status,
            "Could not execute arguments for [%4.4s] (%s)",
            AcpiUtGetNodeName (Node), AcpiUtGetTypeName (Type)));
    }

    /*
     * We ignore errors from above, and always return OK, since we don't want
     * to abort the walk on any single error.
     */
    AcpiExExitInterpreter ();
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsFindIniMethods
 *
 * PARAMETERS:  ACPI_WALK_CALLBACK
 *
 * RETURN:      ACPI_STATUS
 *
 * DESCRIPTION: Called during namespace walk. Finds objects named _INI under
 *              device/processor/thermal objects, and marks the entire subtree
 *              with a SUBTREE_HAS_INI flag. This flag is used during the
 *              subsequent device initialization walk to avoid entire subtrees
 *              that do not contain an _INI.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiNsFindIniMethods (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_DEVICE_WALK_INFO   *Info = ACPI_CAST_PTR (ACPI_DEVICE_WALK_INFO, Context);
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_NAMESPACE_NODE     *ParentNode;


    /* Keep count of device/processor/thermal objects */

    Node = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, ObjHandle);
    if ((Node->Type == ACPI_TYPE_DEVICE)    ||
        (Node->Type == ACPI_TYPE_PROCESSOR) ||
        (Node->Type == ACPI_TYPE_THERMAL))
    {
        Info->DeviceCount++;
        return (AE_OK);
    }

    /* We are only looking for methods named _INI */

    if (!ACPI_COMPARE_NAME (Node->Name.Ascii, METHOD_NAME__INI))
    {
        return (AE_OK);
    }

    /*
     * The only _INI methods that we care about are those that are
     * present under Device, Processor, and Thermal objects.
     */
    ParentNode = Node->Parent;
    switch (ParentNode->Type)
    {
    case ACPI_TYPE_DEVICE:
    case ACPI_TYPE_PROCESSOR:
    case ACPI_TYPE_THERMAL:

        /* Mark parent and bubble up the INI present flag to the root */

        while (ParentNode)
        {
            ParentNode->Flags |= ANOBJ_SUBTREE_HAS_INI;
            ParentNode = ParentNode->Parent;
        }
        break;

    default:

        break;
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsInitOneDevice
 *
 * PARAMETERS:  ACPI_WALK_CALLBACK
 *
 * RETURN:      ACPI_STATUS
 *
 * DESCRIPTION: This is called once per device soon after ACPI is enabled
 *              to initialize each device. It determines if the device is
 *              present, and if so, calls _INI.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiNsInitOneDevice (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_DEVICE_WALK_INFO   *WalkInfo = ACPI_CAST_PTR (ACPI_DEVICE_WALK_INFO, Context);
    ACPI_EVALUATE_INFO      *Info = WalkInfo->EvaluateInfo;
    UINT32                  Flags;
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *DeviceNode;


    ACPI_FUNCTION_TRACE (NsInitOneDevice);


    /* We are interested in Devices, Processors and ThermalZones only */

    DeviceNode = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, ObjHandle);
    if ((DeviceNode->Type != ACPI_TYPE_DEVICE)    &&
        (DeviceNode->Type != ACPI_TYPE_PROCESSOR) &&
        (DeviceNode->Type != ACPI_TYPE_THERMAL))
    {
        return_ACPI_STATUS (AE_OK);
    }

    /*
     * Because of an earlier namespace analysis, all subtrees that contain an
     * _INI method are tagged.
     *
     * If this device subtree does not contain any _INI methods, we
     * can exit now and stop traversing this entire subtree.
     */
    if (!(DeviceNode->Flags & ANOBJ_SUBTREE_HAS_INI))
    {
        return_ACPI_STATUS (AE_CTRL_DEPTH);
    }

    /*
     * Run _STA to determine if this device is present and functioning. We
     * must know this information for two important reasons (from ACPI spec):
     *
     * 1) We can only run _INI if the device is present.
     * 2) We must abort the device tree walk on this subtree if the device is
     *    not present and is not functional (we will not examine the children)
     *
     * The _STA method is not required to be present under the device, we
     * assume the device is present if _STA does not exist.
     */
    ACPI_DEBUG_EXEC (AcpiUtDisplayInitPathname (
        ACPI_TYPE_METHOD, DeviceNode, METHOD_NAME__STA));

    Status = AcpiUtExecute_STA (DeviceNode, &Flags);
    if (ACPI_FAILURE (Status))
    {
        /* Ignore error and move on to next device */

        return_ACPI_STATUS (AE_OK);
    }

    /*
     * Flags == -1 means that _STA was not found. In this case, we assume that
     * the device is both present and functional.
     *
     * From the ACPI spec, description of _STA:
     *
     * "If a device object (including the processor object) does not have an
     * _STA object, then OSPM assumes that all of the above bits are set (in
     * other words, the device is present, ..., and functioning)"
     */
    if (Flags != ACPI_UINT32_MAX)
    {
        WalkInfo->Num_STA++;
    }

    /*
     * Examine the PRESENT and FUNCTIONING status bits
     *
     * Note: ACPI spec does not seem to specify behavior for the present but
     * not functioning case, so we assume functioning if present.
     */
    if (!(Flags & ACPI_STA_DEVICE_PRESENT))
    {
        /* Device is not present, we must examine the Functioning bit */

        if (Flags & ACPI_STA_DEVICE_FUNCTIONING)
        {
            /*
             * Device is not present but is "functioning". In this case,
             * we will not run _INI, but we continue to examine the children
             * of this device.
             *
             * From the ACPI spec, description of _STA: (Note - no mention
             * of whether to run _INI or not on the device in question)
             *
             * "_STA may return bit 0 clear (not present) with bit 3 set
             * (device is functional). This case is used to indicate a valid
             * device for which no device driver should be loaded (for example,
             * a bridge device.) Children of this device may be present and
             * valid. OSPM should continue enumeration below a device whose
             * _STA returns this bit combination"
             */
            return_ACPI_STATUS (AE_OK);
        }
        else
        {
            /*
             * Device is not present and is not functioning. We must abort the
             * walk of this subtree immediately -- don't look at the children
             * of such a device.
             *
             * From the ACPI spec, description of _INI:
             *
             * "If the _STA method indicates that the device is not present,
             * OSPM will not run the _INI and will not examine the children
             * of the device for _INI methods"
             */
            return_ACPI_STATUS (AE_CTRL_DEPTH);
        }
    }

    /*
     * The device is present or is assumed present if no _STA exists.
     * Run the _INI if it exists (not required to exist)
     *
     * Note: We know there is an _INI within this subtree, but it may not be
     * under this particular device, it may be lower in the branch.
     */
    if (!ACPI_COMPARE_NAME (DeviceNode->Name.Ascii, "_SB_") ||
        DeviceNode->Parent != AcpiGbl_RootNode)
    {
        ACPI_DEBUG_EXEC (AcpiUtDisplayInitPathname (
            ACPI_TYPE_METHOD, DeviceNode, METHOD_NAME__INI));

        memset (Info, 0, sizeof (ACPI_EVALUATE_INFO));
        Info->PrefixNode = DeviceNode;
        Info->RelativePathname = METHOD_NAME__INI;
        Info->Parameters = NULL;
        Info->Flags = ACPI_IGNORE_RETURN_VALUE;

        Status = AcpiNsEvaluate (Info);
        if (ACPI_SUCCESS (Status))
        {
            WalkInfo->Num_INI++;
        }

#ifdef ACPI_DEBUG_OUTPUT
        else if (Status != AE_NOT_FOUND)
        {
            /* Ignore error and move on to next device */

            char *ScopeName = AcpiNsGetNormalizedPathname (DeviceNode, TRUE);

            ACPI_EXCEPTION ((AE_INFO, Status, "during %s._INI execution",
                ScopeName));
            ACPI_FREE (ScopeName);
        }
#endif
    }

    /* Ignore errors from above */

    Status = AE_OK;

    /*
     * The _INI method has been run if present; call the Global Initialization
     * Handler for this device.
     */
    if (AcpiGbl_InitHandler)
    {
        Status = AcpiGbl_InitHandler (DeviceNode, ACPI_INIT_DEVICE_INI);
    }

    return_ACPI_STATUS (Status);
}
