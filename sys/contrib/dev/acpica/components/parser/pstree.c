/******************************************************************************
 *
 * Module Name: pstree - Parser op tree manipulation/traversal/search
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acconvert.h>

#define _COMPONENT          ACPI_PARSER
        ACPI_MODULE_NAME    ("pstree")

/* Local prototypes */

#ifdef ACPI_OBSOLETE_FUNCTIONS
ACPI_PARSE_OBJECT *
AcpiPsGetChild (
    ACPI_PARSE_OBJECT       *op);
#endif


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsGetArg
 *
 * PARAMETERS:  Op              - Get an argument for this op
 *              Argn            - Nth argument to get
 *
 * RETURN:      The argument (as an Op object). NULL if argument does not exist
 *
 * DESCRIPTION: Get the specified op's argument.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
AcpiPsGetArg (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Argn)
{
    ACPI_PARSE_OBJECT       *Arg = NULL;
    const ACPI_OPCODE_INFO  *OpInfo;


    ACPI_FUNCTION_ENTRY ();

/*
    if (Op->Common.AmlOpcode == AML_INT_CONNECTION_OP)
    {
        return (Op->Common.Value.Arg);
    }
*/
    /* Get the info structure for this opcode */

    OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);
    if (OpInfo->Class == AML_CLASS_UNKNOWN)
    {
        /* Invalid opcode or ASCII character */

        return (NULL);
    }

    /* Check if this opcode requires argument sub-objects */

    if (!(OpInfo->Flags & AML_HAS_ARGS))
    {
        /* Has no linked argument objects */

        return (NULL);
    }

    /* Get the requested argument object */

    Arg = Op->Common.Value.Arg;
    while (Arg && Argn)
    {
        Argn--;
        Arg = Arg->Common.Next;
    }

    return (Arg);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsAppendArg
 *
 * PARAMETERS:  Op              - Append an argument to this Op.
 *              Arg             - Argument Op to append
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Append an argument to an op's argument list (a NULL arg is OK)
 *
 ******************************************************************************/

void
AcpiPsAppendArg (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *Arg)
{
    ACPI_PARSE_OBJECT       *PrevArg;
    const ACPI_OPCODE_INFO  *OpInfo;


    ACPI_FUNCTION_TRACE (PsAppendArg);


    if (!Op)
    {
        return_VOID;
    }

    /* Get the info structure for this opcode */

    OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);
    if (OpInfo->Class == AML_CLASS_UNKNOWN)
    {
        /* Invalid opcode */

        ACPI_ERROR ((AE_INFO, "Invalid AML Opcode: 0x%2.2X",
            Op->Common.AmlOpcode));
        return_VOID;
    }

    /* Check if this opcode requires argument sub-objects */

    if (!(OpInfo->Flags & AML_HAS_ARGS))
    {
        /* Has no linked argument objects */

        return_VOID;
    }

    /* Append the argument to the linked argument list */

    if (Op->Common.Value.Arg)
    {
        /* Append to existing argument list */

        PrevArg = Op->Common.Value.Arg;
        while (PrevArg->Common.Next)
        {
            PrevArg = PrevArg->Common.Next;
        }
        PrevArg->Common.Next = Arg;
    }
    else
    {
        /* No argument list, this will be the first argument */

        Op->Common.Value.Arg = Arg;
    }

    /* Set the parent in this arg and any args linked after it */

    while (Arg)
    {
        Arg->Common.Parent = Op;
        Arg = Arg->Common.Next;

        Op->Common.ArgListLength++;
    }

    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsGetDepthNext
 *
 * PARAMETERS:  Origin          - Root of subtree to search
 *              Op              - Last (previous) Op that was found
 *
 * RETURN:      Next Op found in the search.
 *
 * DESCRIPTION: Get next op in tree (walking the tree in depth-first order)
 *              Return NULL when reaching "origin" or when walking up from root
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
AcpiPsGetDepthNext (
    ACPI_PARSE_OBJECT       *Origin,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next = NULL;
    ACPI_PARSE_OBJECT       *Parent;
    ACPI_PARSE_OBJECT       *Arg;


    ACPI_FUNCTION_ENTRY ();


    if (!Op)
    {
        return (NULL);
    }

    /* Look for an argument or child */

    Next = AcpiPsGetArg (Op, 0);
    if (Next)
    {
        ASL_CV_LABEL_FILENODE (Next);
        return (Next);
    }

    /* Look for a sibling */

    Next = Op->Common.Next;
    if (Next)
    {
        ASL_CV_LABEL_FILENODE (Next);
        return (Next);
    }

    /* Look for a sibling of parent */

    Parent = Op->Common.Parent;

    while (Parent)
    {
        Arg = AcpiPsGetArg (Parent, 0);
        while (Arg && (Arg != Origin) && (Arg != Op))
        {

            ASL_CV_LABEL_FILENODE (Arg);
            Arg = Arg->Common.Next;
        }

        if (Arg == Origin)
        {
            /* Reached parent of origin, end search */

            return (NULL);
        }

        if (Parent->Common.Next)
        {
            /* Found sibling of parent */

            ASL_CV_LABEL_FILENODE (Parent->Common.Next);
            return (Parent->Common.Next);
        }

        Op = Parent;
        Parent = Parent->Common.Parent;
    }

    ASL_CV_LABEL_FILENODE (Next);
    return (Next);
}


#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    AcpiPsGetChild
 *
 * PARAMETERS:  Op              - Get the child of this Op
 *
 * RETURN:      Child Op, Null if none is found.
 *
 * DESCRIPTION: Get op's children or NULL if none
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
AcpiPsGetChild (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Child = NULL;


    ACPI_FUNCTION_ENTRY ();


    switch (Op->Common.AmlOpcode)
    {
    case AML_SCOPE_OP:
    case AML_ELSE_OP:
    case AML_DEVICE_OP:
    case AML_THERMAL_ZONE_OP:
    case AML_INT_METHODCALL_OP:

        Child = AcpiPsGetArg (Op, 0);
        break;

    case AML_BUFFER_OP:
    case AML_PACKAGE_OP:
    case AML_VARIABLE_PACKAGE_OP:
    case AML_METHOD_OP:
    case AML_IF_OP:
    case AML_WHILE_OP:
    case AML_FIELD_OP:

        Child = AcpiPsGetArg (Op, 1);
        break;

    case AML_POWER_RESOURCE_OP:
    case AML_INDEX_FIELD_OP:

        Child = AcpiPsGetArg (Op, 2);
        break;

    case AML_PROCESSOR_OP:
    case AML_BANK_FIELD_OP:

        Child = AcpiPsGetArg (Op, 3);
        break;

    default:

        /* All others have no children */

        break;
    }

    return (Child);
}
#endif
