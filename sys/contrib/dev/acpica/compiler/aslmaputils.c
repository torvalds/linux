/******************************************************************************
 *
 * Module Name: aslmaputils - Utilities for the resource descriptor/device maps
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
#include <contrib/dev/acpica/include/acapps.h>
#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include "aslcompiler.y.h"
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/amlcode.h>

/* This module used for application-level code only */

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslmaputils")


/*******************************************************************************
 *
 * FUNCTION:    MpGetHidFromParseTree
 *
 * PARAMETERS:  HidNode             - Node for a _HID object
 *
 * RETURN:      An _HID string value. Automatically converts _HID integers
 *              to strings. Never NULL.
 *
 * DESCRIPTION: Extract a _HID value from the parse tree, not the namespace.
 *              Used when a fully initialized namespace is not available.
 *
 ******************************************************************************/

char *
MpGetHidFromParseTree (
    ACPI_NAMESPACE_NODE     *HidNode)
{
    ACPI_PARSE_OBJECT       *Op;
    ACPI_PARSE_OBJECT       *Arg;
    char                    *HidString;


    Op = HidNode->Op;
    if (!Op)
    {
        /* Object is not resolved, probably an External */

        return ("Unresolved Symbol - referenced but not defined in this table");
    }

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_NAME:

        Arg = Op->Asl.Child;  /* Get the NameSeg/NameString node */
        Arg = Arg->Asl.Next;  /* First peer is the object to be associated with the name */

        switch (Arg->Asl.ParseOpcode)
        {
        case PARSEOP_STRING_LITERAL:

            return (Arg->Asl.Value.String);

        case PARSEOP_INTEGER:

            /* Convert EISAID to a string */

            HidString = UtLocalCacheCalloc (ACPI_EISAID_STRING_SIZE);
            AcpiExEisaIdToString (HidString, Arg->Asl.Value.Integer);
            return (HidString);

        default:

            return ("UNKNOWN");
        }

    default:
        return ("-No HID-");
    }
}


/*******************************************************************************
 *
 * FUNCTION:    MpGetHidValue
 *
 * PARAMETERS:  DeviceNode          - Node for parent device
 *
 * RETURN:      An _HID string value. Automatically converts _HID integers
 *              to strings. Never NULL.
 *
 * DESCRIPTION: Extract _HID value from within a device scope. Does not
 *              actually execute a method, just gets the string or integer
 *              value for the _HID.
 *
 ******************************************************************************/

char *
MpGetHidValue (
    ACPI_NAMESPACE_NODE     *DeviceNode)
{
    ACPI_NAMESPACE_NODE     *HidNode;
    char                    *HidString;
    ACPI_STATUS             Status;


    Status = AcpiNsGetNode (DeviceNode, METHOD_NAME__HID,
        ACPI_NS_NO_UPSEARCH, &HidNode);
    if (ACPI_FAILURE (Status))
    {
        goto ErrorExit;
    }

    /* If only partial namespace, get the _HID from the parse tree */

    if (!HidNode->Object)
    {
        return (MpGetHidFromParseTree (HidNode));
    }

    /* Handle the different _HID flavors */

    switch (HidNode->Type)
    {
    case ACPI_TYPE_STRING:

        return (HidNode->Object->String.Pointer);

    case ACPI_TYPE_INTEGER:

        /* Convert EISAID to a string */

        HidString = UtLocalCacheCalloc (ACPI_EISAID_STRING_SIZE);
        AcpiExEisaIdToString (HidString, HidNode->Object->Integer.Value);
        return (HidString);

    case ACPI_TYPE_METHOD:

        return ("-Method-");

    default:

        FlPrintFile (ASL_FILE_MAP_OUTPUT, "BAD HID TYPE: %u", HidNode->Type);
        break;
    }


ErrorExit:
    return ("-No HID-");
}


/*******************************************************************************
 *
 * FUNCTION:    MpGetHidViaNamestring
 *
 * PARAMETERS:  DeviceName          - Namepath for parent device
 *
 * RETURN:      _HID string. Never NULL.
 *
 * DESCRIPTION: Get a _HID value via a device pathname (instead of just simply
 *              a device node.)
 *
 ******************************************************************************/

char *
MpGetHidViaNamestring (
    char                    *DeviceName)
{
    ACPI_NAMESPACE_NODE     *DeviceNode;
    ACPI_STATUS             Status;


    Status = AcpiNsGetNode (NULL, DeviceName, ACPI_NS_NO_UPSEARCH,
        &DeviceNode);
    if (ACPI_FAILURE (Status))
    {
        goto ErrorExit;
    }

    return (MpGetHidValue (DeviceNode));


ErrorExit:
    return ("-No HID-");
}


/*******************************************************************************
 *
 * FUNCTION:    MpGetParentDeviceHid
 *
 * PARAMETERS:  Op                      - Parse Op to be examined
 *              TargetNode              - Where the field node is returned
 *              ParentDeviceName        - Where the node path is returned
 *
 * RETURN:      _HID string. Never NULL.
 *
 * DESCRIPTION: Find the parent Device or Scope Op, get the full pathname to
 *              the parent, and get the _HID associated with the parent.
 *
 ******************************************************************************/

char *
MpGetParentDeviceHid (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_NAMESPACE_NODE     **TargetNode,
    char                    **ParentDeviceName)
{
    ACPI_NAMESPACE_NODE     *DeviceNode;


    /* Find parent Device() or Scope() Op */

    while (Op &&
        (Op->Asl.AmlOpcode != AML_DEVICE_OP) &&
        (Op->Asl.AmlOpcode != AML_SCOPE_OP))
    {
        Op = Op->Asl.Parent;
    }

    if (!Op)
    {
        FlPrintFile (ASL_FILE_MAP_OUTPUT, " No_Parent_Device ");
        goto ErrorExit;
    }

    /* Get the full pathname to the device and the _HID */

    DeviceNode = Op->Asl.Node;
    if (!DeviceNode)
    {
        FlPrintFile (ASL_FILE_MAP_OUTPUT, " No_Device_Node ");
        goto ErrorExit;
    }

    *ParentDeviceName = AcpiNsGetExternalPathname (DeviceNode);
    return (MpGetHidValue (DeviceNode));


ErrorExit:
    return ("-No HID-");
}


/*******************************************************************************
 *
 * FUNCTION:    MpGetDdnValue
 *
 * PARAMETERS:  DeviceName          - Namepath for parent device
 *
 * RETURN:      _DDN description string. NULL on failure.
 *
 * DESCRIPTION: Execute the _DDN method for the device.
 *
 ******************************************************************************/

char *
MpGetDdnValue (
    char                    *DeviceName)
{
    ACPI_NAMESPACE_NODE     *DeviceNode;
    ACPI_NAMESPACE_NODE     *DdnNode;
    ACPI_STATUS             Status;


    Status = AcpiNsGetNode (NULL, DeviceName, ACPI_NS_NO_UPSEARCH,
        &DeviceNode);
    if (ACPI_FAILURE (Status))
    {
        goto ErrorExit;
    }

    Status = AcpiNsGetNode (DeviceNode, METHOD_NAME__DDN, ACPI_NS_NO_UPSEARCH,
        &DdnNode);
    if (ACPI_FAILURE (Status))
    {
        goto ErrorExit;
    }

    if ((DdnNode->Type != ACPI_TYPE_STRING) ||
        !DdnNode->Object)
    {
        goto ErrorExit;
    }

    return (DdnNode->Object->String.Pointer);


ErrorExit:
    return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    MpGetConnectionInfo
 *
 * PARAMETERS:  Op                      - Parse Op to be examined
 *              PinIndex                - Index into GPIO PinList
 *              TargetNode              - Where the field node is returned
 *              TargetName              - Where the node path is returned
 *
 * RETURN:      A substitute _HID string, indicating that the name is actually
 *              a field. NULL if the Op does not refer to a Connection.
 *
 * DESCRIPTION: Get the Field Unit that corresponds to the PinIndex after
 *              a Connection() invocation.
 *
 ******************************************************************************/

char *
MpGetConnectionInfo (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  PinIndex,
    ACPI_NAMESPACE_NODE     **TargetNode,
    char                    **TargetName)
{
    ACPI_PARSE_OBJECT       *NextOp;
    UINT32                  i;


    /*
     * Handle Connection() here. Find the next named FieldUnit.
     * Note: we look at the ParseOpcode for the compiler, look
     * at the AmlOpcode for the disassembler.
     */
    if ((Op->Asl.AmlOpcode == AML_INT_CONNECTION_OP) ||
        (Op->Asl.ParseOpcode == PARSEOP_CONNECTION))
    {
        /* Find the correct field unit definition */

        NextOp = Op;
        for (i = 0; i <= PinIndex;)
        {
            NextOp = NextOp->Asl.Next;
            while (NextOp &&
                (NextOp->Asl.ParseOpcode != PARSEOP_NAMESEG) &&
                (NextOp->Asl.AmlOpcode != AML_INT_NAMEDFIELD_OP))
            {
                NextOp = NextOp->Asl.Next;
            }

            if (!NextOp)
            {
                return ("UNKNOWN");
            }

            /* Add length of this field to the current pin index */

            if (NextOp->Asl.ParseOpcode == PARSEOP_NAMESEG)
            {
                i += (UINT32) NextOp->Asl.Child->Asl.Value.Integer;
            }
            else /* AML_INT_NAMEDFIELD_OP */
            {
                i += (UINT32) NextOp->Asl.Value.Integer;
            }
        }

        /* Return the node and pathname for the field unit */

        *TargetNode = NextOp->Asl.Node;
        *TargetName = AcpiNsGetExternalPathname (*TargetNode);
        return ("-Field-");
    }

    return (NULL);
}
