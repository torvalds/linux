/*******************************************************************************
 *
 * Module Name: dbmethod - Debug commands for control methods
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
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acdebug.h>
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acpredef.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dbmethod")

/* Local prototypes */

static ACPI_STATUS
AcpiDbWalkForExecute (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbSetMethodBreakpoint
 *
 * PARAMETERS:  Location            - AML offset of breakpoint
 *              WalkState           - Current walk info
 *              Op                  - Current Op (from parse walk)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set a breakpoint in a control method at the specified
 *              AML offset
 *
 ******************************************************************************/

void
AcpiDbSetMethodBreakpoint (
    char                    *Location,
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    UINT32                  Address;
    UINT32                  AmlOffset;


    if (!Op)
    {
        AcpiOsPrintf ("There is no method currently executing\n");
        return;
    }

    /* Get and verify the breakpoint address */

    Address = strtoul (Location, NULL, 16);
    AmlOffset = (UINT32) ACPI_PTR_DIFF (Op->Common.Aml,
        WalkState->ParserState.AmlStart);
    if (Address <= AmlOffset)
    {
        AcpiOsPrintf ("Breakpoint %X is beyond current address %X\n",
            Address, AmlOffset);
    }

    /* Save breakpoint in current walk */

    WalkState->UserBreakpoint = Address;
    AcpiOsPrintf ("Breakpoint set at AML offset %X\n", Address);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbSetMethodCallBreakpoint
 *
 * PARAMETERS:  Op                  - Current Op (from parse walk)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set a breakpoint in a control method at the specified
 *              AML offset
 *
 ******************************************************************************/

void
AcpiDbSetMethodCallBreakpoint (
    ACPI_PARSE_OBJECT       *Op)
{


    if (!Op)
    {
        AcpiOsPrintf ("There is no method currently executing\n");
        return;
    }

    AcpiGbl_StepToNextCall = TRUE;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbSetMethodData
 *
 * PARAMETERS:  TypeArg         - L for local, A for argument
 *              IndexArg        - which one
 *              ValueArg        - Value to set.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set a local or argument for the running control method.
 *              NOTE: only object supported is Number.
 *
 ******************************************************************************/

void
AcpiDbSetMethodData (
    char                    *TypeArg,
    char                    *IndexArg,
    char                    *ValueArg)
{
    char                    Type;
    UINT32                  Index;
    UINT32                  Value;
    ACPI_WALK_STATE         *WalkState;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node;


    /* Validate TypeArg */

    AcpiUtStrupr (TypeArg);
    Type = TypeArg[0];
    if ((Type != 'L') &&
        (Type != 'A') &&
        (Type != 'N'))
    {
        AcpiOsPrintf ("Invalid SET operand: %s\n", TypeArg);
        return;
    }

    Value = strtoul (ValueArg, NULL, 16);

    if (Type == 'N')
    {
        Node = AcpiDbConvertToNode (IndexArg);
        if (!Node)
        {
            return;
        }

        if (Node->Type != ACPI_TYPE_INTEGER)
        {
            AcpiOsPrintf ("Can only set Integer nodes\n");
            return;
        }
        ObjDesc = Node->Object;
        ObjDesc->Integer.Value = Value;
        return;
    }

    /* Get the index and value */

    Index = strtoul (IndexArg, NULL, 16);

    WalkState = AcpiDsGetCurrentWalkState (AcpiGbl_CurrentWalkList);
    if (!WalkState)
    {
        AcpiOsPrintf ("There is no method currently executing\n");
        return;
    }

    /* Create and initialize the new object */

    ObjDesc = AcpiUtCreateIntegerObject ((UINT64) Value);
    if (!ObjDesc)
    {
        AcpiOsPrintf ("Could not create an internal object\n");
        return;
    }

    /* Store the new object into the target */

    switch (Type)
    {
    case 'A':

        /* Set a method argument */

        if (Index > ACPI_METHOD_MAX_ARG)
        {
            AcpiOsPrintf ("Arg%u - Invalid argument name\n",
                Index);
            goto Cleanup;
        }

        Status = AcpiDsStoreObjectToLocal (ACPI_REFCLASS_ARG,
            Index, ObjDesc, WalkState);
        if (ACPI_FAILURE (Status))
        {
            goto Cleanup;
        }

        ObjDesc = WalkState->Arguments[Index].Object;

        AcpiOsPrintf ("Arg%u: ", Index);
        AcpiDbDisplayInternalObject (ObjDesc, WalkState);
        break;

    case 'L':

        /* Set a method local */

        if (Index > ACPI_METHOD_MAX_LOCAL)
        {
            AcpiOsPrintf ("Local%u - Invalid local variable name\n",
                Index);
            goto Cleanup;
        }

        Status = AcpiDsStoreObjectToLocal (ACPI_REFCLASS_LOCAL,
            Index, ObjDesc, WalkState);
        if (ACPI_FAILURE (Status))
        {
            goto Cleanup;
        }

        ObjDesc = WalkState->LocalVariables[Index].Object;

        AcpiOsPrintf ("Local%u: ", Index);
        AcpiDbDisplayInternalObject (ObjDesc, WalkState);
        break;

    default:

        break;
    }

Cleanup:
    AcpiUtRemoveReference (ObjDesc);
}


#ifdef ACPI_DISASSEMBLER
/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisassembleAml
 *
 * PARAMETERS:  Statements          - Number of statements to disassemble
 *              Op                  - Current Op (from parse walk)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display disassembled AML (ASL) starting from Op for the number
 *              of statements specified.
 *
 ******************************************************************************/

void
AcpiDbDisassembleAml (
    char                    *Statements,
    ACPI_PARSE_OBJECT       *Op)
{
    UINT32                  NumStatements = 8;


    if (!Op)
    {
        AcpiOsPrintf ("There is no method currently executing\n");
        return;
    }

    if (Statements)
    {
        NumStatements = strtoul (Statements, NULL, 0);
    }

    AcpiDmDisassemble (NULL, Op, NumStatements);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisassembleMethod
 *
 * PARAMETERS:  Name            - Name of control method
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display disassembled AML (ASL) starting from Op for the number
 *              of statements specified.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbDisassembleMethod (
    char                    *Name)
{
    ACPI_STATUS             Status;
    ACPI_PARSE_OBJECT       *Op;
    ACPI_WALK_STATE         *WalkState;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_NAMESPACE_NODE     *Method;


    Method = AcpiDbConvertToNode (Name);
    if (!Method)
    {
        return (AE_BAD_PARAMETER);
    }

    if (Method->Type != ACPI_TYPE_METHOD)
    {
        ACPI_ERROR ((AE_INFO, "%s (%s): Object must be a control method",
            Name, AcpiUtGetTypeName (Method->Type)));
        return (AE_BAD_PARAMETER);
    }

    ObjDesc = Method->Object;

    Op = AcpiPsCreateScopeOp (ObjDesc->Method.AmlStart);
    if (!Op)
    {
        return (AE_NO_MEMORY);
    }

    /* Create and initialize a new walk state */

    WalkState = AcpiDsCreateWalkState (0, Op, NULL, NULL);
    if (!WalkState)
    {
        return (AE_NO_MEMORY);
    }

    Status = AcpiDsInitAmlWalk (WalkState, Op, NULL,
        ObjDesc->Method.AmlStart,
        ObjDesc->Method.AmlLength, NULL, ACPI_IMODE_LOAD_PASS1);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    Status = AcpiUtAllocateOwnerId (&ObjDesc->Method.OwnerId);
    WalkState->OwnerId = ObjDesc->Method.OwnerId;

    /* Push start scope on scope stack and make it current */

    Status = AcpiDsScopeStackPush (Method,
        Method->Type, WalkState);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* Parse the entire method AML including deferred operators */

    WalkState->ParseFlags &= ~ACPI_PARSE_DELETE_TREE;
    WalkState->ParseFlags |= ACPI_PARSE_DISASSEMBLE;

    Status = AcpiPsParseAml (WalkState);
    (void) AcpiDmParseDeferredOps (Op);

    /* Now we can disassemble the method */

    AcpiGbl_DmOpt_Verbose = FALSE;
    AcpiDmDisassemble (NULL, Op, 0);
    AcpiGbl_DmOpt_Verbose = TRUE;

    AcpiPsDeleteParseTree (Op);

    /* Method cleanup */

    AcpiNsDeleteNamespaceSubtree (Method);
    AcpiNsDeleteNamespaceByOwner (ObjDesc->Method.OwnerId);
    AcpiUtReleaseOwnerId (&ObjDesc->Method.OwnerId);
    return (AE_OK);
}
#endif


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbWalkForExecute
 *
 * PARAMETERS:  Callback from WalkNamespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Batch execution module. Currently only executes predefined
 *              ACPI names.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbWalkForExecute (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;
    ACPI_DB_EXECUTE_WALK    *Info = (ACPI_DB_EXECUTE_WALK *) Context;
    ACPI_BUFFER             ReturnObj;
    ACPI_STATUS             Status;
    char                    *Pathname;
    UINT32                  i;
    ACPI_DEVICE_INFO        *ObjInfo;
    ACPI_OBJECT_LIST        ParamObjects;
    ACPI_OBJECT             Params[ACPI_METHOD_NUM_ARGS];
    const ACPI_PREDEFINED_INFO *Predefined;


    Predefined = AcpiUtMatchPredefinedMethod (Node->Name.Ascii);
    if (!Predefined)
    {
        return (AE_OK);
    }

    if (Node->Type == ACPI_TYPE_LOCAL_SCOPE)
    {
        return (AE_OK);
    }

    Pathname = AcpiNsGetExternalPathname (Node);
    if (!Pathname)
    {
        return (AE_OK);
    }

    /* Get the object info for number of method parameters */

    Status = AcpiGetObjectInfo (ObjHandle, &ObjInfo);
    if (ACPI_FAILURE (Status))
    {
        ACPI_FREE (Pathname);
        return (Status);
    }

    ParamObjects.Pointer = NULL;
    ParamObjects.Count   = 0;

    if (ObjInfo->Type == ACPI_TYPE_METHOD)
    {
        /* Setup default parameters */

        for (i = 0; i < ObjInfo->ParamCount; i++)
        {
            Params[i].Type           = ACPI_TYPE_INTEGER;
            Params[i].Integer.Value  = 1;
        }

        ParamObjects.Pointer     = Params;
        ParamObjects.Count       = ObjInfo->ParamCount;
    }

    ACPI_FREE (ObjInfo);
    ReturnObj.Pointer = NULL;
    ReturnObj.Length = ACPI_ALLOCATE_BUFFER;

    /* Do the actual method execution */

    AcpiGbl_MethodExecuting = TRUE;

    Status = AcpiEvaluateObject (Node, NULL, &ParamObjects, &ReturnObj);

    AcpiOsPrintf ("%-32s returned %s\n", Pathname, AcpiFormatException (Status));
    AcpiGbl_MethodExecuting = FALSE;
    ACPI_FREE (Pathname);

    /* Ignore status from method execution */

    Status = AE_OK;

    /* Update count, check if we have executed enough methods */

    Info->Count++;
    if (Info->Count >= Info->MaxCount)
    {
        Status = AE_CTRL_TERMINATE;
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbEvaluatePredefinedNames
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Namespace batch execution. Execute predefined names in the
 *              namespace, up to the max count, if specified.
 *
 ******************************************************************************/

void
AcpiDbEvaluatePredefinedNames (
    void)
{
    ACPI_DB_EXECUTE_WALK    Info;


    Info.Count = 0;
    Info.MaxCount = ACPI_UINT32_MAX;

    /* Search all nodes in namespace */

    (void) AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
                AcpiDbWalkForExecute, NULL, (void *) &Info, NULL);

    AcpiOsPrintf ("Evaluated %u predefined names in the namespace\n", Info.Count);
}
