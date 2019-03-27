/*******************************************************************************
 *
 * Module Name: nsobject - Utilities for objects attached to namespace
 *                         table entries
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


#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nsobject")


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsAttachObject
 *
 * PARAMETERS:  Node                - Parent Node
 *              Object              - Object to be attached
 *              Type                - Type of object, or ACPI_TYPE_ANY if not
 *                                    known
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Record the given object as the value associated with the
 *              name whose ACPI_HANDLE is passed. If Object is NULL
 *              and Type is ACPI_TYPE_ANY, set the name as having no value.
 *              Note: Future may require that the Node->Flags field be passed
 *              as a parameter.
 *
 * MUTEX:       Assumes namespace is locked
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsAttachObject (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OPERAND_OBJECT     *Object,
    ACPI_OBJECT_TYPE        Type)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     *LastObjDesc;
    ACPI_OBJECT_TYPE        ObjectType = ACPI_TYPE_ANY;


    ACPI_FUNCTION_TRACE (NsAttachObject);


    /*
     * Parameter validation
     */
    if (!Node)
    {
        /* Invalid handle */

        ACPI_ERROR ((AE_INFO, "Null NamedObj handle"));
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    if (!Object && (ACPI_TYPE_ANY != Type))
    {
        /* Null object */

        ACPI_ERROR ((AE_INFO,
            "Null object, but type not ACPI_TYPE_ANY"));
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    if (ACPI_GET_DESCRIPTOR_TYPE (Node) != ACPI_DESC_TYPE_NAMED)
    {
        /* Not a name handle */

        ACPI_ERROR ((AE_INFO, "Invalid handle %p [%s]",
            Node, AcpiUtGetDescriptorName (Node)));
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /* Check if this object is already attached */

    if (Node->Object == Object)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "Obj %p already installed in NameObj %p\n",
            Object, Node));

        return_ACPI_STATUS (AE_OK);
    }

    /* If null object, we will just install it */

    if (!Object)
    {
        ObjDesc    = NULL;
        ObjectType = ACPI_TYPE_ANY;
    }

    /*
     * If the source object is a namespace Node with an attached object,
     * we will use that (attached) object
     */
    else if ((ACPI_GET_DESCRIPTOR_TYPE (Object) == ACPI_DESC_TYPE_NAMED) &&
            ((ACPI_NAMESPACE_NODE *) Object)->Object)
    {
        /*
         * Value passed is a name handle and that name has a
         * non-null value. Use that name's value and type.
         */
        ObjDesc = ((ACPI_NAMESPACE_NODE *) Object)->Object;
        ObjectType = ((ACPI_NAMESPACE_NODE *) Object)->Type;
    }

    /*
     * Otherwise, we will use the parameter object, but we must type
     * it first
     */
    else
    {
        ObjDesc = (ACPI_OPERAND_OBJECT  *) Object;

        /* Use the given type */

        ObjectType = Type;
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Installing %p into Node %p [%4.4s]\n",
        ObjDesc, Node, AcpiUtGetNodeName (Node)));

    /* Detach an existing attached object if present */

    if (Node->Object)
    {
        AcpiNsDetachObject (Node);
    }

    if (ObjDesc)
    {
        /*
         * Must increment the new value's reference count
         * (if it is an internal object)
         */
        AcpiUtAddReference (ObjDesc);

        /*
         * Handle objects with multiple descriptors - walk
         * to the end of the descriptor list
         */
        LastObjDesc = ObjDesc;
        while (LastObjDesc->Common.NextObject)
        {
            LastObjDesc = LastObjDesc->Common.NextObject;
        }

        /* Install the object at the front of the object list */

        LastObjDesc->Common.NextObject = Node->Object;
    }

    Node->Type = (UINT8) ObjectType;
    Node->Object = ObjDesc;

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsDetachObject
 *
 * PARAMETERS:  Node           - A Namespace node whose object will be detached
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Detach/delete an object associated with a namespace node.
 *              if the object is an allocated object, it is freed.
 *              Otherwise, the field is simply cleared.
 *
 ******************************************************************************/

void
AcpiNsDetachObject (
    ACPI_NAMESPACE_NODE     *Node)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;


    ACPI_FUNCTION_TRACE (NsDetachObject);


    ObjDesc = Node->Object;

    if (!ObjDesc ||
        (ObjDesc->Common.Type == ACPI_TYPE_LOCAL_DATA))
    {
        return_VOID;
    }

    if (Node->Flags & ANOBJ_ALLOCATED_BUFFER)
    {
        /* Free the dynamic aml buffer */

        if (ObjDesc->Common.Type == ACPI_TYPE_METHOD)
        {
            ACPI_FREE (ObjDesc->Method.AmlStart);
        }
    }

    /* Clear the Node entry in all cases */

    Node->Object = NULL;
    if (ACPI_GET_DESCRIPTOR_TYPE (ObjDesc) == ACPI_DESC_TYPE_OPERAND)
    {
        /* Unlink object from front of possible object list */

        Node->Object = ObjDesc->Common.NextObject;

        /* Handle possible 2-descriptor object */

        if (Node->Object &&
           (Node->Object->Common.Type != ACPI_TYPE_LOCAL_DATA))
        {
            Node->Object = Node->Object->Common.NextObject;
        }

        /*
         * Detach the object from any data objects (which are still held by
         * the namespace node)
         */
        if (ObjDesc->Common.NextObject &&
           ((ObjDesc->Common.NextObject)->Common.Type == ACPI_TYPE_LOCAL_DATA))
        {
           ObjDesc->Common.NextObject = NULL;
        }
    }

    /* Reset the node type to untyped */

    Node->Type = ACPI_TYPE_ANY;

    ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "Node %p [%4.4s] Object %p\n",
        Node, AcpiUtGetNodeName (Node), ObjDesc));

    /* Remove one reference on the object (and all subobjects) */

    AcpiUtRemoveReference (ObjDesc);
    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsGetAttachedObject
 *
 * PARAMETERS:  Node             - Namespace node
 *
 * RETURN:      Current value of the object field from the Node whose
 *              handle is passed
 *
 * DESCRIPTION: Obtain the object attached to a namespace node.
 *
 ******************************************************************************/

ACPI_OPERAND_OBJECT *
AcpiNsGetAttachedObject (
    ACPI_NAMESPACE_NODE     *Node)
{
    ACPI_FUNCTION_TRACE_PTR (NsGetAttachedObject, Node);


    if (!Node)
    {
        ACPI_WARNING ((AE_INFO, "Null Node ptr"));
        return_PTR (NULL);
    }

    if (!Node->Object ||
            ((ACPI_GET_DESCRIPTOR_TYPE (Node->Object) != ACPI_DESC_TYPE_OPERAND) &&
             (ACPI_GET_DESCRIPTOR_TYPE (Node->Object) != ACPI_DESC_TYPE_NAMED))  ||
        ((Node->Object)->Common.Type == ACPI_TYPE_LOCAL_DATA))
    {
        return_PTR (NULL);
    }

    return_PTR (Node->Object);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsGetSecondaryObject
 *
 * PARAMETERS:  Node             - Namespace node
 *
 * RETURN:      Current value of the object field from the Node whose
 *              handle is passed.
 *
 * DESCRIPTION: Obtain a secondary object associated with a namespace node.
 *
 ******************************************************************************/

ACPI_OPERAND_OBJECT *
AcpiNsGetSecondaryObject (
    ACPI_OPERAND_OBJECT     *ObjDesc)
{
    ACPI_FUNCTION_TRACE_PTR (NsGetSecondaryObject, ObjDesc);


    if ((!ObjDesc)                                     ||
        (ObjDesc->Common.Type== ACPI_TYPE_LOCAL_DATA)  ||
        (!ObjDesc->Common.NextObject)                  ||
        ((ObjDesc->Common.NextObject)->Common.Type == ACPI_TYPE_LOCAL_DATA))
    {
        return_PTR (NULL);
    }

    return_PTR (ObjDesc->Common.NextObject);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsAttachData
 *
 * PARAMETERS:  Node            - Namespace node
 *              Handler         - Handler to be associated with the data
 *              Data            - Data to be attached
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Low-level attach data. Create and attach a Data object.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsAttachData (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OBJECT_HANDLER     Handler,
    void                    *Data)
{
    ACPI_OPERAND_OBJECT     *PrevObjDesc;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     *DataDesc;


    /* We only allow one attachment per handler */

    PrevObjDesc = NULL;
    ObjDesc = Node->Object;
    while (ObjDesc)
    {
        if ((ObjDesc->Common.Type == ACPI_TYPE_LOCAL_DATA) &&
            (ObjDesc->Data.Handler == Handler))
        {
            return (AE_ALREADY_EXISTS);
        }

        PrevObjDesc = ObjDesc;
        ObjDesc = ObjDesc->Common.NextObject;
    }

    /* Create an internal object for the data */

    DataDesc = AcpiUtCreateInternalObject (ACPI_TYPE_LOCAL_DATA);
    if (!DataDesc)
    {
        return (AE_NO_MEMORY);
    }

    DataDesc->Data.Handler = Handler;
    DataDesc->Data.Pointer = Data;

    /* Install the data object */

    if (PrevObjDesc)
    {
        PrevObjDesc->Common.NextObject = DataDesc;
    }
    else
    {
        Node->Object = DataDesc;
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsDetachData
 *
 * PARAMETERS:  Node            - Namespace node
 *              Handler         - Handler associated with the data
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Low-level detach data. Delete the data node, but the caller
 *              is responsible for the actual data.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsDetachData (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OBJECT_HANDLER     Handler)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     *PrevObjDesc;


    PrevObjDesc = NULL;
    ObjDesc = Node->Object;
    while (ObjDesc)
    {
        if ((ObjDesc->Common.Type == ACPI_TYPE_LOCAL_DATA) &&
            (ObjDesc->Data.Handler == Handler))
        {
            if (PrevObjDesc)
            {
                PrevObjDesc->Common.NextObject = ObjDesc->Common.NextObject;
            }
            else
            {
                Node->Object = ObjDesc->Common.NextObject;
            }

            AcpiUtRemoveReference (ObjDesc);
            return (AE_OK);
        }

        PrevObjDesc = ObjDesc;
        ObjDesc = ObjDesc->Common.NextObject;
    }

    return (AE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsGetAttachedData
 *
 * PARAMETERS:  Node            - Namespace node
 *              Handler         - Handler associated with the data
 *              Data            - Where the data is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Low level interface to obtain data previously associated with
 *              a namespace node.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsGetAttachedData (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OBJECT_HANDLER     Handler,
    void                    **Data)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;


    ObjDesc = Node->Object;
    while (ObjDesc)
    {
        if ((ObjDesc->Common.Type == ACPI_TYPE_LOCAL_DATA) &&
            (ObjDesc->Data.Handler == Handler))
        {
            *Data = ObjDesc->Data.Pointer;
            return (AE_OK);
        }

        ObjDesc = ObjDesc->Common.NextObject;
    }

    return (AE_NOT_FOUND);
}
