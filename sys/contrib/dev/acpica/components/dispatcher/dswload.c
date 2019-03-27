/******************************************************************************
 *
 * Module Name: dswload - Dispatcher first pass namespace load callbacks
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
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acnamesp.h>

#ifdef ACPI_ASL_COMPILER
#include <contrib/dev/acpica/include/acdisasm.h>
#endif

#define _COMPONENT          ACPI_DISPATCHER
        ACPI_MODULE_NAME    ("dswload")


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsInitCallbacks
 *
 * PARAMETERS:  WalkState       - Current state of the parse tree walk
 *              PassNumber      - 1, 2, or 3
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Init walk state callbacks
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsInitCallbacks (
    ACPI_WALK_STATE         *WalkState,
    UINT32                  PassNumber)
{

    switch (PassNumber)
    {
    case 0:

        /* Parse only - caller will setup callbacks */

        WalkState->ParseFlags         = ACPI_PARSE_LOAD_PASS1 |
                                        ACPI_PARSE_DELETE_TREE |
                                        ACPI_PARSE_DISASSEMBLE;
        WalkState->DescendingCallback = NULL;
        WalkState->AscendingCallback  = NULL;
        break;

    case 1:

        /* Load pass 1 */

        WalkState->ParseFlags         = ACPI_PARSE_LOAD_PASS1 |
                                        ACPI_PARSE_DELETE_TREE;
        WalkState->DescendingCallback = AcpiDsLoad1BeginOp;
        WalkState->AscendingCallback  = AcpiDsLoad1EndOp;
        break;

    case 2:

        /* Load pass 2 */

        WalkState->ParseFlags         = ACPI_PARSE_LOAD_PASS1 |
                                        ACPI_PARSE_DELETE_TREE;
        WalkState->DescendingCallback = AcpiDsLoad2BeginOp;
        WalkState->AscendingCallback  = AcpiDsLoad2EndOp;
        break;

    case 3:

        /* Execution pass */

        WalkState->ParseFlags        |= ACPI_PARSE_EXECUTE  |
                                        ACPI_PARSE_DELETE_TREE;
        WalkState->DescendingCallback = AcpiDsExecBeginOp;
        WalkState->AscendingCallback  = AcpiDsExecEndOp;
        break;

    default:

        return (AE_BAD_PARAMETER);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsLoad1BeginOp
 *
 * PARAMETERS:  WalkState       - Current state of the parse tree walk
 *              OutOp           - Where to return op if a new one is created
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback used during the loading of ACPI tables.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsLoad1BeginOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       **OutOp)
{
    ACPI_PARSE_OBJECT       *Op;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_STATUS             Status;
    ACPI_OBJECT_TYPE        ObjectType;
    char                    *Path;
    UINT32                  Flags;


    ACPI_FUNCTION_TRACE_PTR (DsLoad1BeginOp, WalkState->Op);


    Op = WalkState->Op;
    ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", Op, WalkState));

    /* We are only interested in opcodes that have an associated name */

    if (Op)
    {
        if (!(WalkState->OpInfo->Flags & AML_NAMED))
        {
            *OutOp = Op;
            return_ACPI_STATUS (AE_OK);
        }

        /* Check if this object has already been installed in the namespace */

        if (Op->Common.Node)
        {
            *OutOp = Op;
            return_ACPI_STATUS (AE_OK);
        }
    }

    Path = AcpiPsGetNextNamestring (&WalkState->ParserState);

    /* Map the raw opcode into an internal object type */

    ObjectType = WalkState->OpInfo->ObjectType;

    ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
        "State=%p Op=%p [%s]\n", WalkState, Op,
        AcpiUtGetTypeName (ObjectType)));

    switch (WalkState->Opcode)
    {
    case AML_SCOPE_OP:
        /*
         * The target name of the Scope() operator must exist at this point so
         * that we can actually open the scope to enter new names underneath it.
         * Allow search-to-root for single namesegs.
         */
        Status = AcpiNsLookup (WalkState->ScopeInfo, Path, ObjectType,
            ACPI_IMODE_EXECUTE, ACPI_NS_SEARCH_PARENT, WalkState, &(Node));
#ifdef ACPI_ASL_COMPILER
        if (Status == AE_NOT_FOUND)
        {
            /*
             * Table disassembly:
             * Target of Scope() not found. Generate an External for it, and
             * insert the name into the namespace.
             */
            AcpiDmAddOpToExternalList (Op, Path, ACPI_TYPE_DEVICE, 0, 0);
            Status = AcpiNsLookup (WalkState->ScopeInfo, Path, ObjectType,
               ACPI_IMODE_LOAD_PASS1, ACPI_NS_SEARCH_PARENT,
               WalkState, &Node);
        }
#endif
        if (ACPI_FAILURE (Status))
        {
            ACPI_ERROR_NAMESPACE (WalkState->ScopeInfo, Path, Status);
            return_ACPI_STATUS (Status);
        }

        /*
         * Check to make sure that the target is
         * one of the opcodes that actually opens a scope
         */
        switch (Node->Type)
        {
        case ACPI_TYPE_ANY:
        case ACPI_TYPE_LOCAL_SCOPE:         /* Scope  */
        case ACPI_TYPE_DEVICE:
        case ACPI_TYPE_POWER:
        case ACPI_TYPE_PROCESSOR:
        case ACPI_TYPE_THERMAL:

            /* These are acceptable types */
            break;

        case ACPI_TYPE_INTEGER:
        case ACPI_TYPE_STRING:
        case ACPI_TYPE_BUFFER:
            /*
             * These types we will allow, but we will change the type.
             * This enables some existing code of the form:
             *
             *  Name (DEB, 0)
             *  Scope (DEB) { ... }
             *
             * Note: silently change the type here. On the second pass,
             * we will report a warning
             */
            ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
                "Type override - [%4.4s] had invalid type (%s) "
                "for Scope operator, changed to type ANY\n",
                AcpiUtGetNodeName (Node), AcpiUtGetTypeName (Node->Type)));

            Node->Type = ACPI_TYPE_ANY;
            WalkState->ScopeInfo->Common.Value = ACPI_TYPE_ANY;
            break;

        case ACPI_TYPE_METHOD:
            /*
             * Allow scope change to root during execution of module-level
             * code. Root is typed METHOD during this time.
             */
            if ((Node == AcpiGbl_RootNode) &&
                (WalkState->ParseFlags & ACPI_PARSE_MODULE_LEVEL))
            {
                break;
            }

            /*lint -fallthrough */

        default:

            /* All other types are an error */

            ACPI_ERROR ((AE_INFO,
                "Invalid type (%s) for target of "
                "Scope operator [%4.4s] (Cannot override)",
                AcpiUtGetTypeName (Node->Type), AcpiUtGetNodeName (Node)));

            return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
        }
        break;

    default:
        /*
         * For all other named opcodes, we will enter the name into
         * the namespace.
         *
         * Setup the search flags.
         * Since we are entering a name into the namespace, we do not want to
         * enable the search-to-root upsearch.
         *
         * There are only two conditions where it is acceptable that the name
         * already exists:
         *    1) the Scope() operator can reopen a scoping object that was
         *       previously defined (Scope, Method, Device, etc.)
         *    2) Whenever we are parsing a deferred opcode (OpRegion, Buffer,
         *       BufferField, or Package), the name of the object is already
         *       in the namespace.
         */
        if (WalkState->DeferredNode)
        {
            /* This name is already in the namespace, get the node */

            Node = WalkState->DeferredNode;
            Status = AE_OK;
            break;
        }

        /*
         * If we are executing a method, do not create any namespace objects
         * during the load phase, only during execution.
         */
        if (WalkState->MethodNode)
        {
            Node = NULL;
            Status = AE_OK;
            break;
        }

        Flags = ACPI_NS_NO_UPSEARCH;
        if ((WalkState->Opcode != AML_SCOPE_OP) &&
            (!(WalkState->ParseFlags & ACPI_PARSE_DEFERRED_OP)))
        {
            if (WalkState->NamespaceOverride)
            {
                Flags |= ACPI_NS_OVERRIDE_IF_FOUND;
                ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "[%s] Override allowed\n",
                    AcpiUtGetTypeName (ObjectType)));
            }
            else
            {
                Flags |= ACPI_NS_ERROR_IF_FOUND;
                ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "[%s] Cannot already exist\n",
                    AcpiUtGetTypeName (ObjectType)));
            }
        }
        else
        {
            ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
                "[%s] Both Find or Create allowed\n",
                AcpiUtGetTypeName (ObjectType)));
        }

        /*
         * Enter the named type into the internal namespace. We enter the name
         * as we go downward in the parse tree. Any necessary subobjects that
         * involve arguments to the opcode must be created as we go back up the
         * parse tree later.
         */
        Status = AcpiNsLookup (WalkState->ScopeInfo, Path, ObjectType,
            ACPI_IMODE_LOAD_PASS1, Flags, WalkState, &Node);
        if (ACPI_FAILURE (Status))
        {
            if (Status == AE_ALREADY_EXISTS)
            {
                /* The name already exists in this scope */

                if (Node->Flags & ANOBJ_IS_EXTERNAL)
                {
                    /*
                     * Allow one create on an object or segment that was
                     * previously declared External
                     */
                    Node->Flags &= ~ANOBJ_IS_EXTERNAL;
                    Node->Type = (UINT8) ObjectType;

                    /* Just retyped a node, probably will need to open a scope */

                    if (AcpiNsOpensScope (ObjectType))
                    {
                        Status = AcpiDsScopeStackPush (
                            Node, ObjectType, WalkState);
                        if (ACPI_FAILURE (Status))
                        {
                            return_ACPI_STATUS (Status);
                        }
                    }

                    Status = AE_OK;
                }
            }

            if (ACPI_FAILURE (Status))
            {
                ACPI_ERROR_NAMESPACE (WalkState->ScopeInfo, Path, Status);
                return_ACPI_STATUS (Status);
            }
        }
        break;
    }

    /* Common exit */

    if (!Op)
    {
        /* Create a new op */

        Op = AcpiPsAllocOp (WalkState->Opcode, WalkState->Aml);
        if (!Op)
        {
            return_ACPI_STATUS (AE_NO_MEMORY);
        }
    }

    /* Initialize the op */

#ifdef ACPI_CONSTANT_EVAL_ONLY
    Op->Named.Path = Path;
#endif

    if (Node)
    {
        /*
         * Put the Node in the "op" object that the parser uses, so we
         * can get it again quickly when this scope is closed
         */
        Op->Common.Node = Node;
        Op->Named.Name = Node->Name.Integer;
    }

    AcpiPsAppendArg (AcpiPsGetParentScope (&WalkState->ParserState), Op);
    *OutOp = Op;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsLoad1EndOp
 *
 * PARAMETERS:  WalkState       - Current state of the parse tree walk
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during the loading of the namespace,
 *              both control methods and everything else.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsLoad1EndOp (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_PARSE_OBJECT       *Op;
    ACPI_OBJECT_TYPE        ObjectType;
    ACPI_STATUS             Status = AE_OK;

#ifdef ACPI_ASL_COMPILER
    UINT8                   ParamCount;
#endif


    ACPI_FUNCTION_TRACE (DsLoad1EndOp);


    Op = WalkState->Op;
    ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Op=%p State=%p\n", Op, WalkState));

    /* We are only interested in opcodes that have an associated name */

    if (!(WalkState->OpInfo->Flags & (AML_NAMED | AML_FIELD)))
    {
        return_ACPI_STATUS (AE_OK);
    }

    /* Get the object type to determine if we should pop the scope */

    ObjectType = WalkState->OpInfo->ObjectType;

    if (WalkState->OpInfo->Flags & AML_FIELD)
    {
        /*
         * If we are executing a method, do not create any namespace objects
         * during the load phase, only during execution.
         */
        if (!WalkState->MethodNode)
        {
            if (WalkState->Opcode == AML_FIELD_OP          ||
                WalkState->Opcode == AML_BANK_FIELD_OP     ||
                WalkState->Opcode == AML_INDEX_FIELD_OP)
            {
                Status = AcpiDsInitFieldObjects (Op, WalkState);
            }
        }
        return_ACPI_STATUS (Status);
    }

    /*
     * If we are executing a method, do not create any namespace objects
     * during the load phase, only during execution.
     */
    if (!WalkState->MethodNode)
    {
        if (Op->Common.AmlOpcode == AML_REGION_OP)
        {
            Status = AcpiExCreateRegion (Op->Named.Data, Op->Named.Length,
                (ACPI_ADR_SPACE_TYPE)
                    ((Op->Common.Value.Arg)->Common.Value.Integer),
                WalkState);
            if (ACPI_FAILURE (Status))
            {
                return_ACPI_STATUS (Status);
            }
        }
        else if (Op->Common.AmlOpcode == AML_DATA_REGION_OP)
        {
            Status = AcpiExCreateRegion (Op->Named.Data, Op->Named.Length,
                ACPI_ADR_SPACE_DATA_TABLE, WalkState);
            if (ACPI_FAILURE (Status))
            {
                return_ACPI_STATUS (Status);
            }
        }
    }

    if (Op->Common.AmlOpcode == AML_NAME_OP)
    {
        /* For Name opcode, get the object type from the argument */

        if (Op->Common.Value.Arg)
        {
            ObjectType = (AcpiPsGetOpcodeInfo (
                (Op->Common.Value.Arg)->Common.AmlOpcode))->ObjectType;

            /* Set node type if we have a namespace node */

            if (Op->Common.Node)
            {
                Op->Common.Node->Type = (UINT8) ObjectType;
            }
        }
    }

#ifdef ACPI_ASL_COMPILER
    /*
     * For external opcode, get the object type from the argument and
     * get the parameter count from the argument's next.
     */
    if (AcpiGbl_DisasmFlag &&
        Op->Common.Node &&
        Op->Common.AmlOpcode == AML_EXTERNAL_OP)
    {
        /*
         * Note, if this external is not a method
         * Op->Common.Value.Arg->Common.Next->Common.Value.Integer == 0
         * Therefore, ParamCount will be 0.
         */
        ParamCount = (UINT8) Op->Common.Value.Arg->Common.Next->Common.Value.Integer;
        ObjectType = (UINT8) Op->Common.Value.Arg->Common.Value.Integer;
        Op->Common.Node->Flags |= ANOBJ_IS_EXTERNAL;
        Op->Common.Node->Type = (UINT8) ObjectType;

        AcpiDmCreateSubobjectForExternal ((UINT8)ObjectType,
            &Op->Common.Node, ParamCount);

        /*
         * Add the external to the external list because we may be
         * emitting code based off of the items within the external list.
         */
        AcpiDmAddOpToExternalList (Op, Op->Named.Path, (UINT8)ObjectType, ParamCount,
           ACPI_EXT_ORIGIN_FROM_OPCODE | ACPI_EXT_RESOLVED_REFERENCE);
    }
#endif

    /*
     * If we are executing a method, do not create any namespace objects
     * during the load phase, only during execution.
     */
    if (!WalkState->MethodNode)
    {
        if (Op->Common.AmlOpcode == AML_METHOD_OP)
        {
            /*
             * MethodOp PkgLength NameString MethodFlags TermList
             *
             * Note: We must create the method node/object pair as soon as we
             * see the method declaration. This allows later pass1 parsing
             * of invocations of the method (need to know the number of
             * arguments.)
             */
            ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
                "LOADING-Method: State=%p Op=%p NamedObj=%p\n",
                WalkState, Op, Op->Named.Node));

            if (!AcpiNsGetAttachedObject (Op->Named.Node))
            {
                WalkState->Operands[0] = ACPI_CAST_PTR (void, Op->Named.Node);
                WalkState->NumOperands = 1;

                Status = AcpiDsCreateOperands (
                    WalkState, Op->Common.Value.Arg);
                if (ACPI_SUCCESS (Status))
                {
                    Status = AcpiExCreateMethod (Op->Named.Data,
                        Op->Named.Length, WalkState);
                }

                WalkState->Operands[0] = NULL;
                WalkState->NumOperands = 0;

                if (ACPI_FAILURE (Status))
                {
                    return_ACPI_STATUS (Status);
                }
            }
        }
    }

    /* Pop the scope stack (only if loading a table) */

    if (!WalkState->MethodNode &&
        Op->Common.AmlOpcode != AML_EXTERNAL_OP &&
        AcpiNsOpensScope (ObjectType))
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "(%s): Popping scope for Op %p\n",
            AcpiUtGetTypeName (ObjectType), Op));

        Status = AcpiDsScopeStackPop (WalkState);
    }

    return_ACPI_STATUS (Status);
}
