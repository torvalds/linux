/******************************************************************************
 *
 * Module Name: exstore - AML Interpreter object store support
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
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exstore")

/* Local prototypes */

static ACPI_STATUS
AcpiExStoreObjectToIndex (
    ACPI_OPERAND_OBJECT     *ValDesc,
    ACPI_OPERAND_OBJECT     *DestDesc,
    ACPI_WALK_STATE         *WalkState);

static ACPI_STATUS
AcpiExStoreDirectToNode (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_WALK_STATE         *WalkState);


/*******************************************************************************
 *
 * FUNCTION:    AcpiExStore
 *
 * PARAMETERS:  *SourceDesc         - Value to be stored
 *              *DestDesc           - Where to store it. Must be an NS node
 *                                    or ACPI_OPERAND_OBJECT of type
 *                                    Reference;
 *              WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the value described by SourceDesc into the location
 *              described by DestDesc. Called by various interpreter
 *              functions to store the result of an operation into
 *              the destination operand -- not just simply the actual "Store"
 *              ASL operator.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExStore (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_OPERAND_OBJECT     *DestDesc,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_OPERAND_OBJECT     *RefDesc = DestDesc;


    ACPI_FUNCTION_TRACE_PTR (ExStore, DestDesc);


    /* Validate parameters */

    if (!SourceDesc || !DestDesc)
    {
        ACPI_ERROR ((AE_INFO, "Null parameter"));
        return_ACPI_STATUS (AE_AML_NO_OPERAND);
    }

    /* DestDesc can be either a namespace node or an ACPI object */

    if (ACPI_GET_DESCRIPTOR_TYPE (DestDesc) == ACPI_DESC_TYPE_NAMED)
    {
        /*
         * Dest is a namespace node,
         * Storing an object into a Named node.
         */
        Status = AcpiExStoreObjectToNode (SourceDesc,
            (ACPI_NAMESPACE_NODE *) DestDesc, WalkState,
            ACPI_IMPLICIT_CONVERSION);

        return_ACPI_STATUS (Status);
    }

    /* Destination object must be a Reference or a Constant object */

    switch (DestDesc->Common.Type)
    {
    case ACPI_TYPE_LOCAL_REFERENCE:

        break;

    case ACPI_TYPE_INTEGER:

        /* Allow stores to Constants -- a Noop as per ACPI spec */

        if (DestDesc->Common.Flags & AOPOBJ_AML_CONSTANT)
        {
            return_ACPI_STATUS (AE_OK);
        }

        /*lint -fallthrough */

    default:

        /* Destination is not a Reference object */

        ACPI_ERROR ((AE_INFO,
            "Target is not a Reference or Constant object - [%s] %p",
            AcpiUtGetObjectTypeName (DestDesc), DestDesc));

        return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
    }

    /*
     * Examine the Reference class. These cases are handled:
     *
     * 1) Store to Name (Change the object associated with a name)
     * 2) Store to an indexed area of a Buffer or Package
     * 3) Store to a Method Local or Arg
     * 4) Store to the debug object
     */
    switch (RefDesc->Reference.Class)
    {
    case ACPI_REFCLASS_REFOF:

        /* Storing an object into a Name "container" */

        Status = AcpiExStoreObjectToNode (SourceDesc,
            RefDesc->Reference.Object,
            WalkState, ACPI_IMPLICIT_CONVERSION);
        break;

    case ACPI_REFCLASS_INDEX:

        /* Storing to an Index (pointer into a packager or buffer) */

        Status = AcpiExStoreObjectToIndex (SourceDesc, RefDesc, WalkState);
        break;

    case ACPI_REFCLASS_LOCAL:
    case ACPI_REFCLASS_ARG:

        /* Store to a method local/arg  */

        Status = AcpiDsStoreObjectToLocal (RefDesc->Reference.Class,
            RefDesc->Reference.Value, SourceDesc, WalkState);
        break;

    case ACPI_REFCLASS_DEBUG:
        /*
         * Storing to the Debug object causes the value stored to be
         * displayed and otherwise has no effect -- see ACPI Specification
         */
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "**** Write to Debug Object: Object %p [%s] ****:\n\n",
            SourceDesc, AcpiUtGetObjectTypeName (SourceDesc)));

        ACPI_DEBUG_OBJECT (SourceDesc, 0, 0);
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Unknown Reference Class 0x%2.2X",
            RefDesc->Reference.Class));
        ACPI_DUMP_ENTRY (RefDesc, ACPI_LV_INFO);

        Status = AE_AML_INTERNAL;
        break;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExStoreObjectToIndex
 *
 * PARAMETERS:  *SourceDesc             - Value to be stored
 *              *DestDesc               - Named object to receive the value
 *              WalkState               - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to indexed Buffer or Package element
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiExStoreObjectToIndex (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_OPERAND_OBJECT     *IndexDesc,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     *NewDesc;
    UINT8                   Value = 0;
    UINT32                  i;


    ACPI_FUNCTION_TRACE (ExStoreObjectToIndex);


    /*
     * Destination must be a reference pointer, and
     * must point to either a buffer or a package
     */
    switch (IndexDesc->Reference.TargetType)
    {
    case ACPI_TYPE_PACKAGE:
        /*
         * Storing to a package element. Copy the object and replace
         * any existing object with the new object. No implicit
         * conversion is performed.
         *
         * The object at *(IndexDesc->Reference.Where) is the
         * element within the package that is to be modified.
         * The parent package object is at IndexDesc->Reference.Object
         */
        ObjDesc = *(IndexDesc->Reference.Where);

        if (SourceDesc->Common.Type == ACPI_TYPE_LOCAL_REFERENCE &&
            SourceDesc->Reference.Class == ACPI_REFCLASS_TABLE)
        {
            /* This is a DDBHandle, just add a reference to it */

            AcpiUtAddReference (SourceDesc);
            NewDesc = SourceDesc;
        }
        else
        {
            /* Normal object, copy it */

            Status = AcpiUtCopyIobjectToIobject (
                SourceDesc, &NewDesc, WalkState);
            if (ACPI_FAILURE (Status))
            {
                return_ACPI_STATUS (Status);
            }
        }

        if (ObjDesc)
        {
            /* Decrement reference count by the ref count of the parent package */

            for (i = 0;
                 i < ((ACPI_OPERAND_OBJECT *)
                        IndexDesc->Reference.Object)->Common.ReferenceCount;
                 i++)
            {
                AcpiUtRemoveReference (ObjDesc);
            }
        }

        *(IndexDesc->Reference.Where) = NewDesc;

        /* Increment ref count by the ref count of the parent package-1 */

        for (i = 1;
             i < ((ACPI_OPERAND_OBJECT *)
                    IndexDesc->Reference.Object)->Common.ReferenceCount;
             i++)
        {
            AcpiUtAddReference (NewDesc);
        }

        break;

    case ACPI_TYPE_BUFFER_FIELD:
        /*
         * Store into a Buffer or String (not actually a real BufferField)
         * at a location defined by an Index.
         *
         * The first 8-bit element of the source object is written to the
         * 8-bit Buffer location defined by the Index destination object,
         * according to the ACPI 2.0 specification.
         */

        /*
         * Make sure the target is a Buffer or String. An error should
         * not happen here, since the ReferenceObject was constructed
         * by the INDEX_OP code.
         */
        ObjDesc = IndexDesc->Reference.Object;
        if ((ObjDesc->Common.Type != ACPI_TYPE_BUFFER) &&
            (ObjDesc->Common.Type != ACPI_TYPE_STRING))
        {
            return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
        }

        /*
         * The assignment of the individual elements will be slightly
         * different for each source type.
         */
        switch (SourceDesc->Common.Type)
        {
        case ACPI_TYPE_INTEGER:

            /* Use the least-significant byte of the integer */

            Value = (UINT8) (SourceDesc->Integer.Value);
            break;

        case ACPI_TYPE_BUFFER:
        case ACPI_TYPE_STRING:

            /* Note: Takes advantage of common string/buffer fields */

            Value = SourceDesc->Buffer.Pointer[0];
            break;

        default:

            /* All other types are invalid */

            ACPI_ERROR ((AE_INFO,
                "Source must be type [Integer/Buffer/String], found [%s]",
                AcpiUtGetObjectTypeName (SourceDesc)));
            return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
        }

        /* Store the source value into the target buffer byte */

        ObjDesc->Buffer.Pointer[IndexDesc->Reference.Value] = Value;
        break;

    default:
        ACPI_ERROR ((AE_INFO,
            "Target is not of type [Package/BufferField]"));
        Status = AE_AML_TARGET_TYPE;
        break;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExStoreObjectToNode
 *
 * PARAMETERS:  SourceDesc              - Value to be stored
 *              Node                    - Named object to receive the value
 *              WalkState               - Current walk state
 *              ImplicitConversion      - Perform implicit conversion (yes/no)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to the named object.
 *
 * The assignment of an object to a named object is handled here.
 * The value passed in will replace the current value (if any)
 * with the input value.
 *
 * When storing into an object the data is converted to the
 * target object type then stored in the object. This means
 * that the target object type (for an initialized target) will
 * not be changed by a store operation. A CopyObject can change
 * the target type, however.
 *
 * The ImplicitConversion flag is set to NO/FALSE only when
 * storing to an ArgX -- as per the rules of the ACPI spec.
 *
 * Assumes parameters are already validated.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExStoreObjectToNode (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_WALK_STATE         *WalkState,
    UINT8                   ImplicitConversion)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_OPERAND_OBJECT     *TargetDesc;
    ACPI_OPERAND_OBJECT     *NewDesc;
    ACPI_OBJECT_TYPE        TargetType;


    ACPI_FUNCTION_TRACE_PTR (ExStoreObjectToNode, SourceDesc);


    /* Get current type of the node, and object attached to Node */

    TargetType = AcpiNsGetType (Node);
    TargetDesc = AcpiNsGetAttachedObject (Node);

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Storing %p [%s] to node %p [%s]\n",
        SourceDesc, AcpiUtGetObjectTypeName (SourceDesc),
        Node, AcpiUtGetTypeName (TargetType)));

    /* Only limited target types possible for everything except CopyObject */

    if (WalkState->Opcode != AML_COPY_OBJECT_OP)
    {
        /*
         * Only CopyObject allows all object types to be overwritten. For
         * TargetRef(s), there are restrictions on the object types that
         * are allowed.
         *
         * Allowable operations/typing for Store:
         *
         * 1) Simple Store
         *      Integer     --> Integer (Named/Local/Arg)
         *      String      --> String  (Named/Local/Arg)
         *      Buffer      --> Buffer  (Named/Local/Arg)
         *      Package     --> Package (Named/Local/Arg)
         *
         * 2) Store with implicit conversion
         *      Integer     --> String or Buffer  (Named)
         *      String      --> Integer or Buffer (Named)
         *      Buffer      --> Integer or String (Named)
         */
        switch (TargetType)
        {
        case ACPI_TYPE_PACKAGE:
            /*
             * Here, can only store a package to an existing package.
             * Storing a package to a Local/Arg is OK, and handled
             * elsewhere.
             */
            if (WalkState->Opcode == AML_STORE_OP)
            {
                if (SourceDesc->Common.Type != ACPI_TYPE_PACKAGE)
                {
                    ACPI_ERROR ((AE_INFO,
                        "Cannot assign type [%s] to [Package] "
                        "(source must be type Pkg)",
                        AcpiUtGetObjectTypeName (SourceDesc)));

                    return_ACPI_STATUS (AE_AML_TARGET_TYPE);
                }
                break;
            }

        /* Fallthrough */

        case ACPI_TYPE_DEVICE:
        case ACPI_TYPE_EVENT:
        case ACPI_TYPE_MUTEX:
        case ACPI_TYPE_REGION:
        case ACPI_TYPE_POWER:
        case ACPI_TYPE_PROCESSOR:
        case ACPI_TYPE_THERMAL:

            ACPI_ERROR ((AE_INFO,
                "Target must be [Buffer/Integer/String/Reference]"
                ", found [%s] (%4.4s)",
                AcpiUtGetTypeName (Node->Type), Node->Name.Ascii));

            return_ACPI_STATUS (AE_AML_TARGET_TYPE);

        default:
            break;
        }
    }

    /*
     * Resolve the source object to an actual value
     * (If it is a reference object)
     */
    Status = AcpiExResolveObject (&SourceDesc, TargetType, WalkState);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Do the actual store operation */

    switch (TargetType)
    {
        /*
         * The simple data types all support implicit source operand
         * conversion before the store.
         */
    case ACPI_TYPE_INTEGER:
    case ACPI_TYPE_STRING:
    case ACPI_TYPE_BUFFER:

        if ((WalkState->Opcode == AML_COPY_OBJECT_OP) ||
            !ImplicitConversion)
        {
            /*
             * However, CopyObject and Stores to ArgX do not perform
             * an implicit conversion, as per the ACPI specification.
             * A direct store is performed instead.
             */
            Status = AcpiExStoreDirectToNode (SourceDesc, Node, WalkState);
            break;
        }

        /* Store with implicit source operand conversion support */

        Status = AcpiExStoreObjectToObject (SourceDesc, TargetDesc,
            &NewDesc, WalkState);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        if (NewDesc != TargetDesc)
        {
            /*
             * Store the new NewDesc as the new value of the Name, and set
             * the Name's type to that of the value being stored in it.
             * SourceDesc reference count is incremented by AttachObject.
             *
             * Note: This may change the type of the node if an explicit
             * store has been performed such that the node/object type
             * has been changed.
             */
            Status = AcpiNsAttachObject (
                Node, NewDesc, NewDesc->Common.Type);

            ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
                "Store type [%s] into [%s] via Convert/Attach\n",
                AcpiUtGetObjectTypeName (SourceDesc),
                AcpiUtGetObjectTypeName (NewDesc)));
        }
        break;

    case ACPI_TYPE_BUFFER_FIELD:
    case ACPI_TYPE_LOCAL_REGION_FIELD:
    case ACPI_TYPE_LOCAL_BANK_FIELD:
    case ACPI_TYPE_LOCAL_INDEX_FIELD:
        /*
         * For all fields, always write the source data to the target
         * field. Any required implicit source operand conversion is
         * performed in the function below as necessary. Note, field
         * objects must retain their original type permanently.
         */
        Status = AcpiExWriteDataToField (SourceDesc, TargetDesc,
            &WalkState->ResultObj);
        break;

    default:
        /*
         * CopyObject operator: No conversions for all other types.
         * Instead, directly store a copy of the source object.
         *
         * This is the ACPI spec-defined behavior for the CopyObject
         * operator. (Note, for this default case, all normal
         * Store/Target operations exited above with an error).
         */
        Status = AcpiExStoreDirectToNode (SourceDesc, Node, WalkState);
        break;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExStoreDirectToNode
 *
 * PARAMETERS:  SourceDesc              - Value to be stored
 *              Node                    - Named object to receive the value
 *              WalkState               - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: "Store" an object directly to a node. This involves a copy
 *              and an attach.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiExStoreDirectToNode (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *NewDesc;


    ACPI_FUNCTION_TRACE (ExStoreDirectToNode);


    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
        "Storing [%s] (%p) directly into node [%s] (%p)"
        " with no implicit conversion\n",
        AcpiUtGetObjectTypeName (SourceDesc), SourceDesc,
        AcpiUtGetTypeName (Node->Type), Node));

    /* Copy the source object to a new object */

    Status = AcpiUtCopyIobjectToIobject (SourceDesc, &NewDesc, WalkState);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Attach the new object to the node */

    Status = AcpiNsAttachObject (Node, NewDesc, NewDesc->Common.Type);
    AcpiUtRemoveReference (NewDesc);
    return_ACPI_STATUS (Status);
}
