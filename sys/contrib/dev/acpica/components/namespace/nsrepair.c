/******************************************************************************
 *
 * Module Name: nsrepair - Repair for objects returned by predefined methods
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
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acpredef.h>
#include <contrib/dev/acpica/include/amlresrc.h>

#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nsrepair")


/*******************************************************************************
 *
 * This module attempts to repair or convert objects returned by the
 * predefined methods to an object type that is expected, as per the ACPI
 * specification. The need for this code is dictated by the many machines that
 * return incorrect types for the standard predefined methods. Performing these
 * conversions here, in one place, eliminates the need for individual ACPI
 * device drivers to do the same. Note: Most of these conversions are different
 * than the internal object conversion routines used for implicit object
 * conversion.
 *
 * The following conversions can be performed as necessary:
 *
 * Integer -> String
 * Integer -> Buffer
 * String  -> Integer
 * String  -> Buffer
 * Buffer  -> Integer
 * Buffer  -> String
 * Buffer  -> Package of Integers
 * Package -> Package of one Package
 *
 * Additional conversions that are available:
 *  Convert a null return or zero return value to an EndTag descriptor
 *  Convert an ASCII string to a Unicode buffer
 *
 * An incorrect standalone object is wrapped with required outer package
 *
 * Additional possible repairs:
 * Required package elements that are NULL replaced by Integer/String/Buffer
 *
 ******************************************************************************/


/* Local prototypes */

static const ACPI_SIMPLE_REPAIR_INFO *
AcpiNsMatchSimpleRepair (
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  ReturnBtype,
    UINT32                  PackageIndex);


/*
 * Special but simple repairs for some names.
 *
 * 2nd argument: Unexpected types that can be repaired
 */
static const ACPI_SIMPLE_REPAIR_INFO    AcpiObjectRepairInfo[] =
{
    /* Resource descriptor conversions */

    { "_CRS", ACPI_RTYPE_INTEGER | ACPI_RTYPE_STRING | ACPI_RTYPE_BUFFER | ACPI_RTYPE_NONE,
                ACPI_NOT_PACKAGE_ELEMENT,
                AcpiNsConvertToResource },
    { "_DMA", ACPI_RTYPE_INTEGER | ACPI_RTYPE_STRING | ACPI_RTYPE_BUFFER | ACPI_RTYPE_NONE,
                ACPI_NOT_PACKAGE_ELEMENT,
                AcpiNsConvertToResource },
    { "_PRS", ACPI_RTYPE_INTEGER | ACPI_RTYPE_STRING | ACPI_RTYPE_BUFFER | ACPI_RTYPE_NONE,
                ACPI_NOT_PACKAGE_ELEMENT,
                AcpiNsConvertToResource },

    /* Object reference conversions */

    { "_DEP", ACPI_RTYPE_STRING, ACPI_ALL_PACKAGE_ELEMENTS,
                AcpiNsConvertToReference },

    /* Unicode conversions */

    { "_MLS", ACPI_RTYPE_STRING, 1,
                AcpiNsConvertToUnicode },
    { "_STR", ACPI_RTYPE_STRING | ACPI_RTYPE_BUFFER,
                ACPI_NOT_PACKAGE_ELEMENT,
                AcpiNsConvertToUnicode },
    { {0,0,0,0}, 0, 0, NULL } /* Table terminator */
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsSimpleRepair
 *
 * PARAMETERS:  Info                - Method execution information block
 *              ExpectedBtypes      - Object types expected
 *              PackageIndex        - Index of object within parent package (if
 *                                    applicable - ACPI_NOT_PACKAGE_ELEMENT
 *                                    otherwise)
 *              ReturnObjectPtr     - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if repair was successful.
 *
 * DESCRIPTION: Attempt to repair/convert a return object of a type that was
 *              not expected.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsSimpleRepair (
    ACPI_EVALUATE_INFO      *Info,
    UINT32                  ExpectedBtypes,
    UINT32                  PackageIndex,
    ACPI_OPERAND_OBJECT     **ReturnObjectPtr)
{
    ACPI_OPERAND_OBJECT     *ReturnObject = *ReturnObjectPtr;
    ACPI_OPERAND_OBJECT     *NewObject = NULL;
    ACPI_STATUS             Status;
    const ACPI_SIMPLE_REPAIR_INFO   *Predefined;


    ACPI_FUNCTION_NAME (NsSimpleRepair);


    /*
     * Special repairs for certain names that are in the repair table.
     * Check if this name is in the list of repairable names.
     */
    Predefined = AcpiNsMatchSimpleRepair (Info->Node,
        Info->ReturnBtype, PackageIndex);
    if (Predefined)
    {
        if (!ReturnObject)
        {
            ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname,
                ACPI_WARN_ALWAYS, "Missing expected return value"));
        }

        Status = Predefined->ObjectConverter (Info->Node, ReturnObject,
            &NewObject);
        if (ACPI_FAILURE (Status))
        {
            /* A fatal error occurred during a conversion */

            ACPI_EXCEPTION ((AE_INFO, Status,
                "During return object analysis"));
            return (Status);
        }
        if (NewObject)
        {
            goto ObjectRepaired;
        }
    }

    /*
     * Do not perform simple object repair unless the return type is not
     * expected.
     */
    if (Info->ReturnBtype & ExpectedBtypes)
    {
        return (AE_OK);
    }

    /*
     * At this point, we know that the type of the returned object was not
     * one of the expected types for this predefined name. Attempt to
     * repair the object by converting it to one of the expected object
     * types for this predefined name.
     */

    /*
     * If there is no return value, check if we require a return value for
     * this predefined name. Either one return value is expected, or none,
     * for both methods and other objects.
     *
     * Try to fix if there was no return object. Warning if failed to fix.
     */
    if (!ReturnObject)
    {
        if (ExpectedBtypes && (!(ExpectedBtypes & ACPI_RTYPE_NONE)))
        {
            if (PackageIndex != ACPI_NOT_PACKAGE_ELEMENT)
            {
                ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname,
                    ACPI_WARN_ALWAYS, "Found unexpected NULL package element"));

                Status = AcpiNsRepairNullElement (Info, ExpectedBtypes,
                    PackageIndex, ReturnObjectPtr);
                if (ACPI_SUCCESS (Status))
                {
                    return (AE_OK); /* Repair was successful */
                }
            }
            else
            {
                ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname,
                    ACPI_WARN_ALWAYS, "Missing expected return value"));
            }

            return (AE_AML_NO_RETURN_VALUE);
        }
    }

    if (ExpectedBtypes & ACPI_RTYPE_INTEGER)
    {
        Status = AcpiNsConvertToInteger (ReturnObject, &NewObject);
        if (ACPI_SUCCESS (Status))
        {
            goto ObjectRepaired;
        }
    }
    if (ExpectedBtypes & ACPI_RTYPE_STRING)
    {
        Status = AcpiNsConvertToString (ReturnObject, &NewObject);
        if (ACPI_SUCCESS (Status))
        {
            goto ObjectRepaired;
        }
    }
    if (ExpectedBtypes & ACPI_RTYPE_BUFFER)
    {
        Status = AcpiNsConvertToBuffer (ReturnObject, &NewObject);
        if (ACPI_SUCCESS (Status))
        {
            goto ObjectRepaired;
        }
    }
    if (ExpectedBtypes & ACPI_RTYPE_PACKAGE)
    {
        /*
         * A package is expected. We will wrap the existing object with a
         * new package object. It is often the case that if a variable-length
         * package is required, but there is only a single object needed, the
         * BIOS will return that object instead of wrapping it with a Package
         * object. Note: after the wrapping, the package will be validated
         * for correct contents (expected object type or types).
         */
        Status = AcpiNsWrapWithPackage (Info, ReturnObject, &NewObject);
        if (ACPI_SUCCESS (Status))
        {
            /*
             * The original object just had its reference count
             * incremented for being inserted into the new package.
             */
            *ReturnObjectPtr = NewObject;       /* New Package object */
            Info->ReturnFlags |= ACPI_OBJECT_REPAIRED;
            return (AE_OK);
        }
    }

    /* We cannot repair this object */

    return (AE_AML_OPERAND_TYPE);


ObjectRepaired:

    /* Object was successfully repaired */

    if (PackageIndex != ACPI_NOT_PACKAGE_ELEMENT)
    {
        /* Update reference count of new object */

        if (!(Info->ReturnFlags & ACPI_OBJECT_WRAPPED))
        {
            NewObject->Common.ReferenceCount =
                ReturnObject->Common.ReferenceCount;
        }

        ACPI_DEBUG_PRINT ((ACPI_DB_REPAIR,
            "%s: Converted %s to expected %s at Package index %u\n",
            Info->FullPathname, AcpiUtGetObjectTypeName (ReturnObject),
            AcpiUtGetObjectTypeName (NewObject), PackageIndex));
    }
    else
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_REPAIR,
            "%s: Converted %s to expected %s\n",
            Info->FullPathname, AcpiUtGetObjectTypeName (ReturnObject),
            AcpiUtGetObjectTypeName (NewObject)));
    }

    /* Delete old object, install the new return object */

    AcpiUtRemoveReference (ReturnObject);
    *ReturnObjectPtr = NewObject;
    Info->ReturnFlags |= ACPI_OBJECT_REPAIRED;
    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiNsMatchSimpleRepair
 *
 * PARAMETERS:  Node                - Namespace node for the method/object
 *              ReturnBtype         - Object type that was returned
 *              PackageIndex        - Index of object within parent package (if
 *                                    applicable - ACPI_NOT_PACKAGE_ELEMENT
 *                                    otherwise)
 *
 * RETURN:      Pointer to entry in repair table. NULL indicates not found.
 *
 * DESCRIPTION: Check an object name against the repairable object list.
 *
 *****************************************************************************/

static const ACPI_SIMPLE_REPAIR_INFO *
AcpiNsMatchSimpleRepair (
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  ReturnBtype,
    UINT32                  PackageIndex)
{
    const ACPI_SIMPLE_REPAIR_INFO   *ThisName;


    /* Search info table for a repairable predefined method/object name */

    ThisName = AcpiObjectRepairInfo;
    while (ThisName->ObjectConverter)
    {
        if (ACPI_COMPARE_NAME (Node->Name.Ascii, ThisName->Name))
        {
            /* Check if we can actually repair this name/type combination */

            if ((ReturnBtype & ThisName->UnexpectedBtypes) &&
                (ThisName->PackageIndex == ACPI_ALL_PACKAGE_ELEMENTS ||
                 PackageIndex == ThisName->PackageIndex))
            {
                return (ThisName);
            }

            return (NULL);
        }

        ThisName++;
    }

    return (NULL); /* Name was not found in the repair table */
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsRepairNullElement
 *
 * PARAMETERS:  Info                - Method execution information block
 *              ExpectedBtypes      - Object types expected
 *              PackageIndex        - Index of object within parent package (if
 *                                    applicable - ACPI_NOT_PACKAGE_ELEMENT
 *                                    otherwise)
 *              ReturnObjectPtr     - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if repair was successful.
 *
 * DESCRIPTION: Attempt to repair a NULL element of a returned Package object.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsRepairNullElement (
    ACPI_EVALUATE_INFO      *Info,
    UINT32                  ExpectedBtypes,
    UINT32                  PackageIndex,
    ACPI_OPERAND_OBJECT     **ReturnObjectPtr)
{
    ACPI_OPERAND_OBJECT     *ReturnObject = *ReturnObjectPtr;
    ACPI_OPERAND_OBJECT     *NewObject;


    ACPI_FUNCTION_NAME (NsRepairNullElement);


    /* No repair needed if return object is non-NULL */

    if (ReturnObject)
    {
        return (AE_OK);
    }

    /*
     * Attempt to repair a NULL element of a Package object. This applies to
     * predefined names that return a fixed-length package and each element
     * is required. It does not apply to variable-length packages where NULL
     * elements are allowed, especially at the end of the package.
     */
    if (ExpectedBtypes & ACPI_RTYPE_INTEGER)
    {
        /* Need an Integer - create a zero-value integer */

        NewObject = AcpiUtCreateIntegerObject ((UINT64) 0);
    }
    else if (ExpectedBtypes & ACPI_RTYPE_STRING)
    {
        /* Need a String - create a NULL string */

        NewObject = AcpiUtCreateStringObject (0);
    }
    else if (ExpectedBtypes & ACPI_RTYPE_BUFFER)
    {
        /* Need a Buffer - create a zero-length buffer */

        NewObject = AcpiUtCreateBufferObject (0);
    }
    else
    {
        /* Error for all other expected types */

        return (AE_AML_OPERAND_TYPE);
    }

    if (!NewObject)
    {
        return (AE_NO_MEMORY);
    }

    /* Set the reference count according to the parent Package object */

    NewObject->Common.ReferenceCount =
        Info->ParentPackage->Common.ReferenceCount;

    ACPI_DEBUG_PRINT ((ACPI_DB_REPAIR,
        "%s: Converted NULL package element to expected %s at index %u\n",
        Info->FullPathname, AcpiUtGetObjectTypeName (NewObject),
        PackageIndex));

    *ReturnObjectPtr = NewObject;
    Info->ReturnFlags |= ACPI_OBJECT_REPAIRED;
    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiNsRemoveNullElements
 *
 * PARAMETERS:  Info                - Method execution information block
 *              PackageType         - An AcpiReturnPackageTypes value
 *              ObjDesc             - A Package object
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Remove all NULL package elements from packages that contain
 *              a variable number of subpackages. For these types of
 *              packages, NULL elements can be safely removed.
 *
 *****************************************************************************/

void
AcpiNsRemoveNullElements (
    ACPI_EVALUATE_INFO      *Info,
    UINT8                   PackageType,
    ACPI_OPERAND_OBJECT     *ObjDesc)
{
    ACPI_OPERAND_OBJECT     **Source;
    ACPI_OPERAND_OBJECT     **Dest;
    UINT32                  Count;
    UINT32                  NewCount;
    UINT32                  i;


    ACPI_FUNCTION_NAME (NsRemoveNullElements);


    /*
     * We can safely remove all NULL elements from these package types:
     * PTYPE1_VAR packages contain a variable number of simple data types.
     * PTYPE2 packages contain a variable number of subpackages.
     */
    switch (PackageType)
    {
    case ACPI_PTYPE1_VAR:
    case ACPI_PTYPE2:
    case ACPI_PTYPE2_COUNT:
    case ACPI_PTYPE2_PKG_COUNT:
    case ACPI_PTYPE2_FIXED:
    case ACPI_PTYPE2_MIN:
    case ACPI_PTYPE2_REV_FIXED:
    case ACPI_PTYPE2_FIX_VAR:
        break;

    default:
    case ACPI_PTYPE2_VAR_VAR:
    case ACPI_PTYPE1_FIXED:
    case ACPI_PTYPE1_OPTION:
        return;
    }

    Count = ObjDesc->Package.Count;
    NewCount = Count;

    Source = ObjDesc->Package.Elements;
    Dest = Source;

    /* Examine all elements of the package object, remove nulls */

    for (i = 0; i < Count; i++)
    {
        if (!*Source)
        {
            NewCount--;
        }
        else
        {
            *Dest = *Source;
            Dest++;
        }

        Source++;
    }

    /* Update parent package if any null elements were removed */

    if (NewCount < Count)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_REPAIR,
            "%s: Found and removed %u NULL elements\n",
            Info->FullPathname, (Count - NewCount)));

        /* NULL terminate list and update the package count */

        *Dest = NULL;
        ObjDesc->Package.Count = NewCount;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsWrapWithPackage
 *
 * PARAMETERS:  Info                - Method execution information block
 *              OriginalObject      - Pointer to the object to repair.
 *              ObjDescPtr          - The new package object is returned here
 *
 * RETURN:      Status, new object in *ObjDescPtr
 *
 * DESCRIPTION: Repair a common problem with objects that are defined to
 *              return a variable-length Package of sub-objects. If there is
 *              only one sub-object, some BIOS code mistakenly simply declares
 *              the single object instead of a Package with one sub-object.
 *              This function attempts to repair this error by wrapping a
 *              Package object around the original object, creating the
 *              correct and expected Package with one sub-object.
 *
 *              Names that can be repaired in this manner include:
 *              _ALR, _CSD, _HPX, _MLS, _PLD, _PRT, _PSS, _TRT, _TSS,
 *              _BCL, _DOD, _FIX, _Sx
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsWrapWithPackage (
    ACPI_EVALUATE_INFO      *Info,
    ACPI_OPERAND_OBJECT     *OriginalObject,
    ACPI_OPERAND_OBJECT     **ObjDescPtr)
{
    ACPI_OPERAND_OBJECT     *PkgObjDesc;


    ACPI_FUNCTION_NAME (NsWrapWithPackage);


    /*
     * Create the new outer package and populate it. The new
     * package will have a single element, the lone sub-object.
     */
    PkgObjDesc = AcpiUtCreatePackageObject (1);
    if (!PkgObjDesc)
    {
        return (AE_NO_MEMORY);
    }

    PkgObjDesc->Package.Elements[0] = OriginalObject;

    ACPI_DEBUG_PRINT ((ACPI_DB_REPAIR,
        "%s: Wrapped %s with expected Package object\n",
        Info->FullPathname, AcpiUtGetObjectTypeName (OriginalObject)));

    /* Return the new object in the object pointer */

    *ObjDescPtr = PkgObjDesc;
    Info->ReturnFlags |= ACPI_OBJECT_REPAIRED | ACPI_OBJECT_WRAPPED;
    return (AE_OK);
}
