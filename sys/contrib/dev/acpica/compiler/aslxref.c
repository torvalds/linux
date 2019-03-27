/******************************************************************************
 *
 * Module Name: aslxref - Namespace cross-reference
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
#include <contrib/dev/acpica/include/acdispat.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslxref")

/* Local prototypes */

static ACPI_STATUS
XfNamespaceLocateBegin (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);

static ACPI_STATUS
XfNamespaceLocateEnd (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);

static BOOLEAN
XfValidateCrossReference (
    ACPI_PARSE_OBJECT       *Op,
    const ACPI_OPCODE_INFO  *OpInfo,
    ACPI_NAMESPACE_NODE     *Node);

static ACPI_PARSE_OBJECT *
XfGetParentMethod (
    ACPI_PARSE_OBJECT       *Op);

static BOOLEAN
XfObjectExists (
    char                    *Name);

static ACPI_STATUS
XfCompareOneNamespaceObject (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue);

static void
XfCheckFieldRange (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  RegionBitLength,
    UINT32                  FieldBitOffset,
    UINT32                  FieldBitLength,
    UINT32                  AccessBitWidth);


/*******************************************************************************
 *
 * FUNCTION:    XfCrossReferenceNamespace
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform a cross reference check of the parse tree against the
 *              namespace. Every named referenced within the parse tree
 *              should be get resolved with a namespace lookup. If not, the
 *              original reference in the ASL code is invalid -- i.e., refers
 *              to a non-existent object.
 *
 * NOTE:  The ASL "External" operator causes the name to be inserted into the
 *        namespace so that references to the external name will be resolved
 *        correctly here.
 *
 ******************************************************************************/

ACPI_STATUS
XfCrossReferenceNamespace (
    void)
{
    ACPI_WALK_STATE         *WalkState;


    /*
     * Create a new walk state for use when looking up names
     * within the namespace (Passed as context to the callbacks)
     */
    WalkState = AcpiDsCreateWalkState (0, NULL, NULL, NULL);
    if (!WalkState)
    {
        return (AE_NO_MEMORY);
    }

    /* Walk the entire parse tree */

    TrWalkParseTree (AslGbl_ParseTreeRoot, ASL_WALK_VISIT_TWICE,
        XfNamespaceLocateBegin, XfNamespaceLocateEnd, WalkState);

    ACPI_FREE (WalkState);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    XfObjectExists
 *
 * PARAMETERS:  Name            - 4 char ACPI name
 *
 * RETURN:      TRUE if name exists in namespace
 *
 * DESCRIPTION: Walk the namespace to find an object
 *
 ******************************************************************************/

static BOOLEAN
XfObjectExists (
    char                    *Name)
{
    ACPI_STATUS             Status;


    /* Walk entire namespace from the supplied root */

    Status = AcpiNsWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, FALSE, XfCompareOneNamespaceObject, NULL,
        Name, NULL);
    if (Status == AE_CTRL_TRUE)
    {
        /* At least one instance of the name was found */

        return (TRUE);
    }

    return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    XfCompareOneNamespaceObject
 *
 * PARAMETERS:  ACPI_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compare name of one object.
 *
 ******************************************************************************/

static ACPI_STATUS
XfCompareOneNamespaceObject (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;


    /* Simply check the name */

    if (*((UINT32 *) (Context)) == Node->Name.Integer)
    {
        /* Abort walk if we found one instance */

        return (AE_CTRL_TRUE);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    XfCheckFieldRange
 *
 * PARAMETERS:  RegionBitLength     - Length of entire parent region
 *              FieldBitOffset      - Start of the field unit (within region)
 *              FieldBitLength      - Entire length of field unit
 *              AccessBitWidth      - Access width of the field unit
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check one field unit to make sure it fits in the parent
 *              op region.
 *
 * Note: AccessBitWidth must be either 8,16,32, or 64
 *
 ******************************************************************************/

static void
XfCheckFieldRange (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  RegionBitLength,
    UINT32                  FieldBitOffset,
    UINT32                  FieldBitLength,
    UINT32                  AccessBitWidth)
{
    UINT32                  FieldEndBitOffset;


    /*
     * Check each field unit against the region size. The entire
     * field unit (start offset plus length) must fit within the
     * region.
     */
    FieldEndBitOffset = FieldBitOffset + FieldBitLength;

    if (FieldEndBitOffset > RegionBitLength)
    {
        /* Field definition itself is beyond the end-of-region */

        AslError (ASL_ERROR, ASL_MSG_FIELD_UNIT_OFFSET, Op, NULL);
        return;
    }

    /*
     * Now check that the field plus AccessWidth doesn't go beyond
     * the end-of-region. Assumes AccessBitWidth is a power of 2
     */
    FieldEndBitOffset = ACPI_ROUND_UP (FieldEndBitOffset, AccessBitWidth);

    if (FieldEndBitOffset > RegionBitLength)
    {
        /* Field definition combined with the access is beyond EOR */

        AslError (ASL_ERROR, ASL_MSG_FIELD_UNIT_ACCESS_WIDTH, Op, NULL);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    XfGetParentMethod
 *
 * PARAMETERS:  Op                      - Parse Op to be checked
 *
 * RETURN:      Control method Op if found. NULL otherwise
 *
 * DESCRIPTION: Find the control method parent of a parse op. Returns NULL if
 *              the input Op is not within a control method.
 *
 ******************************************************************************/

static ACPI_PARSE_OBJECT *
XfGetParentMethod (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *NextOp;


    NextOp = Op->Asl.Parent;
    while (NextOp)
    {
        if (NextOp->Asl.AmlOpcode == AML_METHOD_OP)
        {
            return (NextOp);
        }

        NextOp = NextOp->Asl.Parent;
    }

    return (NULL); /* No parent method found */
}


/*******************************************************************************
 *
 * FUNCTION:    XfNamespaceLocateBegin
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback used during cross-reference. For named
 *              object references, attempt to locate the name in the
 *              namespace.
 *
 * NOTE: ASL references to named fields within resource descriptors are
 *       resolved to integer values here. Therefore, this step is an
 *       important part of the code generation. We don't know that the
 *       name refers to a resource descriptor until now.
 *
 ******************************************************************************/

static ACPI_STATUS
XfNamespaceLocateBegin (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ACPI_WALK_STATE         *WalkState = (ACPI_WALK_STATE *) Context;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_STATUS             Status;
    ACPI_OBJECT_TYPE        ObjectType;
    char                    *Path;
    UINT8                   PassedArgs;
    ACPI_PARSE_OBJECT       *NextOp;
    ACPI_PARSE_OBJECT       *OwningOp;
    ACPI_PARSE_OBJECT       *SpaceIdOp;
    UINT32                  MinimumLength;
    UINT32                  Offset;
    UINT32                  FieldBitLength;
    UINT32                  TagBitLength;
    UINT8                   Message = 0;
    const ACPI_OPCODE_INFO  *OpInfo;
    UINT32                  Flags;
    ASL_METHOD_LOCAL        *MethodLocals = NULL;
    ASL_METHOD_LOCAL        *MethodArgs = NULL;
    int                     RegisterNumber;
    UINT32                  i;


    ACPI_FUNCTION_TRACE_PTR (XfNamespaceLocateBegin, Op);


    if ((Op->Asl.AmlOpcode == AML_METHOD_OP) && Op->Asl.Node)
    {
        Node = Op->Asl.Node;

        /* Support for method LocalX/ArgX analysis */

        if (!Node->MethodLocals)
        {
            /* Create local/arg info blocks */

            MethodLocals = UtLocalCalloc (
                sizeof (ASL_METHOD_LOCAL) * ACPI_METHOD_NUM_LOCALS);
            Node->MethodLocals = MethodLocals;

            MethodArgs = UtLocalCalloc (
                sizeof (ASL_METHOD_LOCAL) * ACPI_METHOD_NUM_ARGS);
            Node->MethodArgs = MethodArgs;

            /*
             * Get the method argument count
             * First, get the name node
             */
            NextOp = Op->Asl.Child;

            /* Get the NumArguments node */

            NextOp = NextOp->Asl.Next;
            Node->ArgCount = (UINT8)
                (((UINT8) NextOp->Asl.Value.Integer) & 0x07);

            /* We will track all possible ArgXs */

            for (i = 0; i < ACPI_METHOD_NUM_ARGS; i++)
            {
                if (i < Node->ArgCount)
                {
                    /* Real Args are always "initialized" */

                    MethodArgs[i].Flags = ASL_ARG_INITIALIZED;
                }
                else
                {
                    /* Other ArgXs can be used as locals */

                    MethodArgs[i].Flags = ASL_ARG_IS_LOCAL;
                }

                MethodArgs[i].Op = Op;
            }
        }
    }

    /*
     * If this node is the actual declaration of a name
     * [such as the XXXX name in "Method (XXXX)"],
     * we are not interested in it here. We only care about names that are
     * references to other objects within the namespace and the parent objects
     * of name declarations
     */
    if (Op->Asl.CompileFlags & OP_IS_NAME_DECLARATION)
    {
        return_ACPI_STATUS (AE_OK);
    }

    OpInfo = AcpiPsGetOpcodeInfo (Op->Asl.AmlOpcode);

    /* Check method LocalX variables */

    if (OpInfo->Type == AML_TYPE_LOCAL_VARIABLE)
    {
        /* Find parent method Op */

        NextOp = XfGetParentMethod (Op);
        if (!NextOp)
        {
            return_ACPI_STATUS (AE_OK);
        }

        /* Get method node */

        Node = NextOp->Asl.Node;

        RegisterNumber = Op->Asl.AmlOpcode & 0x0007; /* 0x60 through 0x67 */
        MethodLocals = Node->MethodLocals;

        if (Op->Asl.CompileFlags & OP_IS_TARGET)
        {
            /* Local is being initialized */

            MethodLocals[RegisterNumber].Flags |= ASL_LOCAL_INITIALIZED;
            MethodLocals[RegisterNumber].Op = Op;

            return_ACPI_STATUS (AE_OK);
        }

        /* Mark this Local as referenced */

        MethodLocals[RegisterNumber].Flags |= ASL_LOCAL_REFERENCED;
        MethodLocals[RegisterNumber].Op = Op;

        return_ACPI_STATUS (AE_OK);
    }

    /* Check method ArgX variables */

    if (OpInfo->Type == AML_TYPE_METHOD_ARGUMENT)
    {
        /* Find parent method Op */

        NextOp = XfGetParentMethod (Op);
        if (!NextOp)
        {
            return_ACPI_STATUS (AE_OK);
        }

        /* Get method node */

        Node = NextOp->Asl.Node;

        /* Get Arg # */

        RegisterNumber = Op->Asl.AmlOpcode - AML_ARG0; /* 0x68 through 0x6F */
        MethodArgs = Node->MethodArgs;

        /* Mark this Arg as referenced */

        MethodArgs[RegisterNumber].Flags |= ASL_ARG_REFERENCED;
        MethodArgs[RegisterNumber].Op = Op;

        if (Op->Asl.CompileFlags & OP_IS_TARGET)
        {
            /* Arg is being initialized */

            MethodArgs[RegisterNumber].Flags |= ASL_ARG_INITIALIZED;
        }

        return_ACPI_STATUS (AE_OK);
    }

    /*
     * After method ArgX and LocalX, we are only interested in opcodes
     * that have an associated name
     */
    if ((!(OpInfo->Flags & AML_NAMED)) &&
        (!(OpInfo->Flags & AML_CREATE)) &&
        (Op->Asl.ParseOpcode != PARSEOP_NAMESTRING) &&
        (Op->Asl.ParseOpcode != PARSEOP_NAMESEG)    &&
        (Op->Asl.ParseOpcode != PARSEOP_METHODCALL) &&
        (Op->Asl.ParseOpcode != PARSEOP_EXTERNAL))
    {
        return_ACPI_STATUS (AE_OK);
    }

    /*
     * One special case: CondRefOf operator - we don't care if the name exists
     * or not at this point, just ignore it, the point of the operator is to
     * determine if the name exists at runtime.
     */
    if ((Op->Asl.Parent) &&
        (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CONDREFOF))
    {
        return_ACPI_STATUS (AE_OK);
    }

    /*
     * We must enable the "search-to-root" for single NameSegs, but
     * we have to be very careful about opening up scopes
     */
    Flags = ACPI_NS_SEARCH_PARENT;
    if ((Op->Asl.ParseOpcode == PARSEOP_NAMESTRING) ||
        (Op->Asl.ParseOpcode == PARSEOP_NAMESEG)    ||
        (Op->Asl.ParseOpcode == PARSEOP_METHODCALL) ||
        (Op->Asl.ParseOpcode == PARSEOP_EXTERNAL))
    {
        /*
         * These are name references, do not push the scope stack
         * for them.
         */
        Flags |= ACPI_NS_DONT_OPEN_SCOPE;
    }

    /* Get the NamePath from the appropriate place */

    if (OpInfo->Flags & AML_NAMED)
    {
        /* For nearly all NAMED operators, the name reference is the first child */

        Path = Op->Asl.Child->Asl.Value.String;
        if (Op->Asl.AmlOpcode == AML_ALIAS_OP)
        {
            /*
             * ALIAS is the only oddball opcode, the name declaration
             * (alias name) is the second operand
             */
            Path = Op->Asl.Child->Asl.Next->Asl.Value.String;
        }
    }
    else if (OpInfo->Flags & AML_CREATE)
    {
        /* Name must appear as the last parameter */

        NextOp = Op->Asl.Child;
        while (!(NextOp->Asl.CompileFlags & OP_IS_NAME_DECLARATION))
        {
            NextOp = NextOp->Asl.Next;
        }

        Path = NextOp->Asl.Value.String;
    }
    else
    {
        Path = Op->Asl.Value.String;
    }

    ObjectType = AslMapNamedOpcodeToDataType (Op->Asl.AmlOpcode);
    ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
        "Type=%s\n", AcpiUtGetTypeName (ObjectType)));

    /*
     * Lookup the name in the namespace. Name must exist at this point, or it
     * is an invalid reference.
     *
     * The namespace is also used as a lookup table for references to resource
     * descriptors and the fields within them.
     */
    AslGbl_NsLookupCount++;

    Status = AcpiNsLookup (WalkState->ScopeInfo, Path, ObjectType,
        ACPI_IMODE_EXECUTE, Flags, WalkState, &Node);
    if (ACPI_FAILURE (Status))
    {
        if (Status == AE_NOT_FOUND)
        {
            /*
             * We didn't find the name reference by path -- we can qualify this
             * a little better before we print an error message
             */
            if (strlen (Path) == ACPI_NAME_SIZE)
            {
                /* A simple, one-segment ACPI name */

                if (XfObjectExists (Path))
                {
                    /*
                     * There exists such a name, but we couldn't get to it
                     * from this scope
                     */
                    AslError (ASL_ERROR, ASL_MSG_NOT_REACHABLE, Op,
                        Op->Asl.ExternalName);
                }
                else
                {
                    /* The name doesn't exist, period */

                    AslError (ASL_ERROR, ASL_MSG_NOT_EXIST,
                        Op, Op->Asl.ExternalName);
                }
            }
            else
            {
                /* The NamePath contains multiple NameSegs */

                if ((OpInfo->Flags & AML_CREATE) ||
                    (OpInfo->ObjectType == ACPI_TYPE_LOCAL_ALIAS))
                {
                    /*
                     * The new name is the last parameter. For the
                     * CreateXXXXField and Alias operators
                     */
                    NextOp = Op->Asl.Child;
                    while (!(NextOp->Asl.CompileFlags & OP_IS_NAME_DECLARATION))
                    {
                        NextOp = NextOp->Asl.Next;
                    }

                    AslError (ASL_ERROR, ASL_MSG_PREFIX_NOT_EXIST, NextOp,
                        NextOp->Asl.ExternalName);
                }
                else if (OpInfo->Flags & AML_NAMED)
                {
                    /* The new name is the first parameter */

                    AslError (ASL_ERROR, ASL_MSG_PREFIX_NOT_EXIST, Op,
                        Op->Asl.ExternalName);
                }
                else if (Path[0] == AML_ROOT_PREFIX)
                {
                    /* Full namepath from root, the object does not exist */

                    AslError (ASL_ERROR, ASL_MSG_NOT_EXIST, Op,
                        Op->Asl.ExternalName);
                }
                else
                {
                    /*
                     * Generic "not found" error. Cannot determine whether it
                     * doesn't exist or just can't be reached. However, we
                     * can differentiate between a NameSeg vs. NamePath.
                     */
                    if (strlen (Op->Asl.ExternalName) == ACPI_NAME_SIZE)
                    {
                        AslError (ASL_ERROR, ASL_MSG_NOT_FOUND, Op,
                            Op->Asl.ExternalName);
                    }
                    else
                    {
                        AslError (ASL_ERROR, ASL_MSG_NAMEPATH_NOT_EXIST, Op,
                            Op->Asl.ExternalName);
                    }
                }
            }

            Status = AE_OK;
        }

        return_ACPI_STATUS (Status);
    }

    /* Check for an attempt to access an object in another method */

    if (!XfValidateCrossReference (Op, OpInfo, Node))
    {
        AslError (ASL_ERROR, ASL_MSG_TEMPORARY_OBJECT, Op,
            Op->Asl.ExternalName);
        return_ACPI_STATUS (Status);
    }

    /* Object was found above, check for an illegal forward reference */

    if (Op->Asl.CompileFlags & OP_NOT_FOUND_DURING_LOAD)
    {
        /*
         * During the load phase, this Op was flagged as a possible
         * illegal forward reference
         *
         * Note: Allow "forward references" from within a method to an
         * object that is not within any method (module-level code)
         */
        if (!WalkState->ScopeInfo || (UtGetParentMethod (Node) &&
            !UtNodeIsDescendantOf (WalkState->ScopeInfo->Scope.Node,
                UtGetParentMethod (Node))))
        {
            AslError (ASL_ERROR, ASL_MSG_ILLEGAL_FORWARD_REF, Op,
                Op->Asl.ExternalName);
        }
    }

    /* Check for a reference vs. name declaration */

    if (!(OpInfo->Flags & AML_NAMED) &&
        !(OpInfo->Flags & AML_CREATE))
    {
        /* This node has been referenced, mark it for reference check */

        Node->Flags |= ANOBJ_IS_REFERENCED;
    }

    /* Attempt to optimize the NamePath */

    OptOptimizeNamePath (Op, OpInfo->Flags, WalkState, Path, Node);

    /*
     * 1) Dereference an alias (A name reference that is an alias)
     *    Aliases are not nested, the alias always points to the final object
     */
    if ((Op->Asl.ParseOpcode != PARSEOP_ALIAS) &&
        (Node->Type == ACPI_TYPE_LOCAL_ALIAS))
    {
        /* This node points back to the original PARSEOP_ALIAS */

        NextOp = Node->Op;

        /* The first child is the alias target op */

        NextOp = NextOp->Asl.Child;

        /* That in turn points back to original target alias node */

        if (NextOp->Asl.Node)
        {
            Node = NextOp->Asl.Node;
        }

        /* Else - forward reference to alias, will be resolved later */
    }

    /* 2) Check for a reference to a resource descriptor */

    if ((Node->Type == ACPI_TYPE_LOCAL_RESOURCE_FIELD) ||
        (Node->Type == ACPI_TYPE_LOCAL_RESOURCE))
    {
        /*
         * This was a reference to a field within a resource descriptor.
         * Extract the associated field offset (either a bit or byte
         * offset depending on the field type) and change the named
         * reference into an integer for AML code generation
         */
        Offset = Node->Value;
        TagBitLength = Node->Length;

        /*
         * If a field is being created, generate the length (in bits) of
         * the field. Note: Opcodes other than CreateXxxField and Index
         * can come through here. For other opcodes, we just need to
         * convert the resource tag reference to an integer offset.
         */
        switch (Op->Asl.Parent->Asl.AmlOpcode)
        {
        case AML_CREATE_FIELD_OP: /* Variable "Length" field, in bits */
            /*
             * We know the length operand is an integer constant because
             * we know that it contains a reference to a resource
             * descriptor tag.
             */
            FieldBitLength = (UINT32) Op->Asl.Next->Asl.Value.Integer;
            break;

        case AML_CREATE_BIT_FIELD_OP:

            FieldBitLength = 1;
            break;

        case AML_CREATE_BYTE_FIELD_OP:
        case AML_INDEX_OP:

            FieldBitLength = 8;
            break;

        case AML_CREATE_WORD_FIELD_OP:

            FieldBitLength = 16;
            break;

        case AML_CREATE_DWORD_FIELD_OP:

            FieldBitLength = 32;
            break;

        case AML_CREATE_QWORD_FIELD_OP:

            FieldBitLength = 64;
            break;

        default:

            FieldBitLength = 0;
            break;
        }

        /* Check the field length against the length of the resource tag */

        if (FieldBitLength)
        {
            if (TagBitLength < FieldBitLength)
            {
                Message = ASL_MSG_TAG_SMALLER;
            }
            else if (TagBitLength > FieldBitLength)
            {
                Message = ASL_MSG_TAG_LARGER;
            }

            if (Message)
            {
                sprintf (AslGbl_MsgBuffer,
                    "Size mismatch, Tag: %u bit%s, Field: %u bit%s",
                    TagBitLength, (TagBitLength > 1) ? "s" : "",
                    FieldBitLength, (FieldBitLength > 1) ? "s" : "");

                AslError (ASL_WARNING, Message, Op, AslGbl_MsgBuffer);
            }
        }

        /* Convert the BitOffset to a ByteOffset for certain opcodes */

        switch (Op->Asl.Parent->Asl.AmlOpcode)
        {
        case AML_CREATE_BYTE_FIELD_OP:
        case AML_CREATE_WORD_FIELD_OP:
        case AML_CREATE_DWORD_FIELD_OP:
        case AML_CREATE_QWORD_FIELD_OP:
        case AML_INDEX_OP:

            Offset = ACPI_DIV_8 (Offset);
            break;

        default:

            break;
        }

        /* Now convert this node to an integer whose value is the field offset */

        Op->Asl.AmlLength = 0;
        Op->Asl.ParseOpcode = PARSEOP_INTEGER;
        Op->Asl.Value.Integer = (UINT64) Offset;
        Op->Asl.CompileFlags |= OP_IS_RESOURCE_FIELD;

        OpcGenerateAmlOpcode (Op);
    }

    /* 3) Check for a method invocation */

    else if ((((Op->Asl.ParseOpcode == PARSEOP_NAMESTRING) || (Op->Asl.ParseOpcode == PARSEOP_NAMESEG)) &&
                (Node->Type == ACPI_TYPE_METHOD) &&
                (Op->Asl.Parent) &&
                (Op->Asl.Parent->Asl.ParseOpcode != PARSEOP_METHOD))   ||

                (Op->Asl.ParseOpcode == PARSEOP_METHODCALL))
    {
        /*
         * A reference to a method within one of these opcodes is not an
         * invocation of the method, it is simply a reference to the method.
         *
         * September 2016: Removed DeRefOf from this list
         */
        if ((Op->Asl.Parent) &&
            ((Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_REFOF)     ||
            (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_PACKAGE)    ||
            (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_VAR_PACKAGE)||
            (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_OBJECTTYPE)))
        {
            return_ACPI_STATUS (AE_OK);
        }

        /*
         * There are two types of method invocation:
         * 1) Invocation with arguments -- the parser recognizes this
         *    as a METHODCALL.
         * 2) Invocation with no arguments --the parser cannot determine that
         *    this is a method invocation, therefore we have to figure it out
         *    here.
         */
        if (Node->Type != ACPI_TYPE_METHOD)
        {
            sprintf (AslGbl_MsgBuffer, "%s is a %s",
                Op->Asl.ExternalName, AcpiUtGetTypeName (Node->Type));

            AslError (ASL_ERROR, ASL_MSG_NOT_METHOD, Op, AslGbl_MsgBuffer);
            return_ACPI_STATUS (AE_OK);
        }

        /* Save the method node in the caller's op */

        Op->Asl.Node = Node;
        if (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CONDREFOF)
        {
            return_ACPI_STATUS (AE_OK);
        }

        /*
         * This is a method invocation, with or without arguments.
         * Count the number of arguments, each appears as a child
         * under the parent node
         */
        Op->Asl.ParseOpcode = PARSEOP_METHODCALL;
        UtSetParseOpName (Op);

        PassedArgs = 0;
        NextOp = Op->Asl.Child;

        while (NextOp)
        {
            PassedArgs++;
            NextOp = NextOp->Asl.Next;
        }

        if (Node->Value != ASL_EXTERNAL_METHOD &&
            Op->Asl.Parent->Asl.ParseOpcode != PARSEOP_EXTERNAL)
        {
            /*
             * Check the parsed arguments with the number expected by the
             * method declaration itself
             */
            if (PassedArgs != Node->Value)
            {
                sprintf (AslGbl_MsgBuffer, "%s requires %u", Op->Asl.ExternalName,
                    Node->Value);

                if (PassedArgs < Node->Value)
                {
                    AslError (ASL_ERROR, ASL_MSG_ARG_COUNT_LO, Op, AslGbl_MsgBuffer);
                }
                else
                {
                    AslError (ASL_ERROR, ASL_MSG_ARG_COUNT_HI, Op, AslGbl_MsgBuffer);
                }
            }
        }
    }

    /* 4) Check for an ASL Field definition */

    else if ((Op->Asl.Parent) &&
            ((Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_FIELD)     ||
             (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_BANKFIELD)))
    {
        /*
         * Offset checking for fields. If the parent operation region has a
         * constant length (known at compile time), we can check fields
         * defined in that region against the region length. This will catch
         * fields and field units that cannot possibly fit within the region.
         *
         * Note: Index fields do not directly reference an operation region,
         * thus they are not included in this check.
         */
        if (Op == Op->Asl.Parent->Asl.Child)
        {
            /*
             * This is the first child of the field node, which is
             * the name of the region. Get the parse node for the
             * region -- which contains the length of the region.
             */
            OwningOp = Node->Op;
            Op->Asl.Parent->Asl.ExtraValue =
                ACPI_MUL_8 ((UINT32) OwningOp->Asl.Value.Integer);

            /* Examine the field access width */

            switch ((UINT8) Op->Asl.Parent->Asl.Value.Integer)
            {
            case AML_FIELD_ACCESS_ANY:
            case AML_FIELD_ACCESS_BYTE:
            case AML_FIELD_ACCESS_BUFFER:
            default:

                MinimumLength = 1;
                break;

            case AML_FIELD_ACCESS_WORD:

                MinimumLength = 2;
                break;

            case AML_FIELD_ACCESS_DWORD:

                MinimumLength = 4;
                break;

            case AML_FIELD_ACCESS_QWORD:

                MinimumLength = 8;
                break;
            }

            /*
             * Is the region at least as big as the access width?
             * Note: DataTableRegions have 0 length
             */
            if (((UINT32) OwningOp->Asl.Value.Integer) &&
                ((UINT32) OwningOp->Asl.Value.Integer < MinimumLength))
            {
                AslError (ASL_ERROR, ASL_MSG_FIELD_ACCESS_WIDTH, Op, NULL);
            }

            /*
             * Check EC/CMOS/SMBUS fields to make sure that the correct
             * access type is used (BYTE for EC/CMOS, BUFFER for SMBUS)
             */
            SpaceIdOp = OwningOp->Asl.Child->Asl.Next;
            switch ((UINT32) SpaceIdOp->Asl.Value.Integer)
            {
            case ACPI_ADR_SPACE_EC:
            case ACPI_ADR_SPACE_CMOS:
            case ACPI_ADR_SPACE_GPIO:

                if ((UINT8) Op->Asl.Parent->Asl.Value.Integer !=
                    AML_FIELD_ACCESS_BYTE)
                {
                    AslError (ASL_ERROR, ASL_MSG_REGION_BYTE_ACCESS, Op, NULL);
                }
                break;

            case ACPI_ADR_SPACE_SMBUS:
            case ACPI_ADR_SPACE_IPMI:
            case ACPI_ADR_SPACE_GSBUS:

                if ((UINT8) Op->Asl.Parent->Asl.Value.Integer !=
                    AML_FIELD_ACCESS_BUFFER)
                {
                    AslError (ASL_ERROR, ASL_MSG_REGION_BUFFER_ACCESS, Op, NULL);
                }
                break;

            default:

                /* Nothing to do for other address spaces */

                break;
            }
        }
        else
        {
            /*
             * This is one element of the field list. Check to make sure
             * that it does not go beyond the end of the parent operation region.
             *
             * In the code below:
             *    Op->Asl.Parent->Asl.ExtraValue      - Region Length (bits)
             *    Op->Asl.ExtraValue                  - Field start offset (bits)
             *    Op->Asl.Child->Asl.Value.Integer32  - Field length (bits)
             *    Op->Asl.Child->Asl.ExtraValue       - Field access width (bits)
             */
            if (Op->Asl.Parent->Asl.ExtraValue && Op->Asl.Child)
            {
                XfCheckFieldRange (Op,
                    Op->Asl.Parent->Asl.ExtraValue,
                    Op->Asl.ExtraValue,
                    (UINT32) Op->Asl.Child->Asl.Value.Integer,
                    Op->Asl.Child->Asl.ExtraValue);
            }
        }
    }

    /* 5) Check for a connection object */
#if 0
    else if (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CONNECTION)
    {
        return_ACPI_STATUS (Status);
    }
#endif

    Op->Asl.Node = Node;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    XfNamespaceLocateEnd
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during cross reference. We only
 *              need to worry about scope management here.
 *
 ******************************************************************************/

static ACPI_STATUS
XfNamespaceLocateEnd (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ACPI_WALK_STATE         *WalkState = (ACPI_WALK_STATE *) Context;
    const ACPI_OPCODE_INFO  *OpInfo;


    ACPI_FUNCTION_TRACE (XfNamespaceLocateEnd);


    /* We are only interested in opcodes that have an associated name */

    OpInfo = AcpiPsGetOpcodeInfo (Op->Asl.AmlOpcode);
    if (!(OpInfo->Flags & AML_NAMED))
    {
        return_ACPI_STATUS (AE_OK);
    }

    /* Not interested in name references, we did not open a scope for them */

    if ((Op->Asl.ParseOpcode == PARSEOP_NAMESTRING) ||
        (Op->Asl.ParseOpcode == PARSEOP_NAMESEG)    ||
        (Op->Asl.ParseOpcode == PARSEOP_METHODCALL) ||
        (Op->Asl.ParseOpcode == PARSEOP_EXTERNAL))
    {
        return_ACPI_STATUS (AE_OK);
    }

    /* Pop the scope stack if necessary */

    if (AcpiNsOpensScope (AslMapNamedOpcodeToDataType (Op->Asl.AmlOpcode)))
    {

        ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
            "%s: Popping scope for Op %p\n",
            AcpiUtGetTypeName (OpInfo->ObjectType), Op));

        (void) AcpiDsScopeStackPop (WalkState);
    }

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    XfValidateCrossReference
 *
 * PARAMETERS:  Op                      - Parse Op that references the object
 *              OpInfo                  - Parse Op info struct
 *              Node                    - Node for the referenced object
 *
 * RETURN:      TRUE if the reference is legal, FALSE otherwise
 *
 * DESCRIPTION: Determine if a reference to another object is allowed.
 *
 * EXAMPLE:
 *      Method (A) {Name (INT1, 1)}     Declaration of object INT1
 *      Method (B) (Store (2, \A.INT1)} Illegal reference to object INT1
 *                                      (INT1 is temporary, valid only during
 *                                      execution of A)
 *
 * NOTES:
 *      A null pointer returned by either XfGetParentMethod or
 *      UtGetParentMethod indicates that the parameter object is not
 *      within a control method.
 *
 *      Five cases are handled: Case(Op, Node)
 *      1) Case(0,0): Op is not within a method, Node is not    --> OK
 *      2) Case(0,1): Op is not within a method, but Node is    --> Illegal
 *      3) Case(1,0): Op is within a method, Node is not        --> OK
 *      4) Case(1,1): Both are within the same method           --> OK
 *      5) Case(1,1): Both are in methods, but not same method  --> Illegal
 *
 ******************************************************************************/

static BOOLEAN
XfValidateCrossReference (
    ACPI_PARSE_OBJECT       *Op,
    const ACPI_OPCODE_INFO  *OpInfo,
    ACPI_NAMESPACE_NODE     *Node)
{
    ACPI_PARSE_OBJECT       *ReferencingMethodOp;
    ACPI_NAMESPACE_NODE     *ReferencedMethodNode;


    /* Ignore actual named (and related) object declarations */

    if (OpInfo->Flags & (AML_NAMED | AML_CREATE | AML_DEFER | AML_HAS_ARGS))
    {
        return (TRUE);
    }

    /*
     * 1) Search upwards in parse tree for owner of the referencing object
     * 2) Search upwards in namespace to find the owner of the referenced object
     */
    ReferencingMethodOp = XfGetParentMethod (Op);
    ReferencedMethodNode = UtGetParentMethod (Node);

    if (!ReferencingMethodOp && !ReferencedMethodNode)
    {
        /*
         * 1) Case (0,0): Both Op and Node are not within methods
         * --> OK
         */
        return (TRUE);
    }

    if (!ReferencingMethodOp && ReferencedMethodNode)
    {
        /*
         * 2) Case (0,1): Op is not in a method, but Node is within a
         * method --> illegal
         */
        return (FALSE);
    }
    else if (ReferencingMethodOp && !ReferencedMethodNode)
    {
        /*
         * 3) Case (1,0): Op is within a method, but Node is not
         * --> OK
         */
        return (TRUE);
    }
    else if (ReferencingMethodOp->Asl.Node == ReferencedMethodNode)
    {
        /*
         * 4) Case (1,1): Both Op and Node are within the same method
         * --> OK
         */
        return (TRUE);
    }
    else
    {
        /*
         * 5) Case (1,1), Op and Node are in different methods
         * --> Illegal
         */
        return (FALSE);
    }
}
