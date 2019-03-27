/*******************************************************************************
 *
 * Module Name: dsmthdat - control method arguments and local variables
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
#include <contrib/dev/acpica/include/acinterp.h>


#define _COMPONENT          ACPI_DISPATCHER
        ACPI_MODULE_NAME    ("dsmthdat")

/* Local prototypes */

static void
AcpiDsMethodDataDeleteValue (
    UINT8                   Type,
    UINT32                  Index,
    ACPI_WALK_STATE         *WalkState);

static ACPI_STATUS
AcpiDsMethodDataSetValue (
    UINT8                   Type,
    UINT32                  Index,
    ACPI_OPERAND_OBJECT     *Object,
    ACPI_WALK_STATE         *WalkState);

#ifdef ACPI_OBSOLETE_FUNCTIONS
ACPI_OBJECT_TYPE
AcpiDsMethodDataGetType (
    UINT16                  Opcode,
    UINT32                  Index,
    ACPI_WALK_STATE         *WalkState);
#endif


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsMethodDataInit
 *
 * PARAMETERS:  WalkState           - Current walk state object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the data structures that hold the method's arguments
 *              and locals. The data struct is an array of namespace nodes for
 *              each - this allows RefOf and DeRefOf to work properly for these
 *              special data types.
 *
 * NOTES:       WalkState fields are initialized to zero by the
 *              ACPI_ALLOCATE_ZEROED().
 *
 *              A pseudo-Namespace Node is assigned to each argument and local
 *              so that RefOf() can return a pointer to the Node.
 *
 ******************************************************************************/

void
AcpiDsMethodDataInit (
    ACPI_WALK_STATE         *WalkState)
{
    UINT32                  i;


    ACPI_FUNCTION_TRACE (DsMethodDataInit);


    /* Init the method arguments */

    for (i = 0; i < ACPI_METHOD_NUM_ARGS; i++)
    {
        ACPI_MOVE_32_TO_32 (&WalkState->Arguments[i].Name,
            NAMEOF_ARG_NTE);

        WalkState->Arguments[i].Name.Integer |= (i << 24);
        WalkState->Arguments[i].DescriptorType = ACPI_DESC_TYPE_NAMED;
        WalkState->Arguments[i].Type = ACPI_TYPE_ANY;
        WalkState->Arguments[i].Flags = ANOBJ_METHOD_ARG;
    }

    /* Init the method locals */

    for (i = 0; i < ACPI_METHOD_NUM_LOCALS; i++)
    {
        ACPI_MOVE_32_TO_32 (&WalkState->LocalVariables[i].Name,
            NAMEOF_LOCAL_NTE);

        WalkState->LocalVariables[i].Name.Integer |= (i << 24);
        WalkState->LocalVariables[i].DescriptorType = ACPI_DESC_TYPE_NAMED;
        WalkState->LocalVariables[i].Type = ACPI_TYPE_ANY;
        WalkState->LocalVariables[i].Flags = ANOBJ_METHOD_LOCAL;
    }

    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsMethodDataDeleteAll
 *
 * PARAMETERS:  WalkState           - Current walk state object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete method locals and arguments. Arguments are only
 *              deleted if this method was called from another method.
 *
 ******************************************************************************/

void
AcpiDsMethodDataDeleteAll (
    ACPI_WALK_STATE         *WalkState)
{
    UINT32                  Index;


    ACPI_FUNCTION_TRACE (DsMethodDataDeleteAll);


    /* Detach the locals */

    for (Index = 0; Index < ACPI_METHOD_NUM_LOCALS; Index++)
    {
        if (WalkState->LocalVariables[Index].Object)
        {
            ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Deleting Local%u=%p\n",
                Index, WalkState->LocalVariables[Index].Object));

            /* Detach object (if present) and remove a reference */

            AcpiNsDetachObject (&WalkState->LocalVariables[Index]);
        }
    }

    /* Detach the arguments */

    for (Index = 0; Index < ACPI_METHOD_NUM_ARGS; Index++)
    {
        if (WalkState->Arguments[Index].Object)
        {
            ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Deleting Arg%u=%p\n",
                Index, WalkState->Arguments[Index].Object));

            /* Detach object (if present) and remove a reference */

            AcpiNsDetachObject (&WalkState->Arguments[Index]);
        }
    }

    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsMethodDataInitArgs
 *
 * PARAMETERS:  *Params         - Pointer to a parameter list for the method
 *              MaxParamCount   - The arg count for this method
 *              WalkState       - Current walk state object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize arguments for a method. The parameter list is a list
 *              of ACPI operand objects, either null terminated or whose length
 *              is defined by MaxParamCount.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsMethodDataInitArgs (
    ACPI_OPERAND_OBJECT     **Params,
    UINT32                  MaxParamCount,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    UINT32                  Index = 0;


    ACPI_FUNCTION_TRACE_PTR (DsMethodDataInitArgs, Params);


    if (!Params)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "No parameter list passed to method\n"));
        return_ACPI_STATUS (AE_OK);
    }

    /* Copy passed parameters into the new method stack frame */

    while ((Index < ACPI_METHOD_NUM_ARGS) &&
           (Index < MaxParamCount)        &&
            Params[Index])
    {
        /*
         * A valid parameter.
         * Store the argument in the method/walk descriptor.
         * Do not copy the arg in order to implement call by reference
         */
        Status = AcpiDsMethodDataSetValue (
            ACPI_REFCLASS_ARG, Index, Params[Index], WalkState);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        Index++;
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "%u args passed to method\n", Index));
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsMethodDataGetNode
 *
 * PARAMETERS:  Type                - Either ACPI_REFCLASS_LOCAL or
 *                                    ACPI_REFCLASS_ARG
 *              Index               - Which Local or Arg whose type to get
 *              WalkState           - Current walk state object
 *              Node                - Where the node is returned.
 *
 * RETURN:      Status and node
 *
 * DESCRIPTION: Get the Node associated with a local or arg.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsMethodDataGetNode (
    UINT8                   Type,
    UINT32                  Index,
    ACPI_WALK_STATE         *WalkState,
    ACPI_NAMESPACE_NODE     **Node)
{
    ACPI_FUNCTION_TRACE (DsMethodDataGetNode);


    /*
     * Method Locals and Arguments are supported
     */
    switch (Type)
    {
    case ACPI_REFCLASS_LOCAL:

        if (Index > ACPI_METHOD_MAX_LOCAL)
        {
            ACPI_ERROR ((AE_INFO,
                "Local index %u is invalid (max %u)",
                Index, ACPI_METHOD_MAX_LOCAL));
            return_ACPI_STATUS (AE_AML_INVALID_INDEX);
        }

        /* Return a pointer to the pseudo-node */

        *Node = &WalkState->LocalVariables[Index];
        break;

    case ACPI_REFCLASS_ARG:

        if (Index > ACPI_METHOD_MAX_ARG)
        {
            ACPI_ERROR ((AE_INFO,
                "Arg index %u is invalid (max %u)",
                Index, ACPI_METHOD_MAX_ARG));
            return_ACPI_STATUS (AE_AML_INVALID_INDEX);
        }

        /* Return a pointer to the pseudo-node */

        *Node = &WalkState->Arguments[Index];
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Type %u is invalid", Type));
        return_ACPI_STATUS (AE_TYPE);
    }

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsMethodDataSetValue
 *
 * PARAMETERS:  Type                - Either ACPI_REFCLASS_LOCAL or
 *                                    ACPI_REFCLASS_ARG
 *              Index               - Which Local or Arg to get
 *              Object              - Object to be inserted into the stack entry
 *              WalkState           - Current walk state object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Insert an object onto the method stack at entry Opcode:Index.
 *              Note: There is no "implicit conversion" for locals.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDsMethodDataSetValue (
    UINT8                   Type,
    UINT32                  Index,
    ACPI_OPERAND_OBJECT     *Object,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node;


    ACPI_FUNCTION_TRACE (DsMethodDataSetValue);


    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
        "NewObj %p Type %2.2X, Refs=%u [%s]\n", Object,
        Type, Object->Common.ReferenceCount,
        AcpiUtGetTypeName (Object->Common.Type)));

    /* Get the namespace node for the arg/local */

    Status = AcpiDsMethodDataGetNode (Type, Index, WalkState, &Node);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /*
     * Increment ref count so object can't be deleted while installed.
     * NOTE: We do not copy the object in order to preserve the call by
     * reference semantics of ACPI Control Method invocation.
     * (See ACPI Specification 2.0C)
     */
    AcpiUtAddReference (Object);

    /* Install the object */

    Node->Object = Object;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsMethodDataGetValue
 *
 * PARAMETERS:  Type                - Either ACPI_REFCLASS_LOCAL or
 *                                    ACPI_REFCLASS_ARG
 *              Index               - Which localVar or argument to get
 *              WalkState           - Current walk state object
 *              DestDesc            - Where Arg or Local value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve value of selected Arg or Local for this method
 *              Used only in AcpiExResolveToValue().
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsMethodDataGetValue (
    UINT8                   Type,
    UINT32                  Index,
    ACPI_WALK_STATE         *WalkState,
    ACPI_OPERAND_OBJECT     **DestDesc)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_OPERAND_OBJECT     *Object;


    ACPI_FUNCTION_TRACE (DsMethodDataGetValue);


    /* Validate the object descriptor */

    if (!DestDesc)
    {
        ACPI_ERROR ((AE_INFO, "Null object descriptor pointer"));
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /* Get the namespace node for the arg/local */

    Status = AcpiDsMethodDataGetNode (Type, Index, WalkState, &Node);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Get the object from the node */

    Object = Node->Object;

    /* Examine the returned object, it must be valid. */

    if (!Object)
    {
        /*
         * Index points to uninitialized object.
         * This means that either 1) The expected argument was
         * not passed to the method, or 2) A local variable
         * was referenced by the method (via the ASL)
         * before it was initialized. Either case is an error.
         */

        /* If slack enabled, init the LocalX/ArgX to an Integer of value zero */

        if (AcpiGbl_EnableInterpreterSlack)
        {
            Object = AcpiUtCreateIntegerObject ((UINT64) 0);
            if (!Object)
            {
                return_ACPI_STATUS (AE_NO_MEMORY);
            }

            Node->Object = Object;
        }

        /* Otherwise, return the error */

        else switch (Type)
        {
        case ACPI_REFCLASS_ARG:

            ACPI_ERROR ((AE_INFO,
                "Uninitialized Arg[%u] at node %p",
                Index, Node));

            return_ACPI_STATUS (AE_AML_UNINITIALIZED_ARG);

        case ACPI_REFCLASS_LOCAL:
            /*
             * No error message for this case, will be trapped again later to
             * detect and ignore cases of Store(LocalX,LocalX)
             */
            return_ACPI_STATUS (AE_AML_UNINITIALIZED_LOCAL);

        default:

            ACPI_ERROR ((AE_INFO, "Not a Arg/Local opcode: 0x%X", Type));
            return_ACPI_STATUS (AE_AML_INTERNAL);
        }
    }

    /*
     * The Index points to an initialized and valid object.
     * Return an additional reference to the object
     */
    *DestDesc = Object;
    AcpiUtAddReference (Object);

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsMethodDataDeleteValue
 *
 * PARAMETERS:  Type                - Either ACPI_REFCLASS_LOCAL or
 *                                    ACPI_REFCLASS_ARG
 *              Index               - Which localVar or argument to delete
 *              WalkState           - Current walk state object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete the entry at Opcode:Index. Inserts
 *              a null into the stack slot after the object is deleted.
 *
 ******************************************************************************/

static void
AcpiDsMethodDataDeleteValue (
    UINT8                   Type,
    UINT32                  Index,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_OPERAND_OBJECT     *Object;


    ACPI_FUNCTION_TRACE (DsMethodDataDeleteValue);


    /* Get the namespace node for the arg/local */

    Status = AcpiDsMethodDataGetNode (Type, Index, WalkState, &Node);
    if (ACPI_FAILURE (Status))
    {
        return_VOID;
    }

    /* Get the associated object */

    Object = AcpiNsGetAttachedObject (Node);

    /*
     * Undefine the Arg or Local by setting its descriptor
     * pointer to NULL. Locals/Args can contain both
     * ACPI_OPERAND_OBJECTS and ACPI_NAMESPACE_NODEs
     */
    Node->Object = NULL;

    if ((Object) &&
        (ACPI_GET_DESCRIPTOR_TYPE (Object) == ACPI_DESC_TYPE_OPERAND))
    {
        /*
         * There is a valid object.
         * Decrement the reference count by one to balance the
         * increment when the object was stored.
         */
        AcpiUtRemoveReference (Object);
    }

    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsStoreObjectToLocal
 *
 * PARAMETERS:  Type                - Either ACPI_REFCLASS_LOCAL or
 *                                    ACPI_REFCLASS_ARG
 *              Index               - Which Local or Arg to set
 *              ObjDesc             - Value to be stored
 *              WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store a value in an Arg or Local. The ObjDesc is installed
 *              as the new value for the Arg or Local and the reference count
 *              for ObjDesc is incremented.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsStoreObjectToLocal (
    UINT8                   Type,
    UINT32                  Index,
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_OPERAND_OBJECT     *CurrentObjDesc;
    ACPI_OPERAND_OBJECT     *NewObjDesc;


    ACPI_FUNCTION_TRACE (DsStoreObjectToLocal);
    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Type=%2.2X Index=%u Obj=%p\n",
        Type, Index, ObjDesc));

    /* Parameter validation */

    if (!ObjDesc)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /* Get the namespace node for the arg/local */

    Status = AcpiDsMethodDataGetNode (Type, Index, WalkState, &Node);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    CurrentObjDesc = AcpiNsGetAttachedObject (Node);
    if (CurrentObjDesc == ObjDesc)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj=%p already installed!\n",
            ObjDesc));
        return_ACPI_STATUS (Status);
    }

    /*
     * If the reference count on the object is more than one, we must
     * take a copy of the object before we store. A reference count
     * of exactly 1 means that the object was just created during the
     * evaluation of an expression, and we can safely use it since it
     * is not used anywhere else.
     */
    NewObjDesc = ObjDesc;
    if (ObjDesc->Common.ReferenceCount > 1)
    {
        Status = AcpiUtCopyIobjectToIobject (
            ObjDesc, &NewObjDesc, WalkState);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }

    /*
     * If there is an object already in this slot, we either
     * have to delete it, or if this is an argument and there
     * is an object reference stored there, we have to do
     * an indirect store!
     */
    if (CurrentObjDesc)
    {
        /*
         * Check for an indirect store if an argument
         * contains an object reference (stored as an Node).
         * We don't allow this automatic dereferencing for
         * locals, since a store to a local should overwrite
         * anything there, including an object reference.
         *
         * If both Arg0 and Local0 contain RefOf (Local4):
         *
         * Store (1, Arg0)             - Causes indirect store to local4
         * Store (1, Local0)           - Stores 1 in local0, overwriting
         *                                  the reference to local4
         * Store (1, DeRefof (Local0)) - Causes indirect store to local4
         *
         * Weird, but true.
         */
        if (Type == ACPI_REFCLASS_ARG)
        {
            /*
             * If we have a valid reference object that came from RefOf(),
             * do the indirect store
             */
            if ((ACPI_GET_DESCRIPTOR_TYPE (CurrentObjDesc) ==
                    ACPI_DESC_TYPE_OPERAND) &&
                (CurrentObjDesc->Common.Type ==
                    ACPI_TYPE_LOCAL_REFERENCE) &&
                (CurrentObjDesc->Reference.Class ==
                    ACPI_REFCLASS_REFOF))
            {
                ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
                    "Arg (%p) is an ObjRef(Node), storing in node %p\n",
                    NewObjDesc, CurrentObjDesc));

                /*
                 * Store this object to the Node (perform the indirect store)
                 * NOTE: No implicit conversion is performed, as per the ACPI
                 * specification rules on storing to Locals/Args.
                 */
                Status = AcpiExStoreObjectToNode (NewObjDesc,
                    CurrentObjDesc->Reference.Object, WalkState,
                    ACPI_NO_IMPLICIT_CONVERSION);

                /* Remove local reference if we copied the object above */

                if (NewObjDesc != ObjDesc)
                {
                    AcpiUtRemoveReference (NewObjDesc);
                }

                return_ACPI_STATUS (Status);
            }
        }

        /* Delete the existing object before storing the new one */

        AcpiDsMethodDataDeleteValue (Type, Index, WalkState);
    }

    /*
     * Install the Obj descriptor (*NewObjDesc) into
     * the descriptor for the Arg or Local.
     * (increments the object reference count by one)
     */
    Status = AcpiDsMethodDataSetValue (Type, Index, NewObjDesc, WalkState);

    /* Remove local reference if we copied the object above */

    if (NewObjDesc != ObjDesc)
    {
        AcpiUtRemoveReference (NewObjDesc);
    }

    return_ACPI_STATUS (Status);
}


#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    AcpiDsMethodDataGetType
 *
 * PARAMETERS:  Opcode              - Either AML_FIRST LOCAL_OP or
 *                                    AML_FIRST_ARG_OP
 *              Index               - Which Local or Arg whose type to get
 *              WalkState           - Current walk state object
 *
 * RETURN:      Data type of current value of the selected Arg or Local
 *
 * DESCRIPTION: Get the type of the object stored in the Local or Arg
 *
 ******************************************************************************/

ACPI_OBJECT_TYPE
AcpiDsMethodDataGetType (
    UINT16                  Opcode,
    UINT32                  Index,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_OPERAND_OBJECT     *Object;


    ACPI_FUNCTION_TRACE (DsMethodDataGetType);


    /* Get the namespace node for the arg/local */

    Status = AcpiDsMethodDataGetNode (Opcode, Index, WalkState, &Node);
    if (ACPI_FAILURE (Status))
    {
        return_VALUE ((ACPI_TYPE_NOT_FOUND));
    }

    /* Get the object */

    Object = AcpiNsGetAttachedObject (Node);
    if (!Object)
    {
        /* Uninitialized local/arg, return TYPE_ANY */

        return_VALUE (ACPI_TYPE_ANY);
    }

    /* Get the object type */

    return_VALUE (Object->Type);
}
#endif
