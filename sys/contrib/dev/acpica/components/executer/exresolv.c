/******************************************************************************
 *
 * Module Name: exresolv - AML Interpreter object resolution
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
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exresolv")

/* Local prototypes */

static ACPI_STATUS
AcpiExResolveObjectToValue (
    ACPI_OPERAND_OBJECT     **StackPtr,
    ACPI_WALK_STATE         *WalkState);


/*******************************************************************************
 *
 * FUNCTION:    AcpiExResolveToValue
 *
 * PARAMETERS:  **StackPtr          - Points to entry on ObjStack, which can
 *                                    be either an (ACPI_OPERAND_OBJECT *)
 *                                    or an ACPI_HANDLE.
 *              WalkState           - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert Reference objects to values
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExResolveToValue (
    ACPI_OPERAND_OBJECT     **StackPtr,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE_PTR (ExResolveToValue, StackPtr);


    if (!StackPtr || !*StackPtr)
    {
        ACPI_ERROR ((AE_INFO, "Internal - null pointer"));
        return_ACPI_STATUS (AE_AML_NO_OPERAND);
    }

    /*
     * The entity pointed to by the StackPtr can be either
     * 1) A valid ACPI_OPERAND_OBJECT, or
     * 2) A ACPI_NAMESPACE_NODE (NamedObj)
     */
    if (ACPI_GET_DESCRIPTOR_TYPE (*StackPtr) == ACPI_DESC_TYPE_OPERAND)
    {
        Status = AcpiExResolveObjectToValue (StackPtr, WalkState);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        if (!*StackPtr)
        {
            ACPI_ERROR ((AE_INFO, "Internal - null pointer"));
            return_ACPI_STATUS (AE_AML_NO_OPERAND);
        }
    }

    /*
     * Object on the stack may have changed if AcpiExResolveObjectToValue()
     * was called (i.e., we can't use an _else_ here.)
     */
    if (ACPI_GET_DESCRIPTOR_TYPE (*StackPtr) == ACPI_DESC_TYPE_NAMED)
    {
        Status = AcpiExResolveNodeToValue (
            ACPI_CAST_INDIRECT_PTR (ACPI_NAMESPACE_NODE, StackPtr),
            WalkState);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Resolved object %p\n", *StackPtr));
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExResolveObjectToValue
 *
 * PARAMETERS:  StackPtr        - Pointer to an internal object
 *              WalkState       - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve the value from an internal object. The Reference type
 *              uses the associated AML opcode to determine the value.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiExResolveObjectToValue (
    ACPI_OPERAND_OBJECT     **StackPtr,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_OPERAND_OBJECT     *StackDesc;
    ACPI_OPERAND_OBJECT     *ObjDesc = NULL;
    UINT8                   RefType;


    ACPI_FUNCTION_TRACE (ExResolveObjectToValue);


    StackDesc = *StackPtr;

    /* This is an object of type ACPI_OPERAND_OBJECT */

    switch (StackDesc->Common.Type)
    {
    case ACPI_TYPE_LOCAL_REFERENCE:

        RefType = StackDesc->Reference.Class;

        switch (RefType)
        {
        case ACPI_REFCLASS_LOCAL:
        case ACPI_REFCLASS_ARG:
            /*
             * Get the local from the method's state info
             * Note: this increments the local's object reference count
             */
            Status = AcpiDsMethodDataGetValue (RefType,
                StackDesc->Reference.Value, WalkState, &ObjDesc);
            if (ACPI_FAILURE (Status))
            {
                return_ACPI_STATUS (Status);
            }

            ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[Arg/Local %X] ValueObj is %p\n",
                StackDesc->Reference.Value, ObjDesc));

            /*
             * Now we can delete the original Reference Object and
             * replace it with the resolved value
             */
            AcpiUtRemoveReference (StackDesc);
            *StackPtr = ObjDesc;
            break;

        case ACPI_REFCLASS_INDEX:

            switch (StackDesc->Reference.TargetType)
            {
            case ACPI_TYPE_BUFFER_FIELD:

                /* Just return - do not dereference */
                break;

            case ACPI_TYPE_PACKAGE:

                /* If method call or CopyObject - do not dereference */

                if ((WalkState->Opcode == AML_INT_METHODCALL_OP) ||
                    (WalkState->Opcode == AML_COPY_OBJECT_OP))
                {
                    break;
                }

                /* Otherwise, dereference the PackageIndex to a package element */

                ObjDesc = *StackDesc->Reference.Where;
                if (ObjDesc)
                {
                    /*
                     * Valid object descriptor, copy pointer to return value
                     * (i.e., dereference the package index)
                     * Delete the ref object, increment the returned object
                     */
                    AcpiUtAddReference (ObjDesc);
                    *StackPtr = ObjDesc;
                }
                else
                {
                    /*
                     * A NULL object descriptor means an uninitialized element of
                     * the package, can't dereference it
                     */
                    ACPI_ERROR ((AE_INFO,
                        "Attempt to dereference an Index to "
                        "NULL package element Idx=%p",
                        StackDesc));
                    Status = AE_AML_UNINITIALIZED_ELEMENT;
                }
                break;

            default:

                /* Invalid reference object */

                ACPI_ERROR ((AE_INFO,
                    "Unknown TargetType 0x%X in Index/Reference object %p",
                    StackDesc->Reference.TargetType, StackDesc));
                Status = AE_AML_INTERNAL;
                break;
            }
            break;

        case ACPI_REFCLASS_REFOF:
        case ACPI_REFCLASS_DEBUG:
        case ACPI_REFCLASS_TABLE:

            /* Just leave the object as-is, do not dereference */

            break;

        case ACPI_REFCLASS_NAME:   /* Reference to a named object */

            /* Dereference the name */

            if ((StackDesc->Reference.Node->Type == ACPI_TYPE_DEVICE) ||
                (StackDesc->Reference.Node->Type == ACPI_TYPE_THERMAL))
            {
                /* These node types do not have 'real' subobjects */

                *StackPtr = (void *) StackDesc->Reference.Node;
            }
            else
            {
                /* Get the object pointed to by the namespace node */

                *StackPtr = (StackDesc->Reference.Node)->Object;
                AcpiUtAddReference (*StackPtr);
            }

            AcpiUtRemoveReference (StackDesc);
            break;

        default:

            ACPI_ERROR ((AE_INFO,
                "Unknown Reference type 0x%X in %p",
                RefType, StackDesc));
            Status = AE_AML_INTERNAL;
            break;
        }
        break;

    case ACPI_TYPE_BUFFER:

        Status = AcpiDsGetBufferArguments (StackDesc);
        break;

    case ACPI_TYPE_PACKAGE:

        Status = AcpiDsGetPackageArguments (StackDesc);
        break;

    case ACPI_TYPE_BUFFER_FIELD:
    case ACPI_TYPE_LOCAL_REGION_FIELD:
    case ACPI_TYPE_LOCAL_BANK_FIELD:
    case ACPI_TYPE_LOCAL_INDEX_FIELD:

        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "FieldRead SourceDesc=%p Type=%X\n",
            StackDesc, StackDesc->Common.Type));

        Status = AcpiExReadDataFromField (WalkState, StackDesc, &ObjDesc);

        /* Remove a reference to the original operand, then override */

        AcpiUtRemoveReference (*StackPtr);
        *StackPtr = (void *) ObjDesc;
        break;

    default:

        break;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExResolveMultiple
 *
 * PARAMETERS:  WalkState           - Current state (contains AML opcode)
 *              Operand             - Starting point for resolution
 *              ReturnType          - Where the object type is returned
 *              ReturnDesc          - Where the resolved object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the base object and type. Traverse a reference list if
 *              necessary to get to the base object.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExResolveMultiple (
    ACPI_WALK_STATE         *WalkState,
    ACPI_OPERAND_OBJECT     *Operand,
    ACPI_OBJECT_TYPE        *ReturnType,
    ACPI_OPERAND_OBJECT     **ReturnDesc)
{
    ACPI_OPERAND_OBJECT     *ObjDesc = ACPI_CAST_PTR (void, Operand);
    ACPI_NAMESPACE_NODE     *Node = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, Operand);
    ACPI_OBJECT_TYPE        Type;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (AcpiExResolveMultiple);


    /* Operand can be either a namespace node or an operand descriptor */

    switch (ACPI_GET_DESCRIPTOR_TYPE (ObjDesc))
    {
    case ACPI_DESC_TYPE_OPERAND:

        Type = ObjDesc->Common.Type;
        break;

    case ACPI_DESC_TYPE_NAMED:

        Type = ((ACPI_NAMESPACE_NODE *) ObjDesc)->Type;
        ObjDesc = AcpiNsGetAttachedObject (Node);

        /* If we had an Alias node, use the attached object for type info */

        if (Type == ACPI_TYPE_LOCAL_ALIAS)
        {
            Type = ((ACPI_NAMESPACE_NODE *) ObjDesc)->Type;
            ObjDesc = AcpiNsGetAttachedObject (
                (ACPI_NAMESPACE_NODE *) ObjDesc);
        }

        switch (Type)
        {
        case ACPI_TYPE_DEVICE:
        case ACPI_TYPE_THERMAL:

            /* These types have no attached subobject */
            break;

        default:

            /* All other types require a subobject */

            if (!ObjDesc)
            {
                ACPI_ERROR ((AE_INFO,
                    "[%4.4s] Node is unresolved or uninitialized",
                    AcpiUtGetNodeName (Node)));
                return_ACPI_STATUS (AE_AML_UNINITIALIZED_NODE);
            }
            break;
        }
        break;

    default:
        return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
    }

    /* If type is anything other than a reference, we are done */

    if (Type != ACPI_TYPE_LOCAL_REFERENCE)
    {
        goto Exit;
    }

    /*
     * For reference objects created via the RefOf, Index, or Load/LoadTable
     * operators, we need to get to the base object (as per the ACPI
     * specification of the ObjectType and SizeOf operators). This means
     * traversing the list of possibly many nested references.
     */
    while (ObjDesc->Common.Type == ACPI_TYPE_LOCAL_REFERENCE)
    {
        switch (ObjDesc->Reference.Class)
        {
        case ACPI_REFCLASS_REFOF:
        case ACPI_REFCLASS_NAME:

            /* Dereference the reference pointer */

            if (ObjDesc->Reference.Class == ACPI_REFCLASS_REFOF)
            {
                Node = ObjDesc->Reference.Object;
            }
            else /* AML_INT_NAMEPATH_OP */
            {
                Node = ObjDesc->Reference.Node;
            }

            /* All "References" point to a NS node */

            if (ACPI_GET_DESCRIPTOR_TYPE (Node) != ACPI_DESC_TYPE_NAMED)
            {
                ACPI_ERROR ((AE_INFO,
                    "Not a namespace node %p [%s]",
                    Node, AcpiUtGetDescriptorName (Node)));
                return_ACPI_STATUS (AE_AML_INTERNAL);
            }

            /* Get the attached object */

            ObjDesc = AcpiNsGetAttachedObject (Node);
            if (!ObjDesc)
            {
                /* No object, use the NS node type */

                Type = AcpiNsGetType (Node);
                goto Exit;
            }

            /* Check for circular references */

            if (ObjDesc == Operand)
            {
                return_ACPI_STATUS (AE_AML_CIRCULAR_REFERENCE);
            }
            break;

        case ACPI_REFCLASS_INDEX:

            /* Get the type of this reference (index into another object) */

            Type = ObjDesc->Reference.TargetType;
            if (Type != ACPI_TYPE_PACKAGE)
            {
                goto Exit;
            }

            /*
             * The main object is a package, we want to get the type
             * of the individual package element that is referenced by
             * the index.
             *
             * This could of course in turn be another reference object.
             */
            ObjDesc = *(ObjDesc->Reference.Where);
            if (!ObjDesc)
            {
                /* NULL package elements are allowed */

                Type = 0; /* Uninitialized */
                goto Exit;
            }
            break;

        case ACPI_REFCLASS_TABLE:

            Type = ACPI_TYPE_DDB_HANDLE;
            goto Exit;

        case ACPI_REFCLASS_LOCAL:
        case ACPI_REFCLASS_ARG:

            if (ReturnDesc)
            {
                Status = AcpiDsMethodDataGetValue (ObjDesc->Reference.Class,
                    ObjDesc->Reference.Value, WalkState, &ObjDesc);
                if (ACPI_FAILURE (Status))
                {
                    return_ACPI_STATUS (Status);
                }
                AcpiUtRemoveReference (ObjDesc);
            }
            else
            {
                Status = AcpiDsMethodDataGetNode (ObjDesc->Reference.Class,
                    ObjDesc->Reference.Value, WalkState, &Node);
                if (ACPI_FAILURE (Status))
                {
                    return_ACPI_STATUS (Status);
                }

                ObjDesc = AcpiNsGetAttachedObject (Node);
                if (!ObjDesc)
                {
                    Type = ACPI_TYPE_ANY;
                    goto Exit;
                }
            }
            break;

        case ACPI_REFCLASS_DEBUG:

            /* The Debug Object is of type "DebugObject" */

            Type = ACPI_TYPE_DEBUG_OBJECT;
            goto Exit;

        default:

            ACPI_ERROR ((AE_INFO,
                "Unknown Reference Class 0x%2.2X",
                ObjDesc->Reference.Class));
            return_ACPI_STATUS (AE_AML_INTERNAL);
        }
    }

    /*
     * Now we are guaranteed to have an object that has not been created
     * via the RefOf or Index operators.
     */
    Type = ObjDesc->Common.Type;


Exit:
    /* Convert internal types to external types */

    switch (Type)
    {
    case ACPI_TYPE_LOCAL_REGION_FIELD:
    case ACPI_TYPE_LOCAL_BANK_FIELD:
    case ACPI_TYPE_LOCAL_INDEX_FIELD:

        Type = ACPI_TYPE_FIELD_UNIT;
        break;

    case ACPI_TYPE_LOCAL_SCOPE:

        /* Per ACPI Specification, Scope is untyped */

        Type = ACPI_TYPE_ANY;
        break;

    default:

        /* No change to Type required */

        break;
    }

    *ReturnType = Type;
    if (ReturnDesc)
    {
        *ReturnDesc = ObjDesc;
    }
    return_ACPI_STATUS (AE_OK);
}
