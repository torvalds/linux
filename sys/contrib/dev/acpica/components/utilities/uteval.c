/******************************************************************************
 *
 * Module Name: uteval - Object evaluation
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


#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("uteval")


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtEvaluateObject
 *
 * PARAMETERS:  PrefixNode          - Starting node
 *              Path                - Path to object from starting node
 *              ExpectedReturnTypes - Bitmap of allowed return types
 *              ReturnDesc          - Where a return value is stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluates a namespace object and verifies the type of the
 *              return object. Common code that simplifies accessing objects
 *              that have required return objects of fixed types.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtEvaluateObject (
    ACPI_NAMESPACE_NODE     *PrefixNode,
    const char              *Path,
    UINT32                  ExpectedReturnBtypes,
    ACPI_OPERAND_OBJECT     **ReturnDesc)
{
    ACPI_EVALUATE_INFO      *Info;
    ACPI_STATUS             Status;
    UINT32                  ReturnBtype;


    ACPI_FUNCTION_TRACE (UtEvaluateObject);


    /* Allocate the evaluation information block */

    Info = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_EVALUATE_INFO));
    if (!Info)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    Info->PrefixNode = PrefixNode;
    Info->RelativePathname = Path;

    /* Evaluate the object/method */

    Status = AcpiNsEvaluate (Info);
    if (ACPI_FAILURE (Status))
    {
        if (Status == AE_NOT_FOUND)
        {
            ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[%4.4s.%s] was not found\n",
                AcpiUtGetNodeName (PrefixNode), Path));
        }
        else
        {
            ACPI_ERROR_METHOD ("Method execution failed",
                PrefixNode, Path, Status);
        }

        goto Cleanup;
    }

    /* Did we get a return object? */

    if (!Info->ReturnObject)
    {
        if (ExpectedReturnBtypes)
        {
            ACPI_ERROR_METHOD ("No object was returned from",
                PrefixNode, Path, AE_NOT_EXIST);

            Status = AE_NOT_EXIST;
        }

        goto Cleanup;
    }

    /* Map the return object type to the bitmapped type */

    switch ((Info->ReturnObject)->Common.Type)
    {
    case ACPI_TYPE_INTEGER:

        ReturnBtype = ACPI_BTYPE_INTEGER;
        break;

    case ACPI_TYPE_BUFFER:

        ReturnBtype = ACPI_BTYPE_BUFFER;
        break;

    case ACPI_TYPE_STRING:

        ReturnBtype = ACPI_BTYPE_STRING;
        break;

    case ACPI_TYPE_PACKAGE:

        ReturnBtype = ACPI_BTYPE_PACKAGE;
        break;

    default:

        ReturnBtype = 0;
        break;
    }

    if ((AcpiGbl_EnableInterpreterSlack) &&
        (!ExpectedReturnBtypes))
    {
        /*
         * We received a return object, but one was not expected. This can
         * happen frequently if the "implicit return" feature is enabled.
         * Just delete the return object and return AE_OK.
         */
        AcpiUtRemoveReference (Info->ReturnObject);
        goto Cleanup;
    }

    /* Is the return object one of the expected types? */

    if (!(ExpectedReturnBtypes & ReturnBtype))
    {
        ACPI_ERROR_METHOD ("Return object type is incorrect",
            PrefixNode, Path, AE_TYPE);

        ACPI_ERROR ((AE_INFO,
            "Type returned from %s was incorrect: %s, expected Btypes: 0x%X",
            Path, AcpiUtGetObjectTypeName (Info->ReturnObject),
            ExpectedReturnBtypes));

        /* On error exit, we must delete the return object */

        AcpiUtRemoveReference (Info->ReturnObject);
        Status = AE_TYPE;
        goto Cleanup;
    }

    /* Object type is OK, return it */

    *ReturnDesc = Info->ReturnObject;

Cleanup:
    ACPI_FREE (Info);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtEvaluateNumericObject
 *
 * PARAMETERS:  ObjectName          - Object name to be evaluated
 *              DeviceNode          - Node for the device
 *              Value               - Where the value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluates a numeric namespace object for a selected device
 *              and stores result in *Value.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtEvaluateNumericObject (
    const char              *ObjectName,
    ACPI_NAMESPACE_NODE     *DeviceNode,
    UINT64                  *Value)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (UtEvaluateNumericObject);


    Status = AcpiUtEvaluateObject (DeviceNode, ObjectName,
        ACPI_BTYPE_INTEGER, &ObjDesc);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Get the returned Integer */

    *Value = ObjDesc->Integer.Value;

    /* On exit, we must delete the return object */

    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtExecute_STA
 *
 * PARAMETERS:  DeviceNode          - Node for the device
 *              Flags               - Where the status flags are returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes _STA for selected device and stores results in
 *              *Flags. If _STA does not exist, then the device is assumed
 *              to be present/functional/enabled (as per the ACPI spec).
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtExecute_STA (
    ACPI_NAMESPACE_NODE     *DeviceNode,
    UINT32                  *Flags)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (UtExecute_STA);


    Status = AcpiUtEvaluateObject (DeviceNode, METHOD_NAME__STA,
        ACPI_BTYPE_INTEGER, &ObjDesc);
    if (ACPI_FAILURE (Status))
    {
        if (AE_NOT_FOUND == Status)
        {
            /*
             * if _STA does not exist, then (as per the ACPI specification),
             * the returned flags will indicate that the device is present,
             * functional, and enabled.
             */
            ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
                "_STA on %4.4s was not found, assuming device is present\n",
                AcpiUtGetNodeName (DeviceNode)));

            *Flags = ACPI_UINT32_MAX;
            Status = AE_OK;
        }

        return_ACPI_STATUS (Status);
    }

    /* Extract the status flags */

    *Flags = (UINT32) ObjDesc->Integer.Value;

    /* On exit, we must delete the return object */

    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtExecutePowerMethods
 *
 * PARAMETERS:  DeviceNode          - Node for the device
 *              MethodNames         - Array of power method names
 *              MethodCount         - Number of methods to execute
 *              OutValues           - Where the power method values are returned
 *
 * RETURN:      Status, OutValues
 *
 * DESCRIPTION: Executes the specified power methods for the device and returns
 *              the result(s).
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtExecutePowerMethods (
    ACPI_NAMESPACE_NODE     *DeviceNode,
    const char              **MethodNames,
    UINT8                   MethodCount,
    UINT8                   *OutValues)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_STATUS             Status;
    ACPI_STATUS             FinalStatus = AE_NOT_FOUND;
    UINT32                  i;


    ACPI_FUNCTION_TRACE (UtExecutePowerMethods);


    for (i = 0; i < MethodCount; i++)
    {
        /*
         * Execute the power method (_SxD or _SxW). The only allowable
         * return type is an Integer.
         */
        Status = AcpiUtEvaluateObject (DeviceNode,
            ACPI_CAST_PTR (char, MethodNames[i]),
            ACPI_BTYPE_INTEGER, &ObjDesc);
        if (ACPI_SUCCESS (Status))
        {
            OutValues[i] = (UINT8) ObjDesc->Integer.Value;

            /* Delete the return object */

            AcpiUtRemoveReference (ObjDesc);
            FinalStatus = AE_OK;            /* At least one value is valid */
            continue;
        }

        OutValues[i] = ACPI_UINT8_MAX;
        if (Status == AE_NOT_FOUND)
        {
            continue; /* Ignore if not found */
        }

        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Failed %s on Device %4.4s, %s\n",
            ACPI_CAST_PTR (char, MethodNames[i]),
            AcpiUtGetNodeName (DeviceNode), AcpiFormatException (Status)));
    }

    return_ACPI_STATUS (FinalStatus);
}
