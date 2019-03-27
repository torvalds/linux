/******************************************************************************
 *
 * Module Name: aslopt- Compiler optimizations
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
        ACPI_MODULE_NAME    ("aslopt")


static UINT32               OptTotal = 0;

/* Local prototypes */

static ACPI_STATUS
OptSearchToRoot (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState,
    ACPI_NAMESPACE_NODE     *CurrentNode,
    ACPI_NAMESPACE_NODE     *TargetNode,
    ACPI_BUFFER             *TargetPath,
    char                    **NewPath);

static ACPI_STATUS
OptBuildShortestPath (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState,
    ACPI_NAMESPACE_NODE     *CurrentNode,
    ACPI_NAMESPACE_NODE     *TargetNode,
    ACPI_BUFFER             *CurrentPath,
    ACPI_BUFFER             *TargetPath,
    ACPI_SIZE               AmlNameStringLength,
    UINT8                   IsDeclaration,
    char                    **ReturnNewPath);

static ACPI_STATUS
OptOptimizeNameDeclaration (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState,
    ACPI_NAMESPACE_NODE     *CurrentNode,
    ACPI_NAMESPACE_NODE     *TargetNode,
    char                    *AmlNameString,
    char                    **NewPath);


/*******************************************************************************
 *
 * FUNCTION:    OptSearchToRoot
 *
 * PARAMETERS:  Op                  - Current parser op
 *              WalkState           - Current state
 *              CurrentNode         - Where we are in the namespace
 *              TargetNode          - Node to which we are referring
 *              TargetPath          - External full path to the target node
 *              NewPath             - Where the optimized path is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Attempt to optimize a reference to a single 4-character ACPI
 *              name utilizing the search-to-root name resolution algorithm
 *              that is used by AML interpreters.
 *
 ******************************************************************************/

static ACPI_STATUS
OptSearchToRoot (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState,
    ACPI_NAMESPACE_NODE     *CurrentNode,
    ACPI_NAMESPACE_NODE     *TargetNode,
    ACPI_BUFFER             *TargetPath,
    char                    **NewPath)
{
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_GENERIC_STATE      ScopeInfo;
    ACPI_STATUS             Status;
    char                    *Path;


    ACPI_FUNCTION_NAME (OptSearchToRoot);


    /*
     * Check if search-to-root can be utilized. Use the last NameSeg of
     * the NamePath and 1) See if can be found and 2) If found, make
     * sure that it is the same node that we want. If there is another
     * name in the search path before the one we want, the nodes will
     * not match, and we cannot use this optimization.
     */
    Path = &(((char *) TargetPath->Pointer)[
        TargetPath->Length - ACPI_NAME_SIZE]);
    ScopeInfo.Scope.Node = CurrentNode;

    /* Lookup the NameSeg using SEARCH_PARENT (search-to-root) */

    Status = AcpiNsLookup (&ScopeInfo, Path, ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
        ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE,
        WalkState, &(Node));
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /*
     * We found the name, but we must check to make sure that the node
     * matches. Otherwise, there is another identical name in the search
     * path that precludes the use of this optimization.
     */
    if (Node != TargetNode)
    {
        /*
         * This means that another object with the same name was found first,
         * and we cannot use this optimization.
         */
        return (AE_NOT_FOUND);
    }

    /* Found the node, we can use this optimization */

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
        "NAMESEG:   %-24s", Path));

    /* We must allocate a new string for the name (TargetPath gets deleted) */

    *NewPath = UtLocalCacheCalloc (ACPI_NAME_SIZE + 1);
    strcpy (*NewPath, Path);

    if (strncmp (*NewPath, "_T_", 3))
    {
        AslError (ASL_OPTIMIZATION, ASL_MSG_SINGLE_NAME_OPTIMIZATION,
            Op, *NewPath);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    OptBuildShortestPath
 *
 * PARAMETERS:  Op                  - Current parser op
 *              WalkState           - Current state
 *              CurrentNode         - Where we are in the namespace
 *              TargetNode          - Node to which we are referring
 *              CurrentPath         - External full path to the current node
 *              TargetPath          - External full path to the target node
 *              AmlNameStringLength - Length of the original namepath
 *              IsDeclaration       - TRUE for declaration, FALSE for reference
 *              ReturnNewPath       - Where the optimized path is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Build an optimal NamePath using carats
 *
 ******************************************************************************/

static ACPI_STATUS
OptBuildShortestPath (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState,
    ACPI_NAMESPACE_NODE     *CurrentNode,
    ACPI_NAMESPACE_NODE     *TargetNode,
    ACPI_BUFFER             *CurrentPath,
    ACPI_BUFFER             *TargetPath,
    ACPI_SIZE               AmlNameStringLength,
    UINT8                   IsDeclaration,
    char                    **ReturnNewPath)
{
    UINT32                  NumCommonSegments;
    UINT32                  MaxCommonSegments;
    UINT32                  Index;
    UINT32                  NumCarats;
    UINT32                  i;
    char                    *NewPathInternal;
    char                    *NewPathExternal;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_GENERIC_STATE      ScopeInfo;
    ACPI_STATUS             Status;
    BOOLEAN                 SubPath = FALSE;


    ACPI_FUNCTION_NAME (OptBuildShortestPath);


    ScopeInfo.Scope.Node = CurrentNode;

    /*
     * Determine the maximum number of NameSegs that the Target and Current paths
     * can possibly have in common. (To optimize, we have to have at least 1)
     *
     * Note: The external NamePath string lengths are always a multiple of 5
     * (ACPI_NAME_SIZE + separator)
     */
    MaxCommonSegments = TargetPath->Length / ACPI_PATH_SEGMENT_LENGTH;
    if (CurrentPath->Length < TargetPath->Length)
    {
        MaxCommonSegments = CurrentPath->Length / ACPI_PATH_SEGMENT_LENGTH;
    }

    /*
     * Determine how many NameSegs the two paths have in common.
     * (Starting from the root)
     */
    for (NumCommonSegments = 0;
         NumCommonSegments < MaxCommonSegments;
         NumCommonSegments++)
    {
        /* Compare two single NameSegs */

        Index = (NumCommonSegments * ACPI_PATH_SEGMENT_LENGTH) + 1;

        if (!ACPI_COMPARE_NAME (
            &(ACPI_CAST_PTR (char, TargetPath->Pointer)) [Index],
            &(ACPI_CAST_PTR (char, CurrentPath->Pointer)) [Index]))
        {
            /* Mismatch */

            break;
        }
    }

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, " COMMON: %u",
        NumCommonSegments));

    /* There must be at least 1 common NameSeg in order to optimize */

    if (NumCommonSegments == 0)
    {
        return (AE_NOT_FOUND);
    }

    if (NumCommonSegments == MaxCommonSegments)
    {
        if (CurrentPath->Length == TargetPath->Length)
        {
            ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, " SAME PATH"));
            return (AE_NOT_FOUND);
        }
        else
        {
            ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, " SUBPATH"));
            SubPath = TRUE;
        }
    }

    /* Determine how many prefix Carats are required */

    NumCarats = (CurrentPath->Length / ACPI_PATH_SEGMENT_LENGTH) -
        NumCommonSegments;

    /*
     * Construct a new target string
     */
    NewPathExternal =
        UtLocalCacheCalloc (TargetPath->Length + NumCarats + 1);

    /* Insert the Carats into the Target string */

    for (i = 0; i < NumCarats; i++)
    {
        NewPathExternal[i] = AML_PARENT_PREFIX;
    }

    /*
     * Copy only the necessary (optimal) segments from the original
     * target string
     */
    Index = (NumCommonSegments * ACPI_PATH_SEGMENT_LENGTH) + 1;

    /* Special handling for exact subpath in a name declaration */

    if (IsDeclaration && SubPath &&
        (CurrentPath->Length > TargetPath->Length))
    {
        /*
         * The current path is longer than the target, and the target is a
         * subpath of the current path. We must include one more NameSeg of
         * the target path
         */
        Index -= ACPI_PATH_SEGMENT_LENGTH;

        /* Special handling for Scope() operator */

        if (Op->Asl.AmlOpcode == AML_SCOPE_OP)
        {
            NewPathExternal[i] = AML_PARENT_PREFIX;
            i++;
            ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, "(EXTRA ^)"));
        }
    }

    /* Make sure we haven't gone off the end of the target path */

    if (Index > TargetPath->Length)
    {
        Index = TargetPath->Length;
    }

    strcpy (&NewPathExternal[i],
        &(ACPI_CAST_PTR (char, TargetPath->Pointer))[Index]);
    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, " %-24s", NewPathExternal));

    /*
     * Internalize the new target string and check it against the original
     * string to make sure that this is in fact an optimization. If the
     * original string is already optimal, there is no point in continuing.
     */
    Status = AcpiNsInternalizeName (NewPathExternal, &NewPathInternal);
    if (ACPI_FAILURE (Status))
    {
        AslCoreSubsystemError (Op, Status, "Internalizing new NamePath",
            ASL_NO_ABORT);
        goto Cleanup;
    }

    if (strlen (NewPathInternal) >= AmlNameStringLength)
    {
        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
            " NOT SHORTER (New %u old %u)",
            (UINT32) strlen (NewPathInternal),
            (UINT32) AmlNameStringLength));

        ACPI_FREE (NewPathInternal);
        Status = AE_NOT_FOUND;
        goto Cleanup;
    }

    /*
     * Check to make sure that the optimization finds the node we are
     * looking for. This is simply a sanity check on the new
     * path that has been created.
     */
    Status = AcpiNsLookup (&ScopeInfo, NewPathInternal,
        ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
        ACPI_NS_DONT_OPEN_SCOPE, WalkState, &(Node));
    if (ACPI_SUCCESS (Status))
    {
        /* Found the namepath, but make sure the node is correct */

        if (Node == TargetNode)
        {
            /* The lookup matched the node, accept this optimization */

            AslError (ASL_OPTIMIZATION, ASL_MSG_NAME_OPTIMIZATION,
                Op, NewPathExternal);
            *ReturnNewPath = NewPathInternal;
        }
        else
        {
            /* Node is not correct, do not use this optimization */

            Status = AE_NOT_FOUND;
            ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, " ***** WRONG NODE"));
            AslError (ASL_WARNING, ASL_MSG_COMPILER_INTERNAL, Op,
                "Not using optimized name - found wrong node");
        }
    }
    else
    {
        /* The lookup failed, we obviously cannot use this optimization */

        ACPI_FREE (NewPathInternal);

        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, " ***** NOT FOUND"));
        AslError (ASL_WARNING, ASL_MSG_COMPILER_INTERNAL, Op,
            "Not using optimized name - did not find node");
    }

Cleanup:

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    OptOptimizeNameDeclaration
 *
 * PARAMETERS:  Op                  - Current parser op
 *              WalkState           - Current state
 *              CurrentNode         - Where we are in the namespace
 *              AmlNameString       - Unoptimized namepath
 *              NewPath             - Where the optimized path is returned
 *
 * RETURN:      Status. AE_OK If path is optimized
 *
 * DESCRIPTION: Perform a simple optimization of removing an extraneous
 *              backslash prefix if we are already at the root scope.
 *
 ******************************************************************************/

static ACPI_STATUS
OptOptimizeNameDeclaration (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState,
    ACPI_NAMESPACE_NODE     *CurrentNode,
    ACPI_NAMESPACE_NODE     *TargetNode,
    char                    *AmlNameString,
    char                    **NewPath)
{
    ACPI_STATUS             Status;
    char                    *NewPathExternal;
    ACPI_NAMESPACE_NODE     *Node;


    ACPI_FUNCTION_TRACE (OptOptimizeNameDeclaration);


    if (((CurrentNode == AcpiGbl_RootNode) ||
        (Op->Common.Parent->Asl.ParseOpcode == PARSEOP_DEFINITION_BLOCK)) &&
            (ACPI_IS_ROOT_PREFIX (AmlNameString[0])))
    {
        /*
         * The current scope is the root, and the namepath has a root prefix
         * that is therefore extraneous. Remove it.
         */
        *NewPath = &AmlNameString[1];

        /* Debug output */

        Status = AcpiNsExternalizeName (ACPI_UINT32_MAX, *NewPath,
            NULL, &NewPathExternal);
        if (ACPI_FAILURE (Status))
        {
            AslCoreSubsystemError (Op, Status, "Externalizing NamePath",
                ASL_NO_ABORT);
            return (Status);
        }

        /*
         * Check to make sure that the optimization finds the node we are
         * looking for. This is simply a sanity check on the new
         * path that has been created.
         *
         * We know that we are at the root, so NULL is used for the scope.
         */
        Status = AcpiNsLookup (NULL, *NewPath,
            ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
            ACPI_NS_DONT_OPEN_SCOPE, WalkState, &(Node));
        if (ACPI_SUCCESS (Status))
        {
            /* Found the namepath, but make sure the node is correct */

            if (Node == TargetNode)
            {
                /* The lookup matched the node, accept this optimization */

                AslError (ASL_OPTIMIZATION, ASL_MSG_NAME_OPTIMIZATION,
                    Op, NewPathExternal);

                ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
                    "AT ROOT:   %-24s", NewPathExternal));
            }
            else
            {
                /* Node is not correct, do not use this optimization */

                Status = AE_NOT_FOUND;
                ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
                    " ***** WRONG NODE"));
                AslError (ASL_WARNING, ASL_MSG_COMPILER_INTERNAL, Op,
                    "Not using optimized name - found wrong node");
            }
        }
        else
        {
            /* The lookup failed, we obviously cannot use this optimization */

            ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
                " ***** NOT FOUND"));
            AslError (ASL_WARNING, ASL_MSG_COMPILER_INTERNAL, Op,
                "Not using optimized name - did not find node");
        }

        ACPI_FREE (NewPathExternal);
        return (Status);
    }

    /* Could not optimize */

    return (AE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    OptOptimizeNamePath
 *
 * PARAMETERS:  Op                  - Current parser op
 *              Flags               - Opcode info flags
 *              WalkState           - Current state
 *              AmlNameString       - Unoptimized namepath
 *              TargetNode          - Node to which AmlNameString refers
 *
 * RETURN:      None. If path is optimized, the Op is updated with new path
 *
 * DESCRIPTION: Optimize a Named Declaration or Reference to the minimal length.
 *              Must take into account both the current location in the
 *              namespace and the actual reference path.
 *
 ******************************************************************************/

void
OptOptimizeNamePath (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Flags,
    ACPI_WALK_STATE         *WalkState,
    char                    *AmlNameString,
    ACPI_NAMESPACE_NODE     *TargetNode)
{
    ACPI_STATUS             Status;
    ACPI_BUFFER             TargetPath;
    ACPI_BUFFER             CurrentPath;
    ACPI_SIZE               AmlNameStringLength;
    ACPI_NAMESPACE_NODE     *CurrentNode;
    char                    *ExternalNameString;
    char                    *NewPath = NULL;
    ACPI_SIZE               HowMuchShorter;
    ACPI_PARSE_OBJECT       *NextOp;


    ACPI_FUNCTION_TRACE (OptOptimizeNamePath);


    /* This is an optional optimization */

    if (!AslGbl_ReferenceOptimizationFlag)
    {
        return_VOID;
    }

    /* Various required items */

    if (!TargetNode || !WalkState || !AmlNameString || !Op->Common.Parent)
    {
        return_VOID;
    }

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
        "PATH OPTIMIZE: Line %5d ParentOp [%12.12s] ThisOp [%12.12s] ",
        Op->Asl.LogicalLineNumber,
        AcpiPsGetOpcodeName (Op->Common.Parent->Common.AmlOpcode),
        AcpiPsGetOpcodeName (Op->Common.AmlOpcode)));

    if (!(Flags & (AML_NAMED | AML_CREATE)))
    {
        if (Op->Asl.CompileFlags & OP_IS_NAME_DECLARATION)
        {
            /* We don't want to fuss with actual name declaration nodes here */

            ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
                "******* NAME DECLARATION\n"));
            return_VOID;
        }
    }

    /*
     * The original path must be longer than one NameSeg (4 chars) for there
     * to be any possibility that it can be optimized to a shorter string
     */
    AmlNameStringLength = strlen (AmlNameString);
    if (AmlNameStringLength <= ACPI_NAME_SIZE)
    {
        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
            "NAMESEG %4.4s\n", AmlNameString));
        return_VOID;
    }

    /*
     * We need to obtain the node that represents the current scope -- where
     * we are right now in the namespace. We will compare this path
     * against the Namepath, looking for commonality.
     */
    CurrentNode = AcpiGbl_RootNode;
    if (WalkState->ScopeInfo)
    {
        CurrentNode = WalkState->ScopeInfo->Scope.Node;
    }

    if (Flags & (AML_NAMED | AML_CREATE))
    {
        /* This is the declaration of a new name */

        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, "NAME\n"));

        /*
         * The node of interest is the parent of this node (the containing
         * scope). The actual namespace node may be up more than one level
         * of parse op or it may not exist at all (if we traverse back
         * up to the root.)
         */
        NextOp = Op->Asl.Parent;
        while (NextOp && (!NextOp->Asl.Node))
        {
            NextOp = NextOp->Asl.Parent;
        }

        if (NextOp && NextOp->Asl.Node)
        {
            CurrentNode = NextOp->Asl.Node;
        }
        else
        {
            CurrentNode = AcpiGbl_RootNode;
        }
    }
    else
    {
        /* This is a reference to an existing named object */

        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, "REFERENCE\n"));
    }

    /*
     * Obtain the full paths to the two nodes that we are interested in
     * (Target and current namespace location) in external
     * format -- something we can easily manipulate
     */
    TargetPath.Length = ACPI_ALLOCATE_LOCAL_BUFFER;
    Status = AcpiNsHandleToPathname (TargetNode, &TargetPath, FALSE);
    if (ACPI_FAILURE (Status))
    {
        AslCoreSubsystemError (Op, Status, "Getting Target NamePath",
            ASL_NO_ABORT);
        return_VOID;
    }

    TargetPath.Length--;    /* Subtract one for null terminator */

    /* CurrentPath is the path to this scope (where we are in the namespace) */

    CurrentPath.Length = ACPI_ALLOCATE_LOCAL_BUFFER;
    Status = AcpiNsHandleToPathname (CurrentNode, &CurrentPath, FALSE);
    if (ACPI_FAILURE (Status))
    {
        AslCoreSubsystemError (Op, Status, "Getting Current NamePath",
            ASL_NO_ABORT);
        return_VOID;
    }

    CurrentPath.Length--;   /* Subtract one for null terminator */

    /* Debug output only */

    Status = AcpiNsExternalizeName (ACPI_UINT32_MAX, AmlNameString,
        NULL, &ExternalNameString);
    if (ACPI_FAILURE (Status))
    {
        AslCoreSubsystemError (Op, Status, "Externalizing NamePath",
            ASL_NO_ABORT);
        return_VOID;
    }

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
        "CURRENT SCOPE: (%2u) %-37s FULL PATH TO NAME: (%2u) %-32s ACTUAL AML:%-32s\n",
        (UINT32) CurrentPath.Length, (char *) CurrentPath.Pointer,
        (UINT32) TargetPath.Length, (char *) TargetPath.Pointer,
        ExternalNameString));

    ACPI_FREE (ExternalNameString);

    /*
     * Attempt an optimization depending on the type of namepath
     */
    if (Flags & (AML_NAMED | AML_CREATE))
    {
        /*
         * This is a named opcode and the namepath is a name declaration, not
         * a reference.
         */
        Status = OptOptimizeNameDeclaration (Op, WalkState, CurrentNode,
            TargetNode, AmlNameString, &NewPath);
        if (ACPI_FAILURE (Status))
        {
            /*
             * 2) now attempt to
             *    optimize the namestring with carats (up-arrow)
             */
            Status = OptBuildShortestPath (Op, WalkState, CurrentNode,
                TargetNode, &CurrentPath, &TargetPath,
                AmlNameStringLength, 1, &NewPath);
        }
    }
    else
    {
        /*
         * This is a reference to an existing named object
         *
         * 1) Check if search-to-root can be utilized using the last
         *    NameSeg of the NamePath
         */
        Status = OptSearchToRoot (Op, WalkState, CurrentNode,
            TargetNode, &TargetPath, &NewPath);
        if (ACPI_FAILURE (Status))
        {
            /*
             * 2) Search-to-root could not be used, now attempt to
             *    optimize the namestring with carats (up-arrow)
             */
            Status = OptBuildShortestPath (Op, WalkState, CurrentNode,
                TargetNode, &CurrentPath, &TargetPath,
                AmlNameStringLength, 0, &NewPath);
        }
    }

    /*
     * Success from above indicates that the NamePath was successfully
     * optimized. We need to update the parse op with the new name
     */
    if (ACPI_SUCCESS (Status))
    {
        HowMuchShorter = (AmlNameStringLength - strlen (NewPath));
        OptTotal += HowMuchShorter;

        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS,
            " REDUCED BY %2u (TOTAL SAVED %2u)",
            (UINT32) HowMuchShorter, OptTotal));

        if (Flags & AML_NAMED)
        {
            if (Op->Asl.AmlOpcode == AML_ALIAS_OP)
            {
                /*
                 * ALIAS is the only oddball opcode, the name declaration
                 * (alias name) is the second operand
                 */
                Op->Asl.Child->Asl.Next->Asl.Value.String = NewPath;
                Op->Asl.Child->Asl.Next->Asl.AmlLength = strlen (NewPath);
            }
            else
            {
                Op->Asl.Child->Asl.Value.String = NewPath;
                Op->Asl.Child->Asl.AmlLength = strlen (NewPath);
            }
        }
        else if (Flags & AML_CREATE)
        {
            /* Name must appear as the last parameter */

            NextOp = Op->Asl.Child;
            while (!(NextOp->Asl.CompileFlags & OP_IS_NAME_DECLARATION))
            {
                NextOp = NextOp->Asl.Next;
            }
            /* Update the parse node with the new NamePath */

            NextOp->Asl.Value.String = NewPath;
            NextOp->Asl.AmlLength = strlen (NewPath);
        }
        else
        {
            /* Update the parse node with the new NamePath */

            Op->Asl.Value.String = NewPath;
            Op->Asl.AmlLength = strlen (NewPath);
        }
    }
    else
    {
        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, " ALREADY OPTIMAL"));
    }

    /* Cleanup path buffers */

    ACPI_FREE (TargetPath.Pointer);
    ACPI_FREE (CurrentPath.Pointer);

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OPTIMIZATIONS, "\n"));
    return_VOID;
}
