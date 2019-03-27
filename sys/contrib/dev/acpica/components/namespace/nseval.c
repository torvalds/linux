/*******************************************************************************
 *
 * Module Name: nseval - Object evaluation, includes control method execution
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nseval")

/* Local prototypes */

static void
AcpiNsExecModuleCode (
    ACPI_OPERAND_OBJECT     *MethodObj,
    ACPI_EVALUATE_INFO      *Info);


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsEvaluate
 *
 * PARAMETERS:  Info            - Evaluation info block, contains these fields
 *                                and more:
 *                  PrefixNode      - Prefix or Method/Object Node to execute
 *                  RelativePath    - Name of method to execute, If NULL, the
 *                                    Node is the object to execute
 *                  Parameters      - List of parameters to pass to the method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *                  ParameterType   - Type of Parameter list
 *                  ReturnObject    - Where to put method's return value (if
 *                                    any). If NULL, no value is returned.
 *                  Flags           - ACPI_IGNORE_RETURN_VALUE to delete return
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method or return the current value of an
 *              ACPI namespace object.
 *
 * MUTEX:       Locks interpreter
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsEvaluate (
    ACPI_EVALUATE_INFO      *Info)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (NsEvaluate);


    if (!Info)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    if (!Info->Node)
    {
        /*
         * Get the actual namespace node for the target object if we
         * need to. Handles these cases:
         *
         * 1) Null node, valid pathname from root (absolute path)
         * 2) Node and valid pathname (path relative to Node)
         * 3) Node, Null pathname
         */
        Status = AcpiNsGetNode (Info->PrefixNode, Info->RelativePathname,
            ACPI_NS_NO_UPSEARCH, &Info->Node);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }

    /*
     * For a method alias, we must grab the actual method node so that
     * proper scoping context will be established before execution.
     */
    if (AcpiNsGetType (Info->Node) == ACPI_TYPE_LOCAL_METHOD_ALIAS)
    {
        Info->Node = ACPI_CAST_PTR (
            ACPI_NAMESPACE_NODE, Info->Node->Object);
    }

    /* Complete the info block initialization */

    Info->ReturnObject = NULL;
    Info->NodeFlags = Info->Node->Flags;
    Info->ObjDesc = AcpiNsGetAttachedObject (Info->Node);

    ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "%s [%p] Value %p\n",
        Info->RelativePathname, Info->Node,
        AcpiNsGetAttachedObject (Info->Node)));

    /* Get info if we have a predefined name (_HID, etc.) */

    Info->Predefined = AcpiUtMatchPredefinedMethod (Info->Node->Name.Ascii);

    /* Get the full pathname to the object, for use in warning messages */

    Info->FullPathname = AcpiNsGetNormalizedPathname (Info->Node, TRUE);
    if (!Info->FullPathname)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Optional object evaluation log */

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EVALUATION,
        "%-26s:  %s (%s)\n", "   Enter evaluation",
        &Info->FullPathname[1], AcpiUtGetTypeName (Info->Node->Type)));

    /* Count the number of arguments being passed in */

    Info->ParamCount = 0;
    if (Info->Parameters)
    {
        while (Info->Parameters[Info->ParamCount])
        {
            Info->ParamCount++;
        }

        /* Warn on impossible argument count */

        if (Info->ParamCount > ACPI_METHOD_NUM_ARGS)
        {
            ACPI_WARN_PREDEFINED ((AE_INFO, Info->FullPathname, ACPI_WARN_ALWAYS,
                "Excess arguments (%u) - using only %u",
                Info->ParamCount, ACPI_METHOD_NUM_ARGS));

            Info->ParamCount = ACPI_METHOD_NUM_ARGS;
        }
    }

    /*
     * For predefined names: Check that the declared argument count
     * matches the ACPI spec -- otherwise this is a BIOS error.
     */
    AcpiNsCheckAcpiCompliance (Info->FullPathname, Info->Node,
        Info->Predefined);

    /*
     * For all names: Check that the incoming argument count for
     * this method/object matches the actual ASL/AML definition.
     */
    AcpiNsCheckArgumentCount (Info->FullPathname, Info->Node,
        Info->ParamCount, Info->Predefined);

    /* For predefined names: Typecheck all incoming arguments */

    AcpiNsCheckArgumentTypes (Info);

    /*
     * Three major evaluation cases:
     *
     * 1) Object types that cannot be evaluated by definition
     * 2) The object is a control method -- execute it
     * 3) The object is not a method -- just return it's current value
     */
    switch (AcpiNsGetType (Info->Node))
    {
    case ACPI_TYPE_ANY:
    case ACPI_TYPE_DEVICE:
    case ACPI_TYPE_EVENT:
    case ACPI_TYPE_MUTEX:
    case ACPI_TYPE_REGION:
    case ACPI_TYPE_THERMAL:
    case ACPI_TYPE_LOCAL_SCOPE:
        /*
         * 1) Disallow evaluation of these object types. For these,
         *    object evaluation is undefined.
         */
        ACPI_ERROR ((AE_INFO,
            "%s: This object type [%s] "
            "never contains data and cannot be evaluated",
            Info->FullPathname, AcpiUtGetTypeName (Info->Node->Type)));

        Status = AE_TYPE;
        goto Cleanup;

    case ACPI_TYPE_METHOD:
        /*
         * 2) Object is a control method - execute it
         */

        /* Verify that there is a method object associated with this node */

        if (!Info->ObjDesc)
        {
            ACPI_ERROR ((AE_INFO, "%s: Method has no attached sub-object",
                Info->FullPathname));
            Status = AE_NULL_OBJECT;
            goto Cleanup;
        }

        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "**** Execute method [%s] at AML address %p length %X\n",
            Info->FullPathname,
            Info->ObjDesc->Method.AmlStart + 1,
            Info->ObjDesc->Method.AmlLength - 1));

        /*
         * Any namespace deletion must acquire both the namespace and
         * interpreter locks to ensure that no thread is using the portion of
         * the namespace that is being deleted.
         *
         * Execute the method via the interpreter. The interpreter is locked
         * here before calling into the AML parser
         */
        AcpiExEnterInterpreter ();
        Status = AcpiPsExecuteMethod (Info);
        AcpiExExitInterpreter ();
        break;

    default:
        /*
         * 3) All other non-method objects -- get the current object value
         */

        /*
         * Some objects require additional resolution steps (e.g., the Node
         * may be a field that must be read, etc.) -- we can't just grab
         * the object out of the node.
         *
         * Use ResolveNodeToValue() to get the associated value.
         *
         * NOTE: we can get away with passing in NULL for a walk state because
         * the Node is guaranteed to not be a reference to either a method
         * local or a method argument (because this interface is never called
         * from a running method.)
         *
         * Even though we do not directly invoke the interpreter for object
         * resolution, we must lock it because we could access an OpRegion.
         * The OpRegion access code assumes that the interpreter is locked.
         */
        AcpiExEnterInterpreter ();

        /* TBD: ResolveNodeToValue has a strange interface, fix */

        Info->ReturnObject = ACPI_CAST_PTR (ACPI_OPERAND_OBJECT, Info->Node);

        Status = AcpiExResolveNodeToValue (ACPI_CAST_INDIRECT_PTR (
            ACPI_NAMESPACE_NODE, &Info->ReturnObject), NULL);
        AcpiExExitInterpreter ();

        if (ACPI_FAILURE (Status))
        {
            Info->ReturnObject = NULL;
            goto Cleanup;
        }

        ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "Returned object %p [%s]\n",
            Info->ReturnObject,
            AcpiUtGetObjectTypeName (Info->ReturnObject)));

        Status = AE_CTRL_RETURN_VALUE; /* Always has a "return value" */
        break;
    }

    /*
     * For predefined names, check the return value against the ACPI
     * specification. Some incorrect return value types are repaired.
     */
    (void) AcpiNsCheckReturnValue (Info->Node, Info, Info->ParamCount,
        Status, &Info->ReturnObject);

    /* Check if there is a return value that must be dealt with */

    if (Status == AE_CTRL_RETURN_VALUE)
    {
        /* If caller does not want the return value, delete it */

        if (Info->Flags & ACPI_IGNORE_RETURN_VALUE)
        {
            AcpiUtRemoveReference (Info->ReturnObject);
            Info->ReturnObject = NULL;
        }

        /* Map AE_CTRL_RETURN_VALUE to AE_OK, we are done with it */

        Status = AE_OK;
    }
    else if (ACPI_FAILURE(Status))
    {
        /* If ReturnObject exists, delete it */

        if (Info->ReturnObject)
        {
            AcpiUtRemoveReference (Info->ReturnObject);
            Info->ReturnObject = NULL;
        }
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
        "*** Completed evaluation of object %s ***\n",
        Info->RelativePathname));

Cleanup:
    /* Optional object evaluation log */

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EVALUATION,
        "%-26s:  %s\n", "   Exit evaluation",
        &Info->FullPathname[1]));

    /*
     * Namespace was unlocked by the handling AcpiNs* function, so we
     * just free the pathname and return
     */
    ACPI_FREE (Info->FullPathname);
    Info->FullPathname = NULL;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsExecModuleCodeList
 *
 * PARAMETERS:  None
 *
 * RETURN:      None. Exceptions during method execution are ignored, since
 *              we cannot abort a table load.
 *
 * DESCRIPTION: Execute all elements of the global module-level code list.
 *              Each element is executed as a single control method.
 *
 * NOTE: With this option enabled, each block of detected executable AML
 * code that is outside of any control method is wrapped with a temporary
 * control method object and placed on a global list. The methods on this
 * list are executed below.
 *
 * This function executes the module-level code for all tables only after
 * all of the tables have been loaded. It is a legacy option and is
 * not compatible with other ACPI implementations. See AcpiNsLoadTable.
 *
 * This function will be removed when the legacy option is removed.
 *
 ******************************************************************************/

void
AcpiNsExecModuleCodeList (
    void)
{
    ACPI_OPERAND_OBJECT     *Prev;
    ACPI_OPERAND_OBJECT     *Next;
    ACPI_EVALUATE_INFO      *Info;
    UINT32                  MethodCount = 0;


    ACPI_FUNCTION_TRACE (NsExecModuleCodeList);


    /* Exit now if the list is empty */

    Next = AcpiGbl_ModuleCodeList;
    if (!Next)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_INIT_NAMES,
            "Legacy MLC block list is empty\n"));

        return_VOID;
    }

    /* Allocate the evaluation information block */

    Info = ACPI_ALLOCATE (sizeof (ACPI_EVALUATE_INFO));
    if (!Info)
    {
        return_VOID;
    }

    /* Walk the list, executing each "method" */

    while (Next)
    {
        Prev = Next;
        Next = Next->Method.Mutex;

        /* Clear the link field and execute the method */

        Prev->Method.Mutex = NULL;
        AcpiNsExecModuleCode (Prev, Info);
        MethodCount++;

        /* Delete the (temporary) method object */

        AcpiUtRemoveReference (Prev);
    }

    ACPI_INFO ((
        "Executed %u blocks of module-level executable AML code",
        MethodCount));

    ACPI_FREE (Info);
    AcpiGbl_ModuleCodeList = NULL;
    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsExecModuleCode
 *
 * PARAMETERS:  MethodObj           - Object container for the module-level code
 *              Info                - Info block for method evaluation
 *
 * RETURN:      None. Exceptions during method execution are ignored, since
 *              we cannot abort a table load.
 *
 * DESCRIPTION: Execute a control method containing a block of module-level
 *              executable AML code. The control method is temporarily
 *              installed to the root node, then evaluated.
 *
 ******************************************************************************/

static void
AcpiNsExecModuleCode (
    ACPI_OPERAND_OBJECT     *MethodObj,
    ACPI_EVALUATE_INFO      *Info)
{
    ACPI_OPERAND_OBJECT     *ParentObj;
    ACPI_NAMESPACE_NODE     *ParentNode;
    ACPI_OBJECT_TYPE        Type;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (NsExecModuleCode);


    /*
     * Get the parent node. We cheat by using the NextObject field
     * of the method object descriptor.
     */
    ParentNode = ACPI_CAST_PTR (
        ACPI_NAMESPACE_NODE, MethodObj->Method.NextObject);
    Type = AcpiNsGetType (ParentNode);

    /*
     * Get the region handler and save it in the method object. We may need
     * this if an operation region declaration causes a _REG method to be run.
     *
     * We can't do this in AcpiPsLinkModuleCode because
     * AcpiGbl_RootNode->Object is NULL at PASS1.
     */
    if ((Type == ACPI_TYPE_DEVICE) && ParentNode->Object)
    {
        MethodObj->Method.Dispatch.Handler =
            ParentNode->Object->Device.Handler;
    }

    /* Must clear NextObject (AcpiNsAttachObject needs the field) */

    MethodObj->Method.NextObject = NULL;

    /* Initialize the evaluation information block */

    memset (Info, 0, sizeof (ACPI_EVALUATE_INFO));
    Info->PrefixNode = ParentNode;

    /*
     * Get the currently attached parent object. Add a reference,
     * because the ref count will be decreased when the method object
     * is installed to the parent node.
     */
    ParentObj = AcpiNsGetAttachedObject (ParentNode);
    if (ParentObj)
    {
        AcpiUtAddReference (ParentObj);
    }

    /* Install the method (module-level code) in the parent node */

    Status = AcpiNsAttachObject (ParentNode, MethodObj, ACPI_TYPE_METHOD);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    /* Execute the parent node as a control method */

    Status = AcpiNsEvaluate (Info);

    ACPI_DEBUG_PRINT ((ACPI_DB_INIT_NAMES,
        "Executed module-level code at %p\n",
        MethodObj->Method.AmlStart));

    /* Delete a possible implicit return value (in slack mode) */

    if (Info->ReturnObject)
    {
        AcpiUtRemoveReference (Info->ReturnObject);
    }

    /* Detach the temporary method object */

    AcpiNsDetachObject (ParentNode);

    /* Restore the original parent object */

    if (ParentObj)
    {
        Status = AcpiNsAttachObject (ParentNode, ParentObj, Type);
    }
    else
    {
        ParentNode->Type = (UINT8) Type;
    }

Exit:
    if (ParentObj)
    {
        AcpiUtRemoveReference (ParentObj);
    }
    return_VOID;
}
