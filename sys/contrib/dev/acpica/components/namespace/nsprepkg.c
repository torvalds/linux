/******************************************************************************
 *
 * Module Name: nsprepkg - Validation of package objects for predefined names
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
#include <contrib/dev/acpica/include/acpredef.h>


#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nsprepkg")


/* Local prototypes */

static ACPI_STATUS
AcpiNsCheckPackageList (
    ACPI_EVALUATE_INFO          *Info,
    const ACPI_PREDEFINED_INFO  *Package,
    ACPI_OPERAND_OBJECT         **Elements,
    UINT32                      Count);

static ACPI_STATUS
AcpiNsCheckPackageElements (
    ACPI_EVALUATE_INFO          *Info,
    ACPI_OPERAND_OBJECT         **Elements,
    UINT8                       Type1,
    UINT32                      Count1,
    UINT8                       Type2,
    UINT32                      Count2,
    UINT32                      StartIndex);

static ACPI_STATUS
AcpiNsCustomPackage (
    ACPI_EVALUATE_INFO          *Info,
    ACPI_OPERAND_OBJECT         **Elements,
    UINT32                      Count);


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsCheckPackage
 *
 * PARAMETERS:  Info                - Method execution information block
 *              ReturnObjectPtr     - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check a returned package object for the correct count and
 *              correct type of all sub-objects.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsCheckPackage (
    ACPI_EVALUATE_INFO          *Info,
    ACPI_OPERAND_OBJECT         **ReturnObjectPtr)
{
    ACPI_OPERAND_OBJECT         *ReturnObject = *ReturnObjectPtr;
    const ACPI_PREDEFINED_INFO  *Package;
    ACPI_OPERAND_OBJECT         **Elements;
    ACPI_STATUS                 Status = AE_OK;
    UINT32                      ExpectedCount;
    UINT32                      Count;
    UINT32                      i;


    ACPI_FUNCTION_NAME (NsCheckPackage);


    /* The package info for this name is in the next table entry */

    Package = Info->Predefined + 1;

    ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
        "%s Validating return Package of Type %X, Count %X\n",
        Info->FullPathname, Package->RetInfo.Type,
        ReturnObject->Package.Count));

    /*
     * For variable-length Packages, we can safely remove all embedded
     * and trailing NULL package elements
     */
    AcpiNsRemoveNullElements (Info, Package->RetInfo.Type, ReturnObject);

    /* Extract package count and elements array */

    Elements = ReturnObject->Package.Elements;
    Count = ReturnObject->Package.Count;

    /*
     * Most packages must have at least one element. The only exception
     * is the variable-length package (ACPI_PTYPE1_VAR).
     */
    if (!Count)
    {
        if (Package->RetInfo.Type == ACPI_PTYPE1_VAR)
        {
            return (AE_OK);
        }

        ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
            "Return Package has no elements (empty)"));

        return (AE_AML_OPERAND_VALUE);
    }

    /*
     * Decode the type of the expected package contents
     *
     * PTYPE1 packages contain no subpackages
     * PTYPE2 packages contain subpackages
     */
    switch (Package->RetInfo.Type)
    {
    case ACPI_PTYPE_CUSTOM:

        Status = AcpiNsCustomPackage (Info, Elements, Count);
        break;

    case ACPI_PTYPE1_FIXED:
        /*
         * The package count is fixed and there are no subpackages
         *
         * If package is too small, exit.
         * If package is larger than expected, issue warning but continue
         */
        ExpectedCount = Package->RetInfo.Count1 + Package->RetInfo.Count2;
        if (Count < ExpectedCount)
        {
            goto PackageTooSmall;
        }
        else if (Count > ExpectedCount)
        {
            ACPI_DEBUG_PRINT ((ACPI_DB_REPAIR,
                "%s: Return Package is larger than needed - "
                "found %u, expected %u\n",
                Info->FullPathname, Count, ExpectedCount));
        }

        /* Validate all elements of the returned package */

        Status = AcpiNsCheckPackageElements (Info, Elements,
            Package->RetInfo.ObjectType1, Package->RetInfo.Count1,
            Package->RetInfo.ObjectType2, Package->RetInfo.Count2, 0);
        break;

    case ACPI_PTYPE1_VAR:
        /*
         * The package count is variable, there are no subpackages, and all
         * elements must be of the same type
         */
        for (i = 0; i < Count; i++)
        {
            Status = AcpiNsCheckObjectType (Info, Elements,
                Package->RetInfo.ObjectType1, i);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            Elements++;
        }
        break;

    case ACPI_PTYPE1_OPTION:
        /*
         * The package count is variable, there are no subpackages. There are
         * a fixed number of required elements, and a variable number of
         * optional elements.
         *
         * Check if package is at least as large as the minimum required
         */
        ExpectedCount = Package->RetInfo3.Count;
        if (Count < ExpectedCount)
        {
            goto PackageTooSmall;
        }

        /* Variable number of sub-objects */

        for (i = 0; i < Count; i++)
        {
            if (i < Package->RetInfo3.Count)
            {
                /* These are the required package elements (0, 1, or 2) */

                Status = AcpiNsCheckObjectType (Info, Elements,
                    Package->RetInfo3.ObjectType[i], i);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
            }
            else
            {
                /* These are the optional package elements */

                Status = AcpiNsCheckObjectType (Info, Elements,
                    Package->RetInfo3.TailObjectType, i);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
            }

            Elements++;
        }
        break;

    case ACPI_PTYPE2_REV_FIXED:

        /* First element is the (Integer) revision */

        Status = AcpiNsCheckObjectType (
            Info, Elements, ACPI_RTYPE_INTEGER, 0);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        Elements++;
        Count--;

        /* Examine the subpackages */

        Status = AcpiNsCheckPackageList (Info, Package, Elements, Count);
        break;

    case ACPI_PTYPE2_PKG_COUNT:

        /* First element is the (Integer) count of subpackages to follow */

        Status = AcpiNsCheckObjectType (
            Info, Elements, ACPI_RTYPE_INTEGER, 0);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        /*
         * Count cannot be larger than the parent package length, but allow it
         * to be smaller. The >= accounts for the Integer above.
         */
        ExpectedCount = (UINT32) (*Elements)->Integer.Value;
        if (ExpectedCount >= Count)
        {
            goto PackageTooSmall;
        }

        Count = ExpectedCount;
        Elements++;

        /* Examine the subpackages */

        Status = AcpiNsCheckPackageList (Info, Package, Elements, Count);
        break;

    case ACPI_PTYPE2:
    case ACPI_PTYPE2_FIXED:
    case ACPI_PTYPE2_MIN:
    case ACPI_PTYPE2_COUNT:
    case ACPI_PTYPE2_FIX_VAR:
        /*
         * These types all return a single Package that consists of a
         * variable number of subpackages.
         *
         * First, ensure that the first element is a subpackage. If not,
         * the BIOS may have incorrectly returned the object as a single
         * package instead of a Package of Packages (a common error if
         * there is only one entry). We may be able to repair this by
         * wrapping the returned Package with a new outer Package.
         */
        if (*Elements && ((*Elements)->Common.Type != ACPI_TYPE_PACKAGE))
        {
            /* Create the new outer package and populate it */

            Status = AcpiNsWrapWithPackage (
                Info, ReturnObject, ReturnObjectPtr);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            /* Update locals to point to the new package (of 1 element) */

            ReturnObject = *ReturnObjectPtr;
            Elements = ReturnObject->Package.Elements;
            Count = 1;
        }

        /* Examine the subpackages */

        Status = AcpiNsCheckPackageList (Info, Package, Elements, Count);
        break;

    case ACPI_PTYPE2_VAR_VAR:
        /*
         * Returns a variable list of packages, each with a variable list
         * of objects.
         */
        break;

    case ACPI_PTYPE2_UUID_PAIR:

        /* The package must contain pairs of (UUID + type) */

        if (Count & 1)
        {
            ExpectedCount = Count + 1;
            goto PackageTooSmall;
        }

        while (Count > 0)
        {
            Status = AcpiNsCheckObjectType(Info, Elements,
                Package->RetInfo.ObjectType1, 0);
            if (ACPI_FAILURE(Status))
            {
                return (Status);
            }

            /* Validate length of the UUID buffer */

            if ((*Elements)->Buffer.Length != 16)
            {
                ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname,
                    Info->NodeFlags, "Invalid length for UUID Buffer"));
                return (AE_AML_OPERAND_VALUE);
            }

            Status = AcpiNsCheckObjectType(Info, Elements + 1,
                Package->RetInfo.ObjectType2, 0);
            if (ACPI_FAILURE(Status))
            {
                return (Status);
            }

            Elements += 2;
            Count -= 2;
        }
        break;

    default:

        /* Should not get here if predefined info table is correct */

        ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
            "Invalid internal return type in table entry: %X",
            Package->RetInfo.Type));

        return (AE_AML_INTERNAL);
    }

    return (Status);


PackageTooSmall:

    /* Error exit for the case with an incorrect package count */

    ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
        "Return Package is too small - found %u elements, expected %u",
        Count, ExpectedCount));

    return (AE_AML_OPERAND_VALUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsCheckPackageList
 *
 * PARAMETERS:  Info            - Method execution information block
 *              Package         - Pointer to package-specific info for method
 *              Elements        - Element list of parent package. All elements
 *                                of this list should be of type Package.
 *              Count           - Count of subpackages
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Examine a list of subpackages
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiNsCheckPackageList (
    ACPI_EVALUATE_INFO          *Info,
    const ACPI_PREDEFINED_INFO  *Package,
    ACPI_OPERAND_OBJECT         **Elements,
    UINT32                      Count)
{
    ACPI_OPERAND_OBJECT         *SubPackage;
    ACPI_OPERAND_OBJECT         **SubElements;
    ACPI_STATUS                 Status;
    UINT32                      ExpectedCount;
    UINT32                      i;
    UINT32                      j;


    /*
     * Validate each subpackage in the parent Package
     *
     * NOTE: assumes list of subpackages contains no NULL elements.
     * Any NULL elements should have been removed by earlier call
     * to AcpiNsRemoveNullElements.
     */
    for (i = 0; i < Count; i++)
    {
        SubPackage = *Elements;
        SubElements = SubPackage->Package.Elements;
        Info->ParentPackage = SubPackage;

        /* Each sub-object must be of type Package */

        Status = AcpiNsCheckObjectType (Info, &SubPackage,
            ACPI_RTYPE_PACKAGE, i);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        /* Examine the different types of expected subpackages */

        Info->ParentPackage = SubPackage;
        switch (Package->RetInfo.Type)
        {
        case ACPI_PTYPE2:
        case ACPI_PTYPE2_PKG_COUNT:
        case ACPI_PTYPE2_REV_FIXED:

            /* Each subpackage has a fixed number of elements */

            ExpectedCount = Package->RetInfo.Count1 + Package->RetInfo.Count2;
            if (SubPackage->Package.Count < ExpectedCount)
            {
                goto PackageTooSmall;
            }

            Status = AcpiNsCheckPackageElements (Info, SubElements,
                Package->RetInfo.ObjectType1,
                Package->RetInfo.Count1,
                Package->RetInfo.ObjectType2,
                Package->RetInfo.Count2, 0);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }
            break;

        case ACPI_PTYPE2_FIX_VAR:
            /*
             * Each subpackage has a fixed number of elements and an
             * optional element
             */
            ExpectedCount = Package->RetInfo.Count1 + Package->RetInfo.Count2;
            if (SubPackage->Package.Count < ExpectedCount)
            {
                goto PackageTooSmall;
            }

            Status = AcpiNsCheckPackageElements (Info, SubElements,
                Package->RetInfo.ObjectType1,
                Package->RetInfo.Count1,
                Package->RetInfo.ObjectType2,
                SubPackage->Package.Count - Package->RetInfo.Count1, 0);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }
            break;

        case ACPI_PTYPE2_VAR_VAR:
            /*
             * Each subpackage has a fixed or variable number of elements
             */
            break;

        case ACPI_PTYPE2_FIXED:

            /* Each subpackage has a fixed length */

            ExpectedCount = Package->RetInfo2.Count;
            if (SubPackage->Package.Count < ExpectedCount)
            {
                goto PackageTooSmall;
            }

            /* Check the type of each subpackage element */

            for (j = 0; j < ExpectedCount; j++)
            {
                Status = AcpiNsCheckObjectType (Info, &SubElements[j],
                    Package->RetInfo2.ObjectType[j], j);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
            }
            break;

        case ACPI_PTYPE2_MIN:

            /* Each subpackage has a variable but minimum length */

            ExpectedCount = Package->RetInfo.Count1;
            if (SubPackage->Package.Count < ExpectedCount)
            {
                goto PackageTooSmall;
            }

            /* Check the type of each subpackage element */

            Status = AcpiNsCheckPackageElements (Info, SubElements,
                Package->RetInfo.ObjectType1,
                SubPackage->Package.Count, 0, 0, 0);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }
            break;

        case ACPI_PTYPE2_COUNT:
            /*
             * First element is the (Integer) count of elements, including
             * the count field (the ACPI name is NumElements)
             */
            Status = AcpiNsCheckObjectType (Info, SubElements,
                ACPI_RTYPE_INTEGER, 0);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            /*
             * Make sure package is large enough for the Count and is
             * is as large as the minimum size
             */
            ExpectedCount = (UINT32) (*SubElements)->Integer.Value;
            if (SubPackage->Package.Count < ExpectedCount)
            {
                goto PackageTooSmall;
            }

            if (SubPackage->Package.Count < Package->RetInfo.Count1)
            {
                ExpectedCount = Package->RetInfo.Count1;
                goto PackageTooSmall;
            }

            if (ExpectedCount == 0)
            {
                /*
                 * Either the NumEntries element was originally zero or it was
                 * a NULL element and repaired to an Integer of value zero.
                 * In either case, repair it by setting NumEntries to be the
                 * actual size of the subpackage.
                 */
                ExpectedCount = SubPackage->Package.Count;
                (*SubElements)->Integer.Value = ExpectedCount;
            }

            /* Check the type of each subpackage element */

            Status = AcpiNsCheckPackageElements (Info, (SubElements + 1),
                Package->RetInfo.ObjectType1,
                (ExpectedCount - 1), 0, 0, 1);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }
            break;

        default: /* Should not get here, type was validated by caller */

            ACPI_ERROR ((AE_INFO, "Invalid Package type: %X",
                Package->RetInfo.Type));
            return (AE_AML_INTERNAL);
        }

        Elements++;
    }

    return (AE_OK);


PackageTooSmall:

    /* The subpackage count was smaller than required */

    ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
        "Return SubPackage[%u] is too small - found %u elements, expected %u",
        i, SubPackage->Package.Count, ExpectedCount));

    return (AE_AML_OPERAND_VALUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsCustomPackage
 *
 * PARAMETERS:  Info                - Method execution information block
 *              Elements            - Pointer to the package elements array
 *              Count               - Element count for the package
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check a returned package object for the correct count and
 *              correct type of all sub-objects.
 *
 * NOTE: Currently used for the _BIX method only. When needed for two or more
 * methods, probably a detect/dispatch mechanism will be required.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiNsCustomPackage (
    ACPI_EVALUATE_INFO          *Info,
    ACPI_OPERAND_OBJECT         **Elements,
    UINT32                      Count)
{
    UINT32                      ExpectedCount;
    UINT32                      Version;
    ACPI_STATUS                 Status = AE_OK;


    ACPI_FUNCTION_NAME (NsCustomPackage);


    /* Get version number, must be Integer */

    if ((*Elements)->Common.Type != ACPI_TYPE_INTEGER)
    {
        ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
            "Return Package has invalid object type for version number"));
        return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
    }

    Version = (UINT32) (*Elements)->Integer.Value;
    ExpectedCount = 21;         /* Version 1 */

    if (Version == 0)
    {
        ExpectedCount = 20;     /* Version 0 */
    }

    if (Count < ExpectedCount)
    {
        ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, Info->NodeFlags,
            "Return Package is too small - found %u elements, expected %u",
            Count, ExpectedCount));
        return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
    }
    else if (Count > ExpectedCount)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_REPAIR,
            "%s: Return Package is larger than needed - "
            "found %u, expected %u\n",
            Info->FullPathname, Count, ExpectedCount));
    }

    /* Validate all elements of the returned package */

    Status = AcpiNsCheckPackageElements (Info, Elements,
        ACPI_RTYPE_INTEGER, 16,
        ACPI_RTYPE_STRING, 4, 0);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Version 1 has a single trailing integer */

    if (Version > 0)
    {
        Status = AcpiNsCheckPackageElements (Info, Elements + 20,
            ACPI_RTYPE_INTEGER, 1, 0, 0, 20);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsCheckPackageElements
 *
 * PARAMETERS:  Info            - Method execution information block
 *              Elements        - Pointer to the package elements array
 *              Type1           - Object type for first group
 *              Count1          - Count for first group
 *              Type2           - Object type for second group
 *              Count2          - Count for second group
 *              StartIndex      - Start of the first group of elements
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check that all elements of a package are of the correct object
 *              type. Supports up to two groups of different object types.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiNsCheckPackageElements (
    ACPI_EVALUATE_INFO          *Info,
    ACPI_OPERAND_OBJECT         **Elements,
    UINT8                       Type1,
    UINT32                      Count1,
    UINT8                       Type2,
    UINT32                      Count2,
    UINT32                      StartIndex)
{
    ACPI_OPERAND_OBJECT         **ThisElement = Elements;
    ACPI_STATUS                 Status;
    UINT32                      i;


    /*
     * Up to two groups of package elements are supported by the data
     * structure. All elements in each group must be of the same type.
     * The second group can have a count of zero.
     */
    for (i = 0; i < Count1; i++)
    {
        Status = AcpiNsCheckObjectType (Info, ThisElement,
            Type1, i + StartIndex);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ThisElement++;
    }

    for (i = 0; i < Count2; i++)
    {
        Status = AcpiNsCheckObjectType (Info, ThisElement,
            Type2, (i + Count1 + StartIndex));
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ThisElement++;
    }

    return (AE_OK);
}
