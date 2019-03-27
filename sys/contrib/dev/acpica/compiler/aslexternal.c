/******************************************************************************
 *
 * Module Name: aslexternal - ASL External opcode compiler support
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

#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include "aslcompiler.y.h"
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslexternal")


/* Local prototypes */

static void
ExInsertArgCount (
    ACPI_PARSE_OBJECT       *Op);

static void
ExMoveExternals (
    ACPI_PARSE_OBJECT       *DefinitionBlockOp);


/*******************************************************************************
 *
 * FUNCTION:    ExDoExternal
 *
 * PARAMETERS:  Op                  - Current Parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add an External() definition to the global list. This list
 *              is used to generate External opcodes.
 *
 ******************************************************************************/

void
ExDoExternal (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *ListOp;
    ACPI_PARSE_OBJECT       *Prev;
    ACPI_PARSE_OBJECT       *Next;
    ACPI_PARSE_OBJECT       *ArgCountOp;


    ArgCountOp = Op->Asl.Child->Asl.Next->Asl.Next;
    ArgCountOp->Asl.AmlOpcode = AML_RAW_DATA_BYTE;
    ArgCountOp->Asl.ParseOpcode = PARSEOP_BYTECONST;
    ArgCountOp->Asl.Value.Integer = 0;
    UtSetParseOpName (ArgCountOp);

    /* Create new list node of arbitrary type */

    ListOp = TrAllocateOp (PARSEOP_DEFAULT_ARG);

    /* Store External node as child */

    ListOp->Asl.Child = Op;
    ListOp->Asl.Next = NULL;

    if (AslGbl_ExternalsListHead)
    {
        /* Link new External to end of list */

        Prev = AslGbl_ExternalsListHead;
        Next = Prev;
        while (Next)
        {
            Prev = Next;
            Next = Next->Asl.Next;
        }

        Prev->Asl.Next = ListOp;
    }
    else
    {
        AslGbl_ExternalsListHead = ListOp;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    ExInsertArgCount
 *
 * PARAMETERS:  Op              - Op for a method invocation
 *
 * RETURN:      None
 *
 * DESCRIPTION: Obtain the number of arguments for a control method -- from
 *              the actual invocation.
 *
 ******************************************************************************/

static void
ExInsertArgCount (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;
    ACPI_PARSE_OBJECT       *NameOp;
    ACPI_PARSE_OBJECT       *Child;
    ACPI_PARSE_OBJECT       *ArgCountOp;
    char *                  ExternalName;
    char *                  CallName;
    UINT16                  ArgCount = 0;
    ACPI_STATUS             Status;


    CallName = AcpiNsGetNormalizedPathname (Op->Asl.Node, TRUE);

    Next = AslGbl_ExternalsListHead;
    while (Next)
    {
        ArgCount = 0;

        /* Skip if External node already handled */

        if (Next->Asl.Child->Asl.CompileFlags & OP_VISITED)
        {
            Next = Next->Asl.Next;
            continue;
        }

        NameOp = Next->Asl.Child->Asl.Child;
        ExternalName = AcpiNsGetNormalizedPathname (NameOp->Asl.Node, TRUE);

        if (strcmp (CallName, ExternalName))
        {
            ACPI_FREE (ExternalName);
            Next = Next->Asl.Next;
            continue;
        }

        Next->Asl.Child->Asl.CompileFlags |= OP_VISITED;

        /*
         * Since we will reposition Externals to the Root, set Namepath
         * to the fully qualified name and recalculate the aml length
         */
        Status = UtInternalizeName (ExternalName,
            &NameOp->Asl.Value.String);

        ACPI_FREE (ExternalName);
        if (ACPI_FAILURE (Status))
        {
            AslError (ASL_ERROR, ASL_MSG_COMPILER_INTERNAL,
                NULL, "- Could not Internalize External");
            break;
        }

        NameOp->Asl.AmlLength = strlen (NameOp->Asl.Value.String);

        /* Get argument count */

        Child = Op->Asl.Child;
        while (Child)
        {
            ArgCount++;
            Child = Child->Asl.Next;
        }

        /* Setup ArgCount operand */

        ArgCountOp = Next->Asl.Child->Asl.Child->Asl.Next->Asl.Next;
        ArgCountOp->Asl.Value.Integer = ArgCount;
        break;
    }

    ACPI_FREE (CallName);
}


/*******************************************************************************
 *
 * FUNCTION:    ExAmlExternalWalkBegin
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      None
 *
 * DESCRIPTION: Parse tree walk to create external opcode list for methods.
 *
 ******************************************************************************/

ACPI_STATUS
ExAmlExternalWalkBegin (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{

    /* External list head saved in the definition block op */

    if (Op->Asl.ParseOpcode == PARSEOP_DEFINITION_BLOCK)
    {
        AslGbl_ExternalsListHead = Op->Asl.Value.Arg;
    }

    if (!AslGbl_ExternalsListHead)
    {
        return (AE_OK);
    }

    if (Op->Asl.ParseOpcode != PARSEOP_METHODCALL)
    {
        return (AE_OK);
    }

    /*
     * The NameOp child under an ExternalOp gets turned into PARSE_METHODCALL
     * by XfNamespaceLocateBegin(). Ignore these.
     */
    if (Op->Asl.Parent &&
        Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_EXTERNAL)
    {
        return (AE_OK);
    }

    ExInsertArgCount (Op);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    ExAmlExternalWalkEnd
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      None
 *
 * DESCRIPTION: Parse tree walk to create external opcode list for methods.
 *              Here, we just want to catch the case where a definition block
 *              has been completed. Then we move all of the externals into
 *              a single block in the parse tree and thus the AML code.
 *
 ******************************************************************************/

ACPI_STATUS
ExAmlExternalWalkEnd (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{

    if (Op->Asl.ParseOpcode == PARSEOP_DEFINITION_BLOCK)
    {
        /*
         * Process any existing external list. (Support for
         * multiple definition blocks in a single file/compile)
         */
        ExMoveExternals (Op);
        AslGbl_ExternalsListHead = NULL;
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    ExMoveExternals
 *
 * PARAMETERS:  DefinitionBlockOp       - Op for current definition block
 *
 * RETURN:      None
 *
 * DESCRIPTION: Move all externals present in the source file into a single
 *              block of AML code, surrounded by an "If (0)" to prevent
 *              AML interpreters from attempting to execute the External
 *              opcodes.
 *
 ******************************************************************************/

static void
ExMoveExternals (
    ACPI_PARSE_OBJECT       *DefinitionBlockOp)
{
    ACPI_PARSE_OBJECT       *ParentOp;
    ACPI_PARSE_OBJECT       *ExternalOp;
    ACPI_PARSE_OBJECT       *PredicateOp;
    ACPI_PARSE_OBJECT       *NextOp;
    ACPI_PARSE_OBJECT       *Prev;
    ACPI_PARSE_OBJECT       *Next;
    char                    *ExternalName;
    ACPI_OBJECT_TYPE        ObjType;
    ACPI_STATUS             Status;
    UINT32                  i;


    if (!AslGbl_ExternalsListHead)
    {
        return;
    }

    /* Remove the External nodes from the tree */

    NextOp = AslGbl_ExternalsListHead;
    while (NextOp)
    {
        /*
         * The External is stored in child pointer of each node in the
         * list
         */
        ExternalOp = NextOp->Asl.Child;

        /* Get/set the fully qualified name */

        ExternalName = AcpiNsGetNormalizedPathname (ExternalOp->Asl.Node, TRUE);
        ExternalOp->Asl.ExternalName = ExternalName;
        ExternalOp->Asl.Namepath = ExternalName;

        /* Set line numbers (for listings, etc.) */

        ExternalOp->Asl.LineNumber = 0;
        ExternalOp->Asl.LogicalLineNumber = 0;

        Next = ExternalOp->Asl.Child;
        Next->Asl.LineNumber = 0;
        Next->Asl.LogicalLineNumber = 0;

        if (Next->Asl.ParseOpcode == PARSEOP_NAMESEG)
        {
            Next->Asl.ParseOpcode = PARSEOP_NAMESTRING;
        }

        Next->Asl.ExternalName = ExternalName;
        Status = UtInternalizeName (ExternalName, &Next->Asl.Value.String);
        if (ACPI_FAILURE (Status))
        {
            AslError (ASL_ERROR, ASL_MSG_COMPILER_INTERNAL,
                Next, "Could not internalize namestring");
            return;
        }

        Next->Asl.AmlLength = strlen (Next->Asl.Value.String);

        Next = Next->Asl.Next;
        Next->Asl.LineNumber = 0;
        Next->Asl.LogicalLineNumber = 0;

        Next = Next->Asl.Next;
        Next->Asl.LineNumber = 0;
        Next->Asl.LogicalLineNumber = 0;

        Next = Next->Asl.Next;
        Next->Asl.LineNumber = 0;
        Next->Asl.LogicalLineNumber = 0;

        ParentOp = ExternalOp->Asl.Parent;
        Prev = Next = ParentOp->Asl.Child;

        /* Now find the External node's position in parse tree */

        while (Next != ExternalOp)
        {
            Prev = Next;
            Next = Next->Asl.Next;
        }

        /* Remove the External from the parse tree */

        if (Prev == ExternalOp)
        {
            /* External was the first child node */

            ParentOp->Asl.Child = ExternalOp->Asl.Next;
        }

        Prev->Asl.Next = ExternalOp->Asl.Next;
        ExternalOp->Asl.Next = NULL;
        ExternalOp->Asl.Parent = AslGbl_ExternalsListHead;

        /* Point the External to the next in the list */

        if (NextOp->Asl.Next)
        {
            ExternalOp->Asl.Next = NextOp->Asl.Next->Asl.Child;
        }

        NextOp = NextOp->Asl.Next;
    }

    /*
     * Loop again to remove MethodObj Externals for which
     * a MethodCall was not found (dead external reference)
     */
    Prev = AslGbl_ExternalsListHead->Asl.Child;
    Next = Prev;
    while (Next)
    {
        ObjType = (ACPI_OBJECT_TYPE)
            Next->Asl.Child->Asl.Next->Asl.Value.Integer;

        if (ObjType == ACPI_TYPE_METHOD &&
            !(Next->Asl.CompileFlags & OP_VISITED))
        {
            if (Next == Prev)
            {
                AslGbl_ExternalsListHead->Asl.Child = Next->Asl.Next;
                Next->Asl.Next = NULL;
                Prev = AslGbl_ExternalsListHead->Asl.Child;
                Next = Prev;
                continue;
            }
            else
            {
                Prev->Asl.Next = Next->Asl.Next;
                Next->Asl.Next = NULL;
                Next = Prev->Asl.Next;
                continue;
            }
        }

        Prev = Next;
        Next = Next->Asl.Next;
    }

    /* If list is now empty, don't bother to make If (0) block */

    if (!AslGbl_ExternalsListHead->Asl.Child)
    {
        return;
    }

    /* Convert Gbl_ExternalsListHead parent to If(). */

    AslGbl_ExternalsListHead->Asl.ParseOpcode = PARSEOP_IF;
    AslGbl_ExternalsListHead->Asl.AmlOpcode = AML_IF_OP;
    AslGbl_ExternalsListHead->Asl.CompileFlags = OP_AML_PACKAGE;
    UtSetParseOpName (AslGbl_ExternalsListHead);

    /* Create a Zero op for the If predicate */

    PredicateOp = TrAllocateOp (PARSEOP_ZERO);
    PredicateOp->Asl.AmlOpcode = AML_ZERO_OP;

    PredicateOp->Asl.Parent = AslGbl_ExternalsListHead;
    PredicateOp->Asl.Child = NULL;
    PredicateOp->Asl.Next = AslGbl_ExternalsListHead->Asl.Child;
    AslGbl_ExternalsListHead->Asl.Child = PredicateOp;

    /* Set line numbers (for listings, etc.) */

    AslGbl_ExternalsListHead->Asl.LineNumber = 0;
    AslGbl_ExternalsListHead->Asl.LogicalLineNumber = 0;

    PredicateOp->Asl.LineNumber = 0;
    PredicateOp->Asl.LogicalLineNumber = 0;

    /* Insert block back in the list */

    Prev = DefinitionBlockOp->Asl.Child;
    Next = Prev;

    /* Find last default arg */

    for (i = 0; i < 6; i++)
    {
        Prev = Next;
        Next = Prev->Asl.Next;
    }

    if (Next)
    {
        /* Definition Block is not empty */

        AslGbl_ExternalsListHead->Asl.Next = Next;
    }
    else
    {
        /* Definition Block is empty. */

        AslGbl_ExternalsListHead->Asl.Next = NULL;
    }

    Prev->Asl.Next = AslGbl_ExternalsListHead;
    AslGbl_ExternalsListHead->Asl.Parent = Prev->Asl.Parent;
}
