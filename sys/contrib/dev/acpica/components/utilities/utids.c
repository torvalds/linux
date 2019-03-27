/******************************************************************************
 *
 * Module Name: utids - support for device IDs - HID, UID, CID, SUB, CLS
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
#include <contrib/dev/acpica/include/acinterp.h>


#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utids")


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtExecute_HID
 *
 * PARAMETERS:  DeviceNode          - Node for the device
 *              ReturnId            - Where the string HID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _HID control method that returns the hardware
 *              ID of the device. The HID is either an 32-bit encoded EISAID
 *              Integer or a String. A string is always returned. An EISAID
 *              is converted to a string.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtExecute_HID (
    ACPI_NAMESPACE_NODE     *DeviceNode,
    ACPI_PNP_DEVICE_ID      **ReturnId)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_PNP_DEVICE_ID      *Hid;
    UINT32                  Length;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (UtExecute_HID);


    Status = AcpiUtEvaluateObject (DeviceNode, METHOD_NAME__HID,
        ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING, &ObjDesc);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Get the size of the String to be returned, includes null terminator */

    if (ObjDesc->Common.Type == ACPI_TYPE_INTEGER)
    {
        Length = ACPI_EISAID_STRING_SIZE;
    }
    else
    {
        Length = ObjDesc->String.Length + 1;
    }

    /* Allocate a buffer for the HID */

    Hid = ACPI_ALLOCATE_ZEROED (
        sizeof (ACPI_PNP_DEVICE_ID) + (ACPI_SIZE) Length);
    if (!Hid)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /* Area for the string starts after PNP_DEVICE_ID struct */

    Hid->String = ACPI_ADD_PTR (char, Hid, sizeof (ACPI_PNP_DEVICE_ID));

    /* Convert EISAID to a string or simply copy existing string */

    if (ObjDesc->Common.Type == ACPI_TYPE_INTEGER)
    {
        AcpiExEisaIdToString (Hid->String, ObjDesc->Integer.Value);
    }
    else
    {
        strcpy (Hid->String, ObjDesc->String.Pointer);
    }

    Hid->Length = Length;
    *ReturnId = Hid;


Cleanup:

    /* On exit, we must delete the return object */

    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtExecute_UID
 *
 * PARAMETERS:  DeviceNode          - Node for the device
 *              ReturnId            - Where the string UID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _UID control method that returns the unique
 *              ID of the device. The UID is either a 64-bit Integer (NOT an
 *              EISAID) or a string. Always returns a string. A 64-bit integer
 *              is converted to a decimal string.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtExecute_UID (
    ACPI_NAMESPACE_NODE     *DeviceNode,
    ACPI_PNP_DEVICE_ID      **ReturnId)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_PNP_DEVICE_ID      *Uid;
    UINT32                  Length;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (UtExecute_UID);


    Status = AcpiUtEvaluateObject (DeviceNode, METHOD_NAME__UID,
        ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING, &ObjDesc);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Get the size of the String to be returned, includes null terminator */

    if (ObjDesc->Common.Type == ACPI_TYPE_INTEGER)
    {
        Length = ACPI_MAX64_DECIMAL_DIGITS + 1;
    }
    else
    {
        Length = ObjDesc->String.Length + 1;
    }

    /* Allocate a buffer for the UID */

    Uid = ACPI_ALLOCATE_ZEROED (
        sizeof (ACPI_PNP_DEVICE_ID) + (ACPI_SIZE) Length);
    if (!Uid)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /* Area for the string starts after PNP_DEVICE_ID struct */

    Uid->String = ACPI_ADD_PTR (char, Uid, sizeof (ACPI_PNP_DEVICE_ID));

    /* Convert an Integer to string, or just copy an existing string */

    if (ObjDesc->Common.Type == ACPI_TYPE_INTEGER)
    {
        AcpiExIntegerToString (Uid->String, ObjDesc->Integer.Value);
    }
    else
    {
        strcpy (Uid->String, ObjDesc->String.Pointer);
    }

    Uid->Length = Length;
    *ReturnId = Uid;


Cleanup:

    /* On exit, we must delete the return object */

    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtExecute_CID
 *
 * PARAMETERS:  DeviceNode          - Node for the device
 *              ReturnCidList       - Where the CID list is returned
 *
 * RETURN:      Status, list of CID strings
 *
 * DESCRIPTION: Executes the _CID control method that returns one or more
 *              compatible hardware IDs for the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 * A _CID method can return either a single compatible ID or a package of
 * compatible IDs. Each compatible ID can be one of the following:
 * 1) Integer (32 bit compressed EISA ID) or
 * 2) String (PCI ID format, e.g. "PCI\VEN_vvvv&DEV_dddd&SUBSYS_ssssssss")
 *
 * The Integer CIDs are converted to string format by this function.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtExecute_CID (
    ACPI_NAMESPACE_NODE     *DeviceNode,
    ACPI_PNP_DEVICE_ID_LIST **ReturnCidList)
{
    ACPI_OPERAND_OBJECT     **CidObjects;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_PNP_DEVICE_ID_LIST *CidList;
    char                    *NextIdString;
    UINT32                  StringAreaSize;
    UINT32                  Length;
    UINT32                  CidListSize;
    ACPI_STATUS             Status;
    UINT32                  Count;
    UINT32                  i;


    ACPI_FUNCTION_TRACE (UtExecute_CID);


    /* Evaluate the _CID method for this device */

    Status = AcpiUtEvaluateObject (DeviceNode, METHOD_NAME__CID,
        ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING | ACPI_BTYPE_PACKAGE,
        &ObjDesc);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /*
     * Get the count and size of the returned _CIDs. _CID can return either
     * a Package of Integers/Strings or a single Integer or String.
     * Note: This section also validates that all CID elements are of the
     * correct type (Integer or String).
     */
    if (ObjDesc->Common.Type == ACPI_TYPE_PACKAGE)
    {
        Count = ObjDesc->Package.Count;
        CidObjects = ObjDesc->Package.Elements;
    }
    else /* Single Integer or String CID */
    {
        Count = 1;
        CidObjects = &ObjDesc;
    }

    StringAreaSize = 0;
    for (i = 0; i < Count; i++)
    {
        /* String lengths include null terminator */

        switch (CidObjects[i]->Common.Type)
        {
        case ACPI_TYPE_INTEGER:

            StringAreaSize += ACPI_EISAID_STRING_SIZE;
            break;

        case ACPI_TYPE_STRING:

            StringAreaSize += CidObjects[i]->String.Length + 1;
            break;

        default:

            Status = AE_TYPE;
            goto Cleanup;
        }
    }

    /*
     * Now that we know the length of the CIDs, allocate return buffer:
     * 1) Size of the base structure +
     * 2) Size of the CID PNP_DEVICE_ID array +
     * 3) Size of the actual CID strings
     */
    CidListSize = sizeof (ACPI_PNP_DEVICE_ID_LIST) +
        ((Count - 1) * sizeof (ACPI_PNP_DEVICE_ID)) +
        StringAreaSize;

    CidList = ACPI_ALLOCATE_ZEROED (CidListSize);
    if (!CidList)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /* Area for CID strings starts after the CID PNP_DEVICE_ID array */

    NextIdString = ACPI_CAST_PTR (char, CidList->Ids) +
        ((ACPI_SIZE) Count * sizeof (ACPI_PNP_DEVICE_ID));

    /* Copy/convert the CIDs to the return buffer */

    for (i = 0; i < Count; i++)
    {
        if (CidObjects[i]->Common.Type == ACPI_TYPE_INTEGER)
        {
            /* Convert the Integer (EISAID) CID to a string */

            AcpiExEisaIdToString (
                NextIdString, CidObjects[i]->Integer.Value);
            Length = ACPI_EISAID_STRING_SIZE;
        }
        else /* ACPI_TYPE_STRING */
        {
            /* Copy the String CID from the returned object */

            strcpy (NextIdString, CidObjects[i]->String.Pointer);
            Length = CidObjects[i]->String.Length + 1;
        }

        CidList->Ids[i].String = NextIdString;
        CidList->Ids[i].Length = Length;
        NextIdString += Length;
    }

    /* Finish the CID list */

    CidList->Count = Count;
    CidList->ListSize = CidListSize;
    *ReturnCidList = CidList;


Cleanup:

    /* On exit, we must delete the _CID return object */

    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtExecute_CLS
 *
 * PARAMETERS:  DeviceNode          - Node for the device
 *              ReturnId            - Where the _CLS is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _CLS control method that returns PCI-defined
 *              class code of the device. The _CLS value is always a package
 *              containing PCI class information as a list of integers.
 *              The returned string has format "BBSSPP", where:
 *                BB = Base-class code
 *                SS = Sub-class code
 *                PP = Programming Interface code
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtExecute_CLS (
    ACPI_NAMESPACE_NODE     *DeviceNode,
    ACPI_PNP_DEVICE_ID      **ReturnId)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     **ClsObjects;
    UINT32                  Count;
    ACPI_PNP_DEVICE_ID      *Cls;
    UINT32                  Length;
    ACPI_STATUS             Status;
    UINT8                   ClassCode[3] = {0, 0, 0};


    ACPI_FUNCTION_TRACE (UtExecute_CLS);


    Status = AcpiUtEvaluateObject (DeviceNode, METHOD_NAME__CLS,
        ACPI_BTYPE_PACKAGE, &ObjDesc);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Get the size of the String to be returned, includes null terminator */

    Length = ACPI_PCICLS_STRING_SIZE;
    ClsObjects = ObjDesc->Package.Elements;
    Count = ObjDesc->Package.Count;

    if (ObjDesc->Common.Type == ACPI_TYPE_PACKAGE)
    {
        if (Count > 0 && ClsObjects[0]->Common.Type == ACPI_TYPE_INTEGER)
        {
            ClassCode[0] = (UINT8) ClsObjects[0]->Integer.Value;
        }
        if (Count > 1 && ClsObjects[1]->Common.Type == ACPI_TYPE_INTEGER)
        {
            ClassCode[1] = (UINT8) ClsObjects[1]->Integer.Value;
        }
        if (Count > 2 && ClsObjects[2]->Common.Type == ACPI_TYPE_INTEGER)
        {
            ClassCode[2] = (UINT8) ClsObjects[2]->Integer.Value;
        }
    }

    /* Allocate a buffer for the CLS */

    Cls = ACPI_ALLOCATE_ZEROED (
        sizeof (ACPI_PNP_DEVICE_ID) + (ACPI_SIZE) Length);
    if (!Cls)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /* Area for the string starts after PNP_DEVICE_ID struct */

    Cls->String = ACPI_ADD_PTR (char, Cls, sizeof (ACPI_PNP_DEVICE_ID));

    /* Simply copy existing string */

    AcpiExPciClsToString (Cls->String, ClassCode);
    Cls->Length = Length;
    *ReturnId = Cls;


Cleanup:

    /* On exit, we must delete the return object */

    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}
