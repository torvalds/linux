/*******************************************************************************
 *
 * Module Name: nssearch - Namespace search
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
#include <contrib/dev/acpica/include/acnamesp.h>

#ifdef ACPI_ASL_COMPILER
#include <contrib/dev/acpica/include/amlcode.h>
#endif

#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nssearch")

/* Local prototypes */

static ACPI_STATUS
AcpiNsSearchParentTree (
    UINT32                  TargetName,
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OBJECT_TYPE        Type,
    ACPI_NAMESPACE_NODE     **ReturnNode);


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsSearchOneScope
 *
 * PARAMETERS:  TargetName      - Ascii ACPI name to search for
 *              ParentNode      - Starting node where search will begin
 *              Type            - Object type to match
 *              ReturnNode      - Where the matched Named obj is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Search a single level of the namespace. Performs a
 *              simple search of the specified level, and does not add
 *              entries or search parents.
 *
 *
 *      Named object lists are built (and subsequently dumped) in the
 *      order in which the names are encountered during the namespace load;
 *
 *      All namespace searching is linear in this implementation, but
 *      could be easily modified to support any improved search
 *      algorithm. However, the linear search was chosen for simplicity
 *      and because the trees are small and the other interpreter
 *      execution overhead is relatively high.
 *
 *      Note: CPU execution analysis has shown that the AML interpreter spends
 *      a very small percentage of its time searching the namespace. Therefore,
 *      the linear search seems to be sufficient, as there would seem to be
 *      little value in improving the search.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsSearchOneScope (
    UINT32                  TargetName,
    ACPI_NAMESPACE_NODE     *ParentNode,
    ACPI_OBJECT_TYPE        Type,
    ACPI_NAMESPACE_NODE     **ReturnNode)
{
    ACPI_NAMESPACE_NODE     *Node;


    ACPI_FUNCTION_TRACE (NsSearchOneScope);


#ifdef ACPI_DEBUG_OUTPUT
    if (ACPI_LV_NAMES & AcpiDbgLevel)
    {
        char                *ScopeName;

        ScopeName = AcpiNsGetNormalizedPathname (ParentNode, TRUE);
        if (ScopeName)
        {
            ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
                "Searching %s (%p) For [%4.4s] (%s)\n",
                ScopeName, ParentNode, ACPI_CAST_PTR (char, &TargetName),
                AcpiUtGetTypeName (Type)));

            ACPI_FREE (ScopeName);
        }
    }
#endif

    /*
     * Search for name at this namespace level, which is to say that we
     * must search for the name among the children of this object
     */
    Node = ParentNode->Child;
    while (Node)
    {
        /* Check for match against the name */

        if (Node->Name.Integer == TargetName)
        {
            /* Resolve a control method alias if any */

            if (AcpiNsGetType (Node) == ACPI_TYPE_LOCAL_METHOD_ALIAS)
            {
                Node = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, Node->Object);
            }

            /* Found matching entry */

            ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
                "Name [%4.4s] (%s) %p found in scope [%4.4s] %p\n",
                ACPI_CAST_PTR (char, &TargetName),
                AcpiUtGetTypeName (Node->Type),
                Node, AcpiUtGetNodeName (ParentNode), ParentNode));

            *ReturnNode = Node;
            return_ACPI_STATUS (AE_OK);
        }

        /* Didn't match name, move on to the next peer object */

        Node = Node->Peer;
    }

    /* Searched entire namespace level, not found */

    ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
        "Name [%4.4s] (%s) not found in search in scope [%4.4s] "
        "%p first child %p\n",
        ACPI_CAST_PTR (char, &TargetName), AcpiUtGetTypeName (Type),
        AcpiUtGetNodeName (ParentNode), ParentNode, ParentNode->Child));

    return_ACPI_STATUS (AE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsSearchParentTree
 *
 * PARAMETERS:  TargetName      - Ascii ACPI name to search for
 *              Node            - Starting node where search will begin
 *              Type            - Object type to match
 *              ReturnNode      - Where the matched Node is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called when a name has not been found in the current namespace
 *              level. Before adding it or giving up, ACPI scope rules require
 *              searching enclosing scopes in cases identified by AcpiNsLocal().
 *
 *              "A name is located by finding the matching name in the current
 *              name space, and then in the parent name space. If the parent
 *              name space does not contain the name, the search continues
 *              recursively until either the name is found or the name space
 *              does not have a parent (the root of the name space). This
 *              indicates that the name is not found" (From ACPI Specification,
 *              section 5.3)
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiNsSearchParentTree (
    UINT32                  TargetName,
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OBJECT_TYPE        Type,
    ACPI_NAMESPACE_NODE     **ReturnNode)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *ParentNode;


    ACPI_FUNCTION_TRACE (NsSearchParentTree);


    ParentNode = Node->Parent;

    /*
     * If there is no parent (i.e., we are at the root) or type is "local",
     * we won't be searching the parent tree.
     */
    if (!ParentNode)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "[%4.4s] has no parent\n",
            ACPI_CAST_PTR (char, &TargetName)));
        return_ACPI_STATUS (AE_NOT_FOUND);
    }

    if (AcpiNsLocal (Type))
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
            "[%4.4s] type [%s] must be local to this scope (no parent search)\n",
            ACPI_CAST_PTR (char, &TargetName), AcpiUtGetTypeName (Type)));
        return_ACPI_STATUS (AE_NOT_FOUND);
    }

    /* Search the parent tree */

    ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
        "Searching parent [%4.4s] for [%4.4s]\n",
        AcpiUtGetNodeName (ParentNode), ACPI_CAST_PTR (char, &TargetName)));

    /* Search parents until target is found or we have backed up to the root */

    while (ParentNode)
    {
        /*
         * Search parent scope. Use TYPE_ANY because we don't care about the
         * object type at this point, we only care about the existence of
         * the actual name we are searching for. Typechecking comes later.
         */
        Status = AcpiNsSearchOneScope (
            TargetName, ParentNode, ACPI_TYPE_ANY, ReturnNode);
        if (ACPI_SUCCESS (Status))
        {
            return_ACPI_STATUS (Status);
        }

        /* Not found here, go up another level (until we reach the root) */

        ParentNode = ParentNode->Parent;
    }

    /* Not found in parent tree */

    return_ACPI_STATUS (AE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsSearchAndEnter
 *
 * PARAMETERS:  TargetName          - Ascii ACPI name to search for (4 chars)
 *              WalkState           - Current state of the walk
 *              Node                - Starting node where search will begin
 *              InterpreterMode     - Add names only in ACPI_MODE_LOAD_PASS_x.
 *                                    Otherwise,search only.
 *              Type                - Object type to match
 *              Flags               - Flags describing the search restrictions
 *              ReturnNode          - Where the Node is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Search for a name segment in a single namespace level,
 *              optionally adding it if it is not found. If the passed
 *              Type is not Any and the type previously stored in the
 *              entry was Any (i.e. unknown), update the stored type.
 *
 *              In ACPI_IMODE_EXECUTE, search only.
 *              In other modes, search and add if not found.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsSearchAndEnter (
    UINT32                  TargetName,
    ACPI_WALK_STATE         *WalkState,
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_INTERPRETER_MODE   InterpreterMode,
    ACPI_OBJECT_TYPE        Type,
    UINT32                  Flags,
    ACPI_NAMESPACE_NODE     **ReturnNode)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *NewNode;


    ACPI_FUNCTION_TRACE (NsSearchAndEnter);


    /* Parameter validation */

    if (!Node || !TargetName || !ReturnNode)
    {
        ACPI_ERROR ((AE_INFO,
            "Null parameter: Node %p Name 0x%X ReturnNode %p",
            Node, TargetName, ReturnNode));
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /*
     * Name must consist of valid ACPI characters. We will repair the name if
     * necessary because we don't want to abort because of this, but we want
     * all namespace names to be printable. A warning message is appropriate.
     *
     * This issue came up because there are in fact machines that exhibit
     * this problem, and we want to be able to enable ACPI support for them,
     * even though there are a few bad names.
     */
    AcpiUtRepairName (ACPI_CAST_PTR (char, &TargetName));

    /* Try to find the name in the namespace level specified by the caller */

    *ReturnNode = ACPI_ENTRY_NOT_FOUND;
    Status = AcpiNsSearchOneScope (TargetName, Node, Type, ReturnNode);
    if (Status != AE_NOT_FOUND)
    {
        /*
         * If we found it AND the request specifies that a find is an error,
         * return the error
         */
        if (Status == AE_OK)
        {
            /* The node was found in the namespace */

            /*
             * If the namespace override feature is enabled for this node,
             * delete any existing attached sub-object and make the node
             * look like a new node that is owned by the override table.
             */
            if (Flags & ACPI_NS_OVERRIDE_IF_FOUND)
            {
                ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
                    "Namespace override: %4.4s pass %u type %X Owner %X\n",
                    ACPI_CAST_PTR(char, &TargetName), InterpreterMode,
                    (*ReturnNode)->Type, WalkState->OwnerId));

                AcpiNsDeleteChildren (*ReturnNode);
                if (AcpiGbl_RuntimeNamespaceOverride)
                {
                    AcpiUtRemoveReference ((*ReturnNode)->Object);
                    (*ReturnNode)->Object = NULL;
                    (*ReturnNode)->OwnerId = WalkState->OwnerId;
                }
                else
                {
                    AcpiNsRemoveNode (*ReturnNode);
                    *ReturnNode = ACPI_ENTRY_NOT_FOUND;
                }
            }

            /* Return an error if we don't expect to find the object */

            else if (Flags & ACPI_NS_ERROR_IF_FOUND)
            {
                Status = AE_ALREADY_EXISTS;
            }
        }

#ifdef ACPI_ASL_COMPILER
        if (*ReturnNode && (*ReturnNode)->Type == ACPI_TYPE_ANY)
        {
            (*ReturnNode)->Flags |= ANOBJ_IS_EXTERNAL;
        }
#endif

        /* Either found it or there was an error: finished either way */

        return_ACPI_STATUS (Status);
    }

    /*
     * The name was not found. If we are NOT performing the first pass
     * (name entry) of loading the namespace, search the parent tree (all the
     * way to the root if necessary.) We don't want to perform the parent
     * search when the namespace is actually being loaded. We want to perform
     * the search when namespace references are being resolved (load pass 2)
     * and during the execution phase.
     */
    if ((InterpreterMode != ACPI_IMODE_LOAD_PASS1) &&
        (Flags & ACPI_NS_SEARCH_PARENT))
    {
        /*
         * Not found at this level - search parent tree according to the
         * ACPI specification
         */
        Status = AcpiNsSearchParentTree (TargetName, Node, Type, ReturnNode);
        if (ACPI_SUCCESS (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }

    /* In execute mode, just search, never add names. Exit now */

    if (InterpreterMode == ACPI_IMODE_EXECUTE)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
            "%4.4s Not found in %p [Not adding]\n",
            ACPI_CAST_PTR (char, &TargetName), Node));

        return_ACPI_STATUS (AE_NOT_FOUND);
    }

    /* Create the new named object */

    NewNode = AcpiNsCreateNode (TargetName);
    if (!NewNode)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

#ifdef ACPI_ASL_COMPILER

    /* Node is an object defined by an External() statement */

    if (Flags & ACPI_NS_EXTERNAL ||
        (WalkState && WalkState->Opcode == AML_SCOPE_OP))
    {
        NewNode->Flags |= ANOBJ_IS_EXTERNAL;
    }
#endif

    if (Flags & ACPI_NS_TEMPORARY)
    {
        NewNode->Flags |= ANOBJ_TEMPORARY;
    }

    /* Install the new object into the parent's list of children */

    AcpiNsInstallNode (WalkState, Node, NewNode, Type);
    *ReturnNode = NewNode;
    return_ACPI_STATUS (AE_OK);
}
