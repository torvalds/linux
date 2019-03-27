/******************************************************************************
 *
 * Module Name: utpredef - support functions for predefined names
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
#include <contrib/dev/acpica/include/acpredef.h>


#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utpredef")


/*
 * Names for the types that can be returned by the predefined objects.
 * Used for warning messages. Must be in the same order as the ACPI_RTYPEs
 */
static const char   *UtRtypeNames[] =
{
    "/Integer",
    "/String",
    "/Buffer",
    "/Package",
    "/Reference",
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetNextPredefinedMethod
 *
 * PARAMETERS:  ThisName            - Entry in the predefined method/name table
 *
 * RETURN:      Pointer to next entry in predefined table.
 *
 * DESCRIPTION: Get the next entry in the predefine method table. Handles the
 *              cases where a package info entry follows a method name that
 *              returns a package.
 *
 ******************************************************************************/

const ACPI_PREDEFINED_INFO *
AcpiUtGetNextPredefinedMethod (
    const ACPI_PREDEFINED_INFO  *ThisName)
{

    /*
     * Skip next entry in the table if this name returns a Package
     * (next entry contains the package info)
     */
    if ((ThisName->Info.ExpectedBtypes & ACPI_RTYPE_PACKAGE) &&
        (ThisName->Info.ExpectedBtypes != ACPI_RTYPE_ALL))
    {
        ThisName++;
    }

    ThisName++;
    return (ThisName);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtMatchPredefinedMethod
 *
 * PARAMETERS:  Name                - Name to find
 *
 * RETURN:      Pointer to entry in predefined table. NULL indicates not found.
 *
 * DESCRIPTION: Check an object name against the predefined object list.
 *
 ******************************************************************************/

const ACPI_PREDEFINED_INFO *
AcpiUtMatchPredefinedMethod (
    char                        *Name)
{
    const ACPI_PREDEFINED_INFO  *ThisName;


    /* Quick check for a predefined name, first character must be underscore */

    if (Name[0] != '_')
    {
        return (NULL);
    }

    /* Search info table for a predefined method/object name */

    ThisName = AcpiGbl_PredefinedMethods;
    while (ThisName->Info.Name[0])
    {
        if (ACPI_COMPARE_NAME (Name, ThisName->Info.Name))
        {
            return (ThisName);
        }

        ThisName = AcpiUtGetNextPredefinedMethod (ThisName);
    }

    return (NULL); /* Not found */
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetExpectedReturnTypes
 *
 * PARAMETERS:  Buffer              - Where the formatted string is returned
 *              ExpectedBTypes      - Bitfield of expected data types
 *
 * RETURN:      Formatted string in Buffer.
 *
 * DESCRIPTION: Format the expected object types into a printable string.
 *
 ******************************************************************************/

void
AcpiUtGetExpectedReturnTypes (
    char                    *Buffer,
    UINT32                  ExpectedBtypes)
{
    UINT32                  ThisRtype;
    UINT32                  i;
    UINT32                  j;


    if (!ExpectedBtypes)
    {
        strcpy (Buffer, "NONE");
        return;
    }

    j = 1;
    Buffer[0] = 0;
    ThisRtype = ACPI_RTYPE_INTEGER;

    for (i = 0; i < ACPI_NUM_RTYPES; i++)
    {
        /* If one of the expected types, concatenate the name of this type */

        if (ExpectedBtypes & ThisRtype)
        {
            strcat (Buffer, &UtRtypeNames[i][j]);
            j = 0;              /* Use name separator from now on */
        }

        ThisRtype <<= 1;    /* Next Rtype */
    }
}


/*******************************************************************************
 *
 * The remaining functions are used by iASL and AcpiHelp only
 *
 ******************************************************************************/

#if (defined ACPI_ASL_COMPILER || defined ACPI_HELP_APP)

/* Local prototypes */

static UINT32
AcpiUtGetArgumentTypes (
    char                    *Buffer,
    UINT16                  ArgumentTypes);


/* Types that can be returned externally by a predefined name */

static const char   *UtExternalTypeNames[] = /* Indexed by ACPI_TYPE_* */
{
    ", UNSUPPORTED-TYPE",
    ", Integer",
    ", String",
    ", Buffer",
    ", Package"
};

/* Bit widths for resource descriptor predefined names */

static const char   *UtResourceTypeNames[] =
{
    "/1",
    "/2",
    "/3",
    "/8",
    "/16",
    "/32",
    "/64",
    "/variable",
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtMatchResourceName
 *
 * PARAMETERS:  Name                - Name to find
 *
 * RETURN:      Pointer to entry in the resource table. NULL indicates not
 *              found.
 *
 * DESCRIPTION: Check an object name against the predefined resource
 *              descriptor object list.
 *
 ******************************************************************************/

const ACPI_PREDEFINED_INFO *
AcpiUtMatchResourceName (
    char                        *Name)
{
    const ACPI_PREDEFINED_INFO  *ThisName;


    /*
     * Quick check for a predefined name, first character must
     * be underscore
     */
    if (Name[0] != '_')
    {
        return (NULL);
    }

    /* Search info table for a predefined method/object name */

    ThisName = AcpiGbl_ResourceNames;
    while (ThisName->Info.Name[0])
    {
        if (ACPI_COMPARE_NAME (Name, ThisName->Info.Name))
        {
            return (ThisName);
        }

        ThisName++;
    }

    return (NULL); /* Not found */
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDisplayPredefinedMethod
 *
 * PARAMETERS:  Buffer              - Scratch buffer for this function
 *              ThisName            - Entry in the predefined method/name table
 *              MultiLine           - TRUE if output should be on >1 line
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display information about a predefined method. Number and
 *              type of the input arguments, and expected type(s) for the
 *              return value, if any.
 *
 ******************************************************************************/

void
AcpiUtDisplayPredefinedMethod (
    char                        *Buffer,
    const ACPI_PREDEFINED_INFO  *ThisName,
    BOOLEAN                     MultiLine)
{
    UINT32                      ArgCount;

    /*
     * Get the argument count and the string buffer
     * containing all argument types
     */
    ArgCount = AcpiUtGetArgumentTypes (Buffer,
        ThisName->Info.ArgumentList);

    if (MultiLine)
    {
        printf ("      ");
    }

    printf ("%4.4s    Requires %s%u argument%s",
        ThisName->Info.Name,
        (ThisName->Info.ArgumentList & ARG_COUNT_IS_MINIMUM) ?
            "(at least) " : "",
        ArgCount, ArgCount != 1 ? "s" : "");

    /* Display the types for any arguments */

    if (ArgCount > 0)
    {
        printf (" (%s)", Buffer);
    }

    if (MultiLine)
    {
        printf ("\n    ");
    }

    /* Get the return value type(s) allowed */

    if (ThisName->Info.ExpectedBtypes)
    {
        AcpiUtGetExpectedReturnTypes (Buffer, ThisName->Info.ExpectedBtypes);
        printf ("  Return value types: %s\n", Buffer);
    }
    else
    {
        printf ("  No return value\n");
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetArgumentTypes
 *
 * PARAMETERS:  Buffer              - Where to return the formatted types
 *              ArgumentTypes       - Types field for this method
 *
 * RETURN:      Count - the number of arguments required for this method
 *
 * DESCRIPTION: Format the required data types for this method (Integer,
 *              String, Buffer, or Package) and return the required argument
 *              count.
 *
 ******************************************************************************/

static UINT32
AcpiUtGetArgumentTypes (
    char                    *Buffer,
    UINT16                  ArgumentTypes)
{
    UINT16                  ThisArgumentType;
    UINT16                  SubIndex;
    UINT16                  ArgCount;
    UINT32                  i;


    *Buffer = 0;
    SubIndex = 2;

    /* First field in the types list is the count of args to follow */

    ArgCount = METHOD_GET_ARG_COUNT (ArgumentTypes);
    if (ArgCount > METHOD_PREDEF_ARGS_MAX)
    {
        printf ("**** Invalid argument count (%u) "
            "in predefined info structure\n", ArgCount);
        return (ArgCount);
    }

    /* Get each argument from the list, convert to ascii, store to buffer */

    for (i = 0; i < ArgCount; i++)
    {
        ThisArgumentType = METHOD_GET_NEXT_TYPE (ArgumentTypes);

        if (!ThisArgumentType || (ThisArgumentType > METHOD_MAX_ARG_TYPE))
        {
            printf ("**** Invalid argument type (%u) "
                "in predefined info structure\n", ThisArgumentType);
            return (ArgCount);
        }

        strcat (Buffer, UtExternalTypeNames[ThisArgumentType] + SubIndex);
        SubIndex = 0;
    }

    return (ArgCount);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetResourceBitWidth
 *
 * PARAMETERS:  Buffer              - Where the formatted string is returned
 *              Types               - Bitfield of expected data types
 *
 * RETURN:      Count of return types. Formatted string in Buffer.
 *
 * DESCRIPTION: Format the resource bit widths into a printable string.
 *
 ******************************************************************************/

UINT32
AcpiUtGetResourceBitWidth (
    char                    *Buffer,
    UINT16                  Types)
{
    UINT32                  i;
    UINT16                  SubIndex;
    UINT32                  Found;


    *Buffer = 0;
    SubIndex = 1;
    Found = 0;

    for (i = 0; i < NUM_RESOURCE_WIDTHS; i++)
    {
        if (Types & 1)
        {
            strcat (Buffer, &(UtResourceTypeNames[i][SubIndex]));
            SubIndex = 0;
            Found++;
        }

        Types >>= 1;
    }

    return (Found);
}
#endif
