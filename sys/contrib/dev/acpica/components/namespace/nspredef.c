/******************************************************************************
 *
 * Module Name: nspredef - Validation of ACPI predefined methods and objects
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

#define ACPI_CREATE_PREDEFINED_TABLE

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acpredef.h>


#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nspredef")


/*******************************************************************************
 *
 * This module validates predefined ACPI objects that appear in the namespace,
 * at the time they are evaluated (via AcpiEvaluateObject). The purpose of this
 * validation is to detect problems with BIOS-exposed predefined ACPI objects
 * before the results are returned to the ACPI-related drivers.
 *
 * There are several areas that are validated:
 *
 *  1) The number of input arguments as defined by the method/object in the
 *     ASL is validated against the ACPI specification.
 *  2) The type of the return object (if any) is validated against the ACPI
 *     specification.
 *  3) For returned package objects, the count of package elements is
 *     validated, as well as the type of each package element. Nested
 *     packages are supported.
 *
 * For any problems found, a warning message is issued.
 *
 ******************************************************************************/


/* Local prototypes */

static ACPI_STATUS
AcpiNsCheckReference (
    ACPI_EVALUATE_INFO          *Info,
    ACPI_OPERAND_OBJECT         *ReturnObject);

static UINT32
AcpiNsGetBitmappedType (
    ACPI_OPERAND_OBJECT         *ReturnObject);


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsCheckReturnValue
 *
 * PARAMETERS:  Node            - Namespace node for the method/object
 *              Info            - Method execution information block
 *              UserParamCount  - Number of parameters actually passed
 *              ReturnStatus    - Status from the object evaluation
 *              ReturnObjectPtr - Pointer to the object returned from the
 *                                evaluation of a method or object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check the value returned from a predefined name.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsCheckReturnValue (
    ACPI_NAMESPACE_NODE         *Node,
    ACPI_EVALUATE_INFO          *Info,
    UINT32                      UserParamCount,
    ACPI_STATUS                 ReturnStatus,
    ACPI_OPERAND_OBJECT         **ReturnObjectPtr)
{
    ACPI_STATUS                 Status;
    const ACPI_PREDEFINED_INFO  *Predefined;


    /* If not a predefined name, we cannot validate the return object */

    Predefined = Info->Predefined;
    if (!Predefined)
    {
        return (AE_OK);
    }

    /*
     * If the method failed or did not actually return an object, we cannot
     * validate the return object
     */
    if ((ReturnStatus != AE_OK) &&
        (ReturnStatus != AE_CTRL_RETURN_VALUE))
    {
        return (AE_OK);
    }

    /*
     * Return value validation and possible repair.
     *
     * 1) Don't perform return value validation/repair if this feature
     * has been disabled via a global option.
     *
     * 2) We have a return value, but if one wasn't expected, just exit,
     * this is not a problem. For example, if the "Implicit Return"
     * feature is enabled, methods will always return a value.
     *
     * 3) If the return value can be of any type, then we cannot perform
     * any validation, just exit.
     */
    if (AcpiGbl_DisableAutoRepair ||
        (!Predefined->Info.ExpectedBtypes) ||
        (Predefined->Info.ExpectedBtypes == ACPI_RTYPE_ALL))
    {
        return (AE_OK);
    }

    /*
     * Check that the type of the main return object is what is expected
     * for this predefined name
     */
    Status = AcpiNsCheckObjectType (Info, ReturnObjectPtr,
        Predefined->Info.ExpectedBtypes, ACPI_NOT_PACKAGE_ELEMENT);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    /*
     *
     * 4) If there is no return value and it is optional, just return
     * AE_OK (_WAK).
     */
    if (!(*ReturnObjectPtr))
    {
        goto Exit;
    }

    /*
     * For returned Package objects, check the type of all sub-objects.
     * Note: Package may have been newly created by call above.
     */
    if ((*ReturnObjectPtr)->Common.Type == ACPI_TYPE_PACKAGE)
    {
        Info->ParentPackage = *ReturnObjectPtr;
        Status = AcpiNsCheckPackage (Info, ReturnObjectPtr);
        if (ACPI_FAILURE (Status))
        {
            /* We might be able to fix some errors */

            if ((Status != AE_AML_OPERAND_TYPE) &&
                (Status != AE_AML_OPERAND_VALUE))
            {
                goto Exit;
            }
        }
    }

    /*
     * The return object was OK, or it was successfully repaired above.
     * Now make some additional checks such as verifying that package
     * objects are sorted correctly (if required) or buffer objects have
     * the correct data width (bytes vs. dwords). These repairs are
     * performed on a per-name basis, i.e., the code is specific to
     * particular predefined names.
     */
    Status = AcpiNsComplexRepairs (Info, Node, Status, ReturnObjectPtr);

Exit:
    /*
     * If the object validation failed or if we successfully repaired one
     * or more objects, mark the parent node to suppress further warning
     * messages during the next evaluation of the same method/object.
     */
    if (ACPI_FAILURE (Status) ||
       (Info->ReturnFlags & ACPI_OBJECT_REPAIRED))
    {
        Node->Flags |= ANOBJ_EVALUATED;
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsCheckObjectType
 *
 * PARAMETERS:  Info            - Method execution information block
 *              ReturnObjectPtr - Pointer to the object returned from the
 *                                evaluation of a method or object
 *              ExpectedBtypes  - Bitmap of expected return type(s)
 *              PackageIndex    - Index of object within parent package (if
 *                                applicable - ACPI_NOT_PACKAGE_ELEMENT
 *                                otherwise)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check the type of the return object against the expected object
 *              type(s). Use of Btype allows multiple expected object types.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsCheckObjectType (
    ACPI_EVALUATE_INFO          *Info,
    ACPI_OPERAND_OBJECT         **ReturnObjectPtr,
    UINT32                      ExpectedBtypes,
    UINT32                      PackageIndex)
{
    ACPI_OPERAND_OBJECT         *ReturnObject = *ReturnObjectPtr;
    ACPI_STATUS                 Status = AE_OK;
    char                        TypeBuffer[96]; /* Room for 10 types */


    /* A Namespace node should not get here, but make sure */

    if (ReturnObject &&
        ACPI_GET_DESCRIPTOR_TYPE (ReturnObject) == ACPI_DESC_TYPE_NAMED)
    {
        ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
            "Invalid return type - Found a Namespace node [%4.4s] type %s",
            ReturnObject->Node.Name.Ascii,
            AcpiUtGetTypeName (ReturnObject->Node.Type)));
        return (AE_AML_OPERAND_TYPE);
    }

    /*
     * Convert the object type (ACPI_TYPE_xxx) to a bitmapped object type.
     * The bitmapped type allows multiple possible return types.
     *
     * Note, the cases below must handle all of the possible types returned
     * from all of the predefined names (including elements of returned
     * packages)
     */
    Info->ReturnBtype = AcpiNsGetBitmappedType (ReturnObject);
    if (Info->ReturnBtype == ACPI_RTYPE_ANY)
    {
        /* Not one of the supported objects, must be incorrect */
        goto TypeErrorExit;
    }

    /* For reference objects, check that the reference type is correct */

    if ((Info->ReturnBtype & ExpectedBtypes) == ACPI_RTYPE_REFERENCE)
    {
        Status = AcpiNsCheckReference (Info, ReturnObject);
        return (Status);
    }

    /* Attempt simple repair of the returned object if necessary */

    Status = AcpiNsSimpleRepair (Info, ExpectedBtypes,
        PackageIndex, ReturnObjectPtr);
    if (ACPI_SUCCESS (Status))
    {
        return (AE_OK); /* Successful repair */
    }


TypeErrorExit:

    /* Create a string with all expected types for this predefined object */

    AcpiUtGetExpectedReturnTypes (TypeBuffer, ExpectedBtypes);

    if (!ReturnObject)
    {
        ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
            "Expected return object of type %s",
            TypeBuffer));
    }
    else if (PackageIndex == ACPI_NOT_PACKAGE_ELEMENT)
    {
        ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
            "Return type mismatch - found %s, expected %s",
            AcpiUtGetObjectTypeName (ReturnObject), TypeBuffer));
    }
    else
    {
        ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
            "Return Package type mismatch at index %u - "
            "found %s, expected %s", PackageIndex,
            AcpiUtGetObjectTypeName (ReturnObject), TypeBuffer));
    }

    return (AE_AML_OPERAND_TYPE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsCheckReference
 *
 * PARAMETERS:  Info            - Method execution information block
 *              ReturnObject    - Object returned from the evaluation of a
 *                                method or object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check a returned reference object for the correct reference
 *              type. The only reference type that can be returned from a
 *              predefined method is a named reference. All others are invalid.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiNsCheckReference (
    ACPI_EVALUATE_INFO          *Info,
    ACPI_OPERAND_OBJECT         *ReturnObject)
{

    /*
     * Check the reference object for the correct reference type (opcode).
     * The only type of reference that can be converted to an ACPI_OBJECT is
     * a reference to a named object (reference class: NAME)
     */
    if (ReturnObject->Reference.Class == ACPI_REFCLASS_NAME)
    {
        return (AE_OK);
    }

    ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
        "Return type mismatch - unexpected reference object type [%s] %2.2X",
        AcpiUtGetReferenceName (ReturnObject),
        ReturnObject->Reference.Class));

    return (AE_AML_OPERAND_TYPE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsGetBitmappedType
 *
 * PARAMETERS:  ReturnObject    - Object returned from method/obj evaluation
 *
 * RETURN:      Object return type. ACPI_RTYPE_ANY indicates that the object
 *              type is not supported. ACPI_RTYPE_NONE indicates that no
 *              object was returned (ReturnObject is NULL).
 *
 * DESCRIPTION: Convert object type into a bitmapped object return type.
 *
 ******************************************************************************/

static UINT32
AcpiNsGetBitmappedType (
    ACPI_OPERAND_OBJECT         *ReturnObject)
{
    UINT32                      ReturnBtype;


    if (!ReturnObject)
    {
        return (ACPI_RTYPE_NONE);
    }

    /* Map ACPI_OBJECT_TYPE to internal bitmapped type */

    switch (ReturnObject->Common.Type)
    {
    case ACPI_TYPE_INTEGER:

        ReturnBtype = ACPI_RTYPE_INTEGER;
        break;

    case ACPI_TYPE_BUFFER:

        ReturnBtype = ACPI_RTYPE_BUFFER;
        break;

    case ACPI_TYPE_STRING:

        ReturnBtype = ACPI_RTYPE_STRING;
        break;

    case ACPI_TYPE_PACKAGE:

        ReturnBtype = ACPI_RTYPE_PACKAGE;
        break;

    case ACPI_TYPE_LOCAL_REFERENCE:

        ReturnBtype = ACPI_RTYPE_REFERENCE;
        break;

    default:

        /* Not one of the supported objects, must be incorrect */

        ReturnBtype = ACPI_RTYPE_ANY;
        break;
    }

    return (ReturnBtype);
}
