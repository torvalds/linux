/*******************************************************************************
 *
 * Module Name: dmnames - AML disassembler, names, namestrings, pathnames
 *
 ******************************************************************************/

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
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acdisasm.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dmnames")

/* Local prototypes */

#ifdef ACPI_OBSOLETE_FUNCTIONS
void
AcpiDmDisplayPath (
    ACPI_PARSE_OBJECT       *Op);
#endif


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpName
 *
 * PARAMETERS:  Name            - 4 character ACPI name
 *
 * RETURN:      Final length of name
 *
 * DESCRIPTION: Dump an ACPI name, minus any trailing underscores.
 *
 ******************************************************************************/

UINT32
AcpiDmDumpName (
    UINT32                  Name)
{
    UINT32                  i;
    UINT32                  Length;
    char                    NewName[4];


    /* Copy name locally in case the original name is not writeable */

    *ACPI_CAST_PTR (UINT32, &NewName[0]) = Name;

    /* Ensure that the name is printable, even if we have to fix it */

    AcpiUtRepairName (NewName);

    /* Remove all trailing underscores from the name */

    Length = ACPI_NAME_SIZE;
    for (i = (ACPI_NAME_SIZE - 1); i != 0; i--)
    {
        if (NewName[i] == '_')
        {
            Length--;
        }
        else
        {
            break;
        }
    }

    /* Dump the name, up to the start of the trailing underscores */

    for (i = 0; i < Length; i++)
    {
        AcpiOsPrintf ("%c", NewName[i]);
    }

    return (Length);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsDisplayObjectPathname
 *
 * PARAMETERS:  WalkState       - Current walk state
 *              Op              - Object whose pathname is to be obtained
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display the pathname associated with a named object. Two
 *              versions. One searches the parse tree (for parser-only
 *              applications suchas AcpiDump), and the other searches the
 *              ACPI namespace (the parse tree is probably deleted)
 *
 ******************************************************************************/

ACPI_STATUS
AcpiPsDisplayObjectPathname (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_BUFFER             Buffer;
    UINT32                  DebugLevel;


    /* Save current debug level so we don't get extraneous debug output */

    DebugLevel = AcpiDbgLevel;
    AcpiDbgLevel = 0;

    /* Just get the Node out of the Op object */

    Node = Op->Common.Node;
    if (!Node)
    {
        /* Node not defined in this scope, look it up */

        Status = AcpiNsLookup (WalkState->ScopeInfo, Op->Common.Value.String,
            ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE, ACPI_NS_SEARCH_PARENT,
            WalkState, &(Node));

        if (ACPI_FAILURE (Status))
        {
            /*
             * We can't get the pathname since the object is not in the
             * namespace. This can happen during single stepping
             * where a dynamic named object is *about* to be created.
             */
            AcpiOsPrintf ("  [Path not found]");
            goto Exit;
        }

        /* Save it for next time. */

        Op->Common.Node = Node;
    }

    /* Convert NamedDesc/handle to a full pathname */

    Buffer.Length = ACPI_ALLOCATE_LOCAL_BUFFER;
    Status = AcpiNsHandleToPathname (Node, &Buffer, FALSE);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("****Could not get pathname****)");
        goto Exit;
    }

    AcpiOsPrintf ("  (Path %s)", (char *) Buffer.Pointer);
    ACPI_FREE (Buffer.Pointer);


Exit:
    /* Restore the debug level */

    AcpiDbgLevel = DebugLevel;
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmNamestring
 *
 * PARAMETERS:  Name                - ACPI Name string to store
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode and dump an ACPI namestring. Handles prefix characters
 *
 ******************************************************************************/

void
AcpiDmNamestring (
    char                    *Name)
{
    UINT32                  SegCount;


    if (!Name)
    {
        return;
    }

    /* Handle all Scope Prefix operators */

    while (ACPI_IS_ROOT_PREFIX (ACPI_GET8 (Name)) ||
           ACPI_IS_PARENT_PREFIX (ACPI_GET8 (Name)))
    {
        /* Append prefix character */

        AcpiOsPrintf ("%1c", ACPI_GET8 (Name));
        Name++;
    }

    switch (ACPI_GET8 (Name))
    {
    case 0:

        SegCount = 0;
        break;

    case AML_DUAL_NAME_PREFIX:

        SegCount = 2;
        Name++;
        break;

    case AML_MULTI_NAME_PREFIX:

        SegCount = (UINT32) ACPI_GET8 (Name + 1);
        Name += 2;
        break;

    default:

        SegCount = 1;
        break;
    }

    while (SegCount)
    {
        /* Append Name segment */

        AcpiDmDumpName (*ACPI_CAST_PTR (UINT32, Name));

        SegCount--;
        if (SegCount)
        {
            /* Not last name, append dot separator */

            AcpiOsPrintf (".");
        }

        Name += ACPI_NAME_SIZE;
    }
}


#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDisplayPath
 *
 * PARAMETERS:  Op                  - Named Op whose path is to be constructed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Walk backwards from current scope and display the name
 *              of each previous level of scope up to the root scope
 *              (like "pwd" does with file systems)
 *
 ******************************************************************************/

void
AcpiDmDisplayPath (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Prev;
    ACPI_PARSE_OBJECT       *Search;
    UINT32                  Name;
    BOOLEAN                 DoDot = FALSE;
    ACPI_PARSE_OBJECT       *NamePath;
    const ACPI_OPCODE_INFO  *OpInfo;


    /* We are only interested in named objects */

    OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);
    if (!(OpInfo->Flags & AML_NSNODE))
    {
        return;
    }

    if (OpInfo->Flags & AML_CREATE)
    {
        /* Field creation - check for a fully qualified namepath */

        if (Op->Common.AmlOpcode == AML_CREATE_FIELD_OP)
        {
            NamePath = AcpiPsGetArg (Op, 3);
        }
        else
        {
            NamePath = AcpiPsGetArg (Op, 2);
        }

        if ((NamePath) &&
            (NamePath->Common.Value.String) &&
            (ACPI_IS_ROOT_PREFIX (NamePath->Common.Value.String[0])))
        {
            AcpiDmNamestring (NamePath->Common.Value.String);
            return;
        }
    }

    Prev = NULL;            /* Start with Root Node */
    while (Prev != Op)
    {
        /* Search upwards in the tree to find scope with "prev" as its parent */

        Search = Op;
        for (; ;)
        {
            if (Search->Common.Parent == Prev)
            {
                break;
            }

            /* Go up one level */

            Search = Search->Common.Parent;
        }

        if (Prev)
        {
            OpInfo = AcpiPsGetOpcodeInfo (Search->Common.AmlOpcode);
            if (!(OpInfo->Flags & AML_FIELD))
            {
                /* Below root scope, append scope name */

                if (DoDot)
                {
                    /* Append dot */

                    AcpiOsPrintf (".");
                }

                if (OpInfo->Flags & AML_CREATE)
                {
                    if (Op->Common.AmlOpcode == AML_CREATE_FIELD_OP)
                    {
                        NamePath = AcpiPsGetArg (Op, 3);
                    }
                    else
                    {
                        NamePath = AcpiPsGetArg (Op, 2);
                    }

                    if ((NamePath) &&
                        (NamePath->Common.Value.String))
                    {
                        AcpiDmDumpName (NamePath->Common.Value.String);
                    }
                }
                else
                {
                    Name = AcpiPsGetName (Search);
                    AcpiDmDumpName ((char *) &Name);
                }

                DoDot = TRUE;
            }
        }

        Prev = Search;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmValidateName
 *
 * PARAMETERS:  Name            - 4 character ACPI name
 *
 * RETURN:      None
 *
 * DESCRIPTION: Lookup the name
 *
 ******************************************************************************/

void
AcpiDmValidateName (
    char                    *Name,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *TargetOp;


    if ((!Name) ||
        (!Op->Common.Parent))
    {
        return;
    }

    if (!Op->Common.Node)
    {
        AcpiOsPrintf (
            " /**** Name not found or not accessible from this scope ****/ ");
    }

    if ((!Name) ||
        (!Op->Common.Parent))
    {
        return;
    }

    TargetOp = AcpiPsFind (Op, Name, 0, 0);
    if (!TargetOp)
    {
        /*
         * Didn't find the name in the parse tree. This may be
         * a problem, or it may simply be one of the predefined names
         * (such as _OS_). Rather than worry about looking up all
         * the predefined names, just display the name as given
         */
        AcpiOsPrintf (
            " /**** Name not found or not accessible from this scope ****/ ");
    }
}
#endif
