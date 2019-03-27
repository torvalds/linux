/******************************************************************************
 *
 * Module Name: aslpredef - support for ACPI predefined names
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
#define ACPI_CREATE_RESOURCE_TABLE

#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include "aslcompiler.y.h"
#include <contrib/dev/acpica/include/acpredef.h>
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslpredef")


/* Local prototypes */

static void
ApCheckForUnexpectedReturnValue (
    ACPI_PARSE_OBJECT       *Op,
    ASL_METHOD_INFO         *MethodInfo);

static UINT32
ApCheckForSpecialName (
    ACPI_PARSE_OBJECT       *Op,
    char                    *Name);


/*******************************************************************************
 *
 * FUNCTION:    ApCheckForPredefinedMethod
 *
 * PARAMETERS:  Op              - A parse node of type "METHOD".
 *              MethodInfo      - Saved info about this method
 *
 * RETURN:      None
 *
 * DESCRIPTION: If method is a predefined name, check that the number of
 *              arguments and the return type (returns a value or not)
 *              is correct.
 *
 ******************************************************************************/

BOOLEAN
ApCheckForPredefinedMethod (
    ACPI_PARSE_OBJECT       *Op,
    ASL_METHOD_INFO         *MethodInfo)
{
    UINT32                      Index;
    UINT32                      RequiredArgCount;
    const ACPI_PREDEFINED_INFO  *ThisName;


    /* Check for a match against the predefined name list */

    Index = ApCheckForPredefinedName (Op, Op->Asl.NameSeg);

    switch (Index)
    {
    case ACPI_NOT_RESERVED_NAME:        /* No underscore or _Txx or _xxx name not matched */
    case ACPI_PREDEFINED_NAME:          /* Resource Name or reserved scope name */
    case ACPI_COMPILER_RESERVED_NAME:   /* A _Txx that was not emitted by compiler */

        /* Just return, nothing to do */
        return (FALSE);


    case ACPI_EVENT_RESERVED_NAME:      /* _Lxx/_Exx/_Wxx/_Qxx methods */

        AslGbl_ReservedMethods++;

        /* NumArguments must be zero for all _Lxx/_Exx/_Wxx/_Qxx methods */

        if (MethodInfo->NumArguments != 0)
        {
            sprintf (AslGbl_MsgBuffer, "%s requires %d", Op->Asl.ExternalName, 0);

            AslError (ASL_WARNING, ASL_MSG_RESERVED_ARG_COUNT_HI, Op,
                AslGbl_MsgBuffer);
        }
        break;


    default:
        /*
         * Matched a predefined method name - validate the ASL-defined
         * argument count against the ACPI specification.
         *
         * Some methods are allowed to have a "minimum" number of args
         * (_SCP) because their definition in ACPI has changed over time.
         */
        AslGbl_ReservedMethods++;
        ThisName = &AcpiGbl_PredefinedMethods[Index];
        RequiredArgCount = METHOD_GET_ARG_COUNT (ThisName->Info.ArgumentList);

        if (MethodInfo->NumArguments != RequiredArgCount)
        {
            sprintf (AslGbl_MsgBuffer, "%4.4s requires %u",
                ThisName->Info.Name, RequiredArgCount);

            if (MethodInfo->NumArguments < RequiredArgCount)
            {
                AslError (ASL_WARNING, ASL_MSG_RESERVED_ARG_COUNT_LO, Op,
                    AslGbl_MsgBuffer);
            }
            else if ((MethodInfo->NumArguments > RequiredArgCount) &&
                !(ThisName->Info.ArgumentList & ARG_COUNT_IS_MINIMUM))
            {
                AslError (ASL_WARNING, ASL_MSG_RESERVED_ARG_COUNT_HI, Op,
                    AslGbl_MsgBuffer);
            }
        }

        /*
         * Check if method returns no value, but the predefined name is
         * required to return a value
         */
        if (MethodInfo->NumReturnNoValue &&
            ThisName->Info.ExpectedBtypes)
        {
            AcpiUtGetExpectedReturnTypes (AslGbl_StringBuffer,
                ThisName->Info.ExpectedBtypes);

            sprintf (AslGbl_MsgBuffer, "%s required for %4.4s",
                AslGbl_StringBuffer, ThisName->Info.Name);

            AslError (ASL_WARNING, ASL_MSG_RESERVED_RETURN_VALUE, Op,
                AslGbl_MsgBuffer);
        }
        break;
    }

    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    ApCheckForUnexpectedReturnValue
 *
 * PARAMETERS:  Op              - A parse node of type "RETURN".
 *              MethodInfo      - Saved info about this method
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check for an unexpected return value from a predefined method.
 *              Invoked for predefined methods that are defined to not return
 *              any value. If there is a return value, issue a remark, since
 *              the ASL writer may be confused as to the method definition
 *              and/or functionality.
 *
 * Note: We ignore all return values of "Zero", since this is what a standalone
 *       Return() statement will always generate -- so we ignore it here --
 *       i.e., there is no difference between Return() and Return(Zero).
 *       Also, a null Return() will be disassembled to return(Zero) -- so, we
 *       don't want to generate extraneous remarks/warnings for a disassembled
 *       ASL file.
 *
 ******************************************************************************/

static void
ApCheckForUnexpectedReturnValue (
    ACPI_PARSE_OBJECT       *Op,
    ASL_METHOD_INFO         *MethodInfo)
{
    ACPI_PARSE_OBJECT       *ReturnValueOp;


    /* Ignore Return() and Return(Zero) (they are the same) */

    ReturnValueOp = Op->Asl.Child;
    if (ReturnValueOp->Asl.ParseOpcode == PARSEOP_ZERO)
    {
        return;
    }

    /* We have a valid return value, but the reserved name did not expect it */

    AslError (ASL_WARNING, ASL_MSG_RESERVED_NO_RETURN_VAL,
        Op, MethodInfo->Op->Asl.ExternalName);
}


/*******************************************************************************
 *
 * FUNCTION:    ApCheckPredefinedReturnValue
 *
 * PARAMETERS:  Op              - A parse node of type "RETURN".
 *              MethodInfo      - Saved info about this method
 *
 * RETURN:      None
 *
 * DESCRIPTION: If method is a predefined name, attempt to validate the return
 *              value. Only "static" types can be validated - a simple return
 *              of an integer/string/buffer/package or a named reference to
 *              a static object. Values such as a Localx or Argx or a control
 *              method invocation are not checked. Issue a warning if there is
 *              a valid return value, but the reserved method defines no
 *              return value.
 *
 ******************************************************************************/

void
ApCheckPredefinedReturnValue (
    ACPI_PARSE_OBJECT       *Op,
    ASL_METHOD_INFO         *MethodInfo)
{
    UINT32                      Index;
    ACPI_PARSE_OBJECT           *ReturnValueOp;
    const ACPI_PREDEFINED_INFO  *ThisName;


    /*
     * Check parent method for a match against the predefined name list.
     *
     * Note: Disable compiler errors/warnings because any errors will be
     * caught when analyzing the parent method. Eliminates duplicate errors.
     */
    AslGbl_AllExceptionsDisabled = TRUE;
    Index = ApCheckForPredefinedName (MethodInfo->Op,
        MethodInfo->Op->Asl.NameSeg);
    AslGbl_AllExceptionsDisabled = FALSE;

    switch (Index)
    {
    case ACPI_EVENT_RESERVED_NAME:      /* _Lxx/_Exx/_Wxx/_Qxx methods */

        /* No return value expected, warn if there is one */

        ApCheckForUnexpectedReturnValue (Op, MethodInfo);
        return;

    case ACPI_NOT_RESERVED_NAME:        /* No underscore or _Txx or _xxx name not matched */
    case ACPI_PREDEFINED_NAME:          /* Resource Name or reserved scope name */
    case ACPI_COMPILER_RESERVED_NAME:   /* A _Txx that was not emitted by compiler */

        /* Just return, nothing to do */
        return;

    default: /* A standard predefined ACPI name */

        ThisName = &AcpiGbl_PredefinedMethods[Index];
        if (!ThisName->Info.ExpectedBtypes)
        {
            /* No return value expected, warn if there is one */

            ApCheckForUnexpectedReturnValue (Op, MethodInfo);
            return;
        }

        /* Get the object returned, it is the next argument */

        ReturnValueOp = Op->Asl.Child;
        switch (ReturnValueOp->Asl.ParseOpcode)
        {
        case PARSEOP_ZERO:
        case PARSEOP_ONE:
        case PARSEOP_ONES:
        case PARSEOP_INTEGER:
        case PARSEOP_STRING_LITERAL:
        case PARSEOP_BUFFER:
        case PARSEOP_PACKAGE:

            /* Static data return object - check against expected type */

            ApCheckObjectType (ThisName->Info.Name, ReturnValueOp,
                ThisName->Info.ExpectedBtypes, ACPI_NOT_PACKAGE_ELEMENT);

            /* For packages, check the individual package elements */

            if (ReturnValueOp->Asl.ParseOpcode == PARSEOP_PACKAGE)
            {
                ApCheckPackage (ReturnValueOp, ThisName);
            }
            break;

        default:
            /*
             * All other ops are very difficult or impossible to typecheck at
             * compile time. These include all Localx, Argx, and method
             * invocations. Also, NAMESEG and NAMESTRING because the type of
             * any named object can be changed at runtime (for example,
             * CopyObject will change the type of the target object.)
             */
            break;
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    ApCheckForPredefinedObject
 *
 * PARAMETERS:  Op              - A parse node
 *              Name            - The ACPI name to be checked
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check for a predefined name for a static object (created via
 *              the ASL Name operator). If it is a predefined ACPI name, ensure
 *              that the name does not require any arguments (which would
 *              require a control method implementation of the name), and that
 *              the type of the object is one of the expected types for the
 *              predefined name.
 *
 ******************************************************************************/

void
ApCheckForPredefinedObject (
    ACPI_PARSE_OBJECT       *Op,
    char                    *Name)
{
    UINT32                      Index;
    ACPI_PARSE_OBJECT           *ObjectOp;
    const ACPI_PREDEFINED_INFO  *ThisName;


    /*
     * Check for a real predefined name -- not a resource descriptor name
     * or a predefined scope name
     */
    Index = ApCheckForPredefinedName (Op, Name);

    switch (Index)
    {
    case ACPI_NOT_RESERVED_NAME:        /* No underscore or _Txx or _xxx name not matched */
    case ACPI_PREDEFINED_NAME:          /* Resource Name or reserved scope name */
    case ACPI_COMPILER_RESERVED_NAME:   /* A _Txx that was not emitted by compiler */

        /* Nothing to do */
        return;

    case ACPI_EVENT_RESERVED_NAME:      /* _Lxx/_Exx/_Wxx/_Qxx methods */

        /*
         * These names must be control methods, by definition in ACPI spec.
         * Also because they are defined to return no value. None of them
         * require any arguments.
         */
        AslError (ASL_ERROR, ASL_MSG_RESERVED_METHOD, Op,
            "with zero arguments");
        return;

    default:

        break;
    }

    /* A standard predefined ACPI name */

    /*
     * If this predefined name requires input arguments, then
     * it must be implemented as a control method
     */
    ThisName = &AcpiGbl_PredefinedMethods[Index];
    if (METHOD_GET_ARG_COUNT (ThisName->Info.ArgumentList) > 0)
    {
        AslError (ASL_ERROR, ASL_MSG_RESERVED_METHOD, Op,
            "with arguments");
        return;
    }

    /*
     * If no return value is expected from this predefined name, then
     * it follows that it must be implemented as a control method
     * (with zero args, because the args > 0 case was handled above)
     * Examples are: _DIS, _INI, _IRC, _OFF, _ON, _PSx
     */
    if (!ThisName->Info.ExpectedBtypes)
    {
        AslError (ASL_ERROR, ASL_MSG_RESERVED_METHOD, Op,
            "with zero arguments");
        return;
    }

    /* Typecheck the actual object, it is the next argument */

    ObjectOp = Op->Asl.Child->Asl.Next;
    ApCheckObjectType (ThisName->Info.Name, Op->Asl.Child->Asl.Next,
        ThisName->Info.ExpectedBtypes, ACPI_NOT_PACKAGE_ELEMENT);

    /* For packages, check the individual package elements */

    if (ObjectOp->Asl.ParseOpcode == PARSEOP_PACKAGE)
    {
        ApCheckPackage (ObjectOp, ThisName);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    ApCheckForPredefinedName
 *
 * PARAMETERS:  Op              - A parse node
 *              Name            - NameSeg to check
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check a NameSeg against the reserved list.
 *
 ******************************************************************************/

UINT32
ApCheckForPredefinedName (
    ACPI_PARSE_OBJECT       *Op,
    char                    *Name)
{
    UINT32                      i;
    const ACPI_PREDEFINED_INFO  *ThisName;


    if (Name[0] == 0)
    {
        AslError (ASL_ERROR, ASL_MSG_COMPILER_INTERNAL, Op,
            "zero length name found");
    }

    /* All reserved names are prefixed with a single underscore */

    if (Name[0] != '_')
    {
        return (ACPI_NOT_RESERVED_NAME);
    }

    /* Check for a standard predefined method name */

    ThisName = AcpiGbl_PredefinedMethods;
    for (i = 0; ThisName->Info.Name[0]; i++)
    {
        if (ACPI_COMPARE_NAME (Name, ThisName->Info.Name))
        {
            /* Return index into predefined array */
            return (i);
        }

        ThisName++; /* Does not account for extra package data, but is OK */
    }

    /* Check for resource names and predefined scope names */

    ThisName = AcpiGbl_ResourceNames;
    while (ThisName->Info.Name[0])
    {
        if (ACPI_COMPARE_NAME (Name, ThisName->Info.Name))
        {
            return (ACPI_PREDEFINED_NAME);
        }

        ThisName++;
    }

    ThisName = AcpiGbl_ScopeNames;
    while (ThisName->Info.Name[0])
    {
        if (ACPI_COMPARE_NAME (Name, ThisName->Info.Name))
        {
            return (ACPI_PREDEFINED_NAME);
        }

        ThisName++;
    }

    /* Check for _Lxx/_Exx/_Wxx/_Qxx/_T_x. Warning if unknown predefined name */

    return (ApCheckForSpecialName (Op, Name));
}


/*******************************************************************************
 *
 * FUNCTION:    ApCheckForSpecialName
 *
 * PARAMETERS:  Op              - A parse node
 *              Name            - NameSeg to check
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check for the "special" predefined names -
 *              _Lxx, _Exx, _Qxx, _Wxx, and _T_x
 *
 ******************************************************************************/

static UINT32
ApCheckForSpecialName (
    ACPI_PARSE_OBJECT       *Op,
    char                    *Name)
{

    /*
     * Check for the "special" predefined names. We already know that the
     * first character is an underscore.
     *   GPE:  _Lxx
     *   GPE:  _Exx
     *   GPE:  _Wxx
     *   EC:   _Qxx
     */
    if ((Name[1] == 'L') ||
        (Name[1] == 'E') ||
        (Name[1] == 'W') ||
        (Name[1] == 'Q'))
    {
        /* The next two characters must be hex digits */

        if ((isxdigit ((int) Name[2])) &&
            (isxdigit ((int) Name[3])))
        {
            return (ACPI_EVENT_RESERVED_NAME);
        }
    }

    /* Check for the names reserved for the compiler itself: _T_x */

    else if ((Op->Asl.ExternalName[1] == 'T') &&
             (Op->Asl.ExternalName[2] == '_'))
    {
        /* Ignore if actually emitted by the compiler */

        if (Op->Asl.CompileFlags & OP_COMPILER_EMITTED)
        {
            return (ACPI_NOT_RESERVED_NAME);
        }

        /*
         * Was not actually emitted by the compiler. This is a special case,
         * however. If the ASL code being compiled was the result of a
         * dissasembly, it may possibly contain valid compiler-emitted names
         * of the form "_T_x". We don't want to issue an error or even a
         * warning and force the user to manually change the names. So, we
         * will issue a remark instead.
         */
        AslError (ASL_REMARK, ASL_MSG_COMPILER_RESERVED,
            Op, Op->Asl.ExternalName);
        return (ACPI_COMPILER_RESERVED_NAME);
    }

    /*
     * The name didn't match any of the known predefined names. Flag it as a
     * warning, since the entire namespace starting with an underscore is
     * reserved by the ACPI spec.
     */
    AslError (ASL_WARNING, ASL_MSG_UNKNOWN_RESERVED_NAME,
        Op, Op->Asl.ExternalName);

    return (ACPI_NOT_RESERVED_NAME);
}


/*******************************************************************************
 *
 * FUNCTION:    ApCheckObjectType
 *
 * PARAMETERS:  PredefinedName  - Name of the predefined object we are checking
 *              Op              - Current parse node
 *              ExpectedBtypes  - Bitmap of expected return type(s)
 *              PackageIndex    - Index of object within parent package (if
 *                                applicable - ACPI_NOT_PACKAGE_ELEMENT
 *                                otherwise)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check if the object type is one of the types that is expected
 *              by the predefined name. Only a limited number of object types
 *              can be returned by the predefined names.
 *
 ******************************************************************************/

ACPI_STATUS
ApCheckObjectType (
    const char              *PredefinedName,
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  ExpectedBtypes,
    UINT32                  PackageIndex)
{
    UINT32                  ReturnBtype;
    char                    *TypeName;


    if (!Op)
    {
        return (AE_TYPE);
    }

    /* Map the parse opcode to a bitmapped return type (RTYPE) */

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_ZERO:
    case PARSEOP_ONE:
    case PARSEOP_ONES:
    case PARSEOP_INTEGER:

        ReturnBtype = ACPI_RTYPE_INTEGER;
        TypeName = "Integer";
        break;

    case PARSEOP_STRING_LITERAL:

        ReturnBtype = ACPI_RTYPE_STRING;
        TypeName = "String";
        break;

    case PARSEOP_BUFFER:

        ReturnBtype = ACPI_RTYPE_BUFFER;
        TypeName = "Buffer";
        break;

    case PARSEOP_PACKAGE:
    case PARSEOP_VAR_PACKAGE:

        ReturnBtype = ACPI_RTYPE_PACKAGE;
        TypeName = "Package";
        break;

    case PARSEOP_NAMESEG:
    case PARSEOP_NAMESTRING:
        /*
         * Ignore any named references within a package object.
         *
         * For Package objects, references are allowed instead of any of the
         * standard data types (Integer/String/Buffer/Package). These
         * references are resolved at runtime. NAMESEG and NAMESTRING are
         * impossible to typecheck at compile time because the type of
         * any named object can be changed at runtime (for example,
         * CopyObject will change the type of the target object).
         */
        if (PackageIndex != ACPI_NOT_PACKAGE_ELEMENT)
        {
            return (AE_OK);
        }

        ReturnBtype = ACPI_RTYPE_REFERENCE;
        TypeName = "Reference";
        break;

    default:

        /* Not one of the supported object types */

        TypeName = UtGetOpName (Op->Asl.ParseOpcode);
        goto TypeErrorExit;
    }

    /* Exit if the object is one of the expected types */

    if (ReturnBtype & ExpectedBtypes)
    {
        return (AE_OK);
    }


TypeErrorExit:

    /* Format the expected types and emit an error message */

    AcpiUtGetExpectedReturnTypes (AslGbl_StringBuffer, ExpectedBtypes);

    if (PackageIndex == ACPI_NOT_PACKAGE_ELEMENT)
    {
        sprintf (AslGbl_MsgBuffer, "%4.4s: found %s, %s required",
            PredefinedName, TypeName, AslGbl_StringBuffer);
    }
    else
    {
        sprintf (AslGbl_MsgBuffer, "%4.4s: found %s at index %u, %s required",
            PredefinedName, TypeName, PackageIndex, AslGbl_StringBuffer);
    }

    AslError (ASL_ERROR, ASL_MSG_RESERVED_OPERAND_TYPE, Op, AslGbl_MsgBuffer);
    return (AE_TYPE);
}


/*******************************************************************************
 *
 * FUNCTION:    ApDisplayReservedNames
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump information about the ACPI predefined names and predefined
 *              resource descriptor names.
 *
 ******************************************************************************/

void
ApDisplayReservedNames (
    void)
{
    const ACPI_PREDEFINED_INFO  *ThisName;
    UINT32                      Count;
    UINT32                      NumTypes;


    /*
     * Predefined names/methods
     */
    printf ("\nPredefined Name Information\n\n");

    Count = 0;
    ThisName = AcpiGbl_PredefinedMethods;
    while (ThisName->Info.Name[0])
    {
        AcpiUtDisplayPredefinedMethod (AslGbl_MsgBuffer, ThisName, FALSE);
        Count++;
        ThisName = AcpiUtGetNextPredefinedMethod (ThisName);
    }

    printf ("%u Predefined Names are recognized\n", Count);

    /*
     * Resource Descriptor names
     */
    printf ("\nPredefined Names for Resource Descriptor Fields\n\n");

    Count = 0;
    ThisName = AcpiGbl_ResourceNames;
    while (ThisName->Info.Name[0])
    {
        NumTypes = AcpiUtGetResourceBitWidth (AslGbl_MsgBuffer,
            ThisName->Info.ArgumentList);

        printf ("%4.4s    Field is %s bits wide%s\n",
            ThisName->Info.Name, AslGbl_MsgBuffer,
            (NumTypes > 1) ? " (depending on descriptor type)" : "");

        Count++;
        ThisName++;
    }

    printf ("%u Resource Descriptor Field Names are recognized\n", Count);

    /*
     * Predefined scope names
     */
    printf ("\nPredefined Scope/Device Names (automatically created at root)\n\n");

    ThisName = AcpiGbl_ScopeNames;
    while (ThisName->Info.Name[0])
    {
        printf ("%4.4s    Scope/Device\n", ThisName->Info.Name);
        ThisName++;
    }
}
