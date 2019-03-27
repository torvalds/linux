/******************************************************************************
 *
 * Module Name: utcopy - Internal to external object translation utilities
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
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utcopy")

/* Local prototypes */

static ACPI_STATUS
AcpiUtCopyIsimpleToEsimple (
    ACPI_OPERAND_OBJECT     *InternalObject,
    ACPI_OBJECT             *ExternalObject,
    UINT8                   *DataSpace,
    ACPI_SIZE               *BufferSpaceUsed);

static ACPI_STATUS
AcpiUtCopyIelementToIelement (
    UINT8                   ObjectType,
    ACPI_OPERAND_OBJECT     *SourceObject,
    ACPI_GENERIC_STATE      *State,
    void                    *Context);

static ACPI_STATUS
AcpiUtCopyIpackageToEpackage (
    ACPI_OPERAND_OBJECT     *InternalObject,
    UINT8                   *Buffer,
    ACPI_SIZE               *SpaceUsed);

static ACPI_STATUS
AcpiUtCopyEsimpleToIsimple(
    ACPI_OBJECT             *UserObj,
    ACPI_OPERAND_OBJECT     **ReturnObj);

static ACPI_STATUS
AcpiUtCopyEpackageToIpackage (
    ACPI_OBJECT             *ExternalObject,
    ACPI_OPERAND_OBJECT     **InternalObject);

static ACPI_STATUS
AcpiUtCopySimpleObject (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_OPERAND_OBJECT     *DestDesc);

static ACPI_STATUS
AcpiUtCopyIelementToEelement (
    UINT8                   ObjectType,
    ACPI_OPERAND_OBJECT     *SourceObject,
    ACPI_GENERIC_STATE      *State,
    void                    *Context);

static ACPI_STATUS
AcpiUtCopyIpackageToIpackage (
    ACPI_OPERAND_OBJECT     *SourceObj,
    ACPI_OPERAND_OBJECT     *DestObj,
    ACPI_WALK_STATE         *WalkState);


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyIsimpleToEsimple
 *
 * PARAMETERS:  InternalObject      - Source object to be copied
 *              ExternalObject      - Where to return the copied object
 *              DataSpace           - Where object data is returned (such as
 *                                    buffer and string data)
 *              BufferSpaceUsed     - Length of DataSpace that was used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to copy a simple internal object to
 *              an external object.
 *
 *              The DataSpace buffer is assumed to have sufficient space for
 *              the object.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtCopyIsimpleToEsimple (
    ACPI_OPERAND_OBJECT     *InternalObject,
    ACPI_OBJECT             *ExternalObject,
    UINT8                   *DataSpace,
    ACPI_SIZE               *BufferSpaceUsed)
{
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE (UtCopyIsimpleToEsimple);


    *BufferSpaceUsed = 0;

    /*
     * Check for NULL object case (could be an uninitialized
     * package element)
     */
    if (!InternalObject)
    {
        return_ACPI_STATUS (AE_OK);
    }

    /* Always clear the external object */

    memset (ExternalObject, 0, sizeof (ACPI_OBJECT));

    /*
     * In general, the external object will be the same type as
     * the internal object
     */
    ExternalObject->Type = InternalObject->Common.Type;

    /* However, only a limited number of external types are supported */

    switch (InternalObject->Common.Type)
    {
    case ACPI_TYPE_STRING:

        ExternalObject->String.Pointer = (char *) DataSpace;
        ExternalObject->String.Length  = InternalObject->String.Length;
        *BufferSpaceUsed = ACPI_ROUND_UP_TO_NATIVE_WORD (
            (ACPI_SIZE) InternalObject->String.Length + 1);

        memcpy ((void *) DataSpace,
            (void *) InternalObject->String.Pointer,
            (ACPI_SIZE) InternalObject->String.Length + 1);
        break;

    case ACPI_TYPE_BUFFER:

        ExternalObject->Buffer.Pointer = DataSpace;
        ExternalObject->Buffer.Length  = InternalObject->Buffer.Length;
        *BufferSpaceUsed = ACPI_ROUND_UP_TO_NATIVE_WORD (
            InternalObject->String.Length);

        memcpy ((void *) DataSpace,
            (void *) InternalObject->Buffer.Pointer,
            InternalObject->Buffer.Length);
        break;

    case ACPI_TYPE_INTEGER:

        ExternalObject->Integer.Value = InternalObject->Integer.Value;
        break;

    case ACPI_TYPE_LOCAL_REFERENCE:

        /* This is an object reference. */

        switch (InternalObject->Reference.Class)
        {
        case ACPI_REFCLASS_NAME:
            /*
             * For namepath, return the object handle ("reference")
             * We are referring to the namespace node
             */
            ExternalObject->Reference.Handle =
                InternalObject->Reference.Node;
            ExternalObject->Reference.ActualType =
                AcpiNsGetType (InternalObject->Reference.Node);
            break;

        default:

            /* All other reference types are unsupported */

            return_ACPI_STATUS (AE_TYPE);
        }
        break;

    case ACPI_TYPE_PROCESSOR:

        ExternalObject->Processor.ProcId =
            InternalObject->Processor.ProcId;
        ExternalObject->Processor.PblkAddress =
            InternalObject->Processor.Address;
        ExternalObject->Processor.PblkLength =
            InternalObject->Processor.Length;
        break;

    case ACPI_TYPE_POWER:

        ExternalObject->PowerResource.SystemLevel =
            InternalObject->PowerResource.SystemLevel;

        ExternalObject->PowerResource.ResourceOrder =
            InternalObject->PowerResource.ResourceOrder;
        break;

    default:
        /*
         * There is no corresponding external object type
         */
        ACPI_ERROR ((AE_INFO,
            "Unsupported object type, cannot convert to external object: %s",
            AcpiUtGetTypeName (InternalObject->Common.Type)));

        return_ACPI_STATUS (AE_SUPPORT);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyIelementToEelement
 *
 * PARAMETERS:  ACPI_PKG_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy one package element to another package element
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtCopyIelementToEelement (
    UINT8                   ObjectType,
    ACPI_OPERAND_OBJECT     *SourceObject,
    ACPI_GENERIC_STATE      *State,
    void                    *Context)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_PKG_INFO           *Info = (ACPI_PKG_INFO *) Context;
    ACPI_SIZE               ObjectSpace;
    UINT32                  ThisIndex;
    ACPI_OBJECT             *TargetObject;


    ACPI_FUNCTION_ENTRY ();


    ThisIndex = State->Pkg.Index;
    TargetObject = (ACPI_OBJECT *) &((ACPI_OBJECT *)
        (State->Pkg.DestObject))->Package.Elements[ThisIndex];

    switch (ObjectType)
    {
    case ACPI_COPY_TYPE_SIMPLE:
        /*
         * This is a simple or null object
         */
        Status = AcpiUtCopyIsimpleToEsimple (SourceObject,
            TargetObject, Info->FreeSpace, &ObjectSpace);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        break;

    case ACPI_COPY_TYPE_PACKAGE:
        /*
         * Build the package object
         */
        TargetObject->Type = ACPI_TYPE_PACKAGE;
        TargetObject->Package.Count = SourceObject->Package.Count;
        TargetObject->Package.Elements =
            ACPI_CAST_PTR (ACPI_OBJECT, Info->FreeSpace);

        /*
         * Pass the new package object back to the package walk routine
         */
        State->Pkg.ThisTargetObj = TargetObject;

        /*
         * Save space for the array of objects (Package elements)
         * update the buffer length counter
         */
        ObjectSpace = ACPI_ROUND_UP_TO_NATIVE_WORD (
            (ACPI_SIZE) TargetObject->Package.Count *
            sizeof (ACPI_OBJECT));
        break;

    default:

        return (AE_BAD_PARAMETER);
    }

    Info->FreeSpace += ObjectSpace;
    Info->Length += ObjectSpace;
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyIpackageToEpackage
 *
 * PARAMETERS:  InternalObject      - Pointer to the object we are returning
 *              Buffer              - Where the object is returned
 *              SpaceUsed           - Where the object length is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to place a package object in a user
 *              buffer. A package object by definition contains other objects.
 *
 *              The buffer is assumed to have sufficient space for the object.
 *              The caller must have verified the buffer length needed using
 *              the AcpiUtGetObjectSize function before calling this function.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtCopyIpackageToEpackage (
    ACPI_OPERAND_OBJECT     *InternalObject,
    UINT8                   *Buffer,
    ACPI_SIZE               *SpaceUsed)
{
    ACPI_OBJECT             *ExternalObject;
    ACPI_STATUS             Status;
    ACPI_PKG_INFO           Info;


    ACPI_FUNCTION_TRACE (UtCopyIpackageToEpackage);


    /*
     * First package at head of the buffer
     */
    ExternalObject = ACPI_CAST_PTR (ACPI_OBJECT, Buffer);

    /*
     * Free space begins right after the first package
     */
    Info.Length = ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (ACPI_OBJECT));
    Info.FreeSpace = Buffer +
        ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (ACPI_OBJECT));
    Info.ObjectSpace = 0;
    Info.NumPackages = 1;

    ExternalObject->Type = InternalObject->Common.Type;
    ExternalObject->Package.Count = InternalObject->Package.Count;
    ExternalObject->Package.Elements =
        ACPI_CAST_PTR (ACPI_OBJECT, Info.FreeSpace);

    /*
     * Leave room for an array of ACPI_OBJECTS in the buffer
     * and move the free space past it
     */
    Info.Length += (ACPI_SIZE) ExternalObject->Package.Count *
        ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (ACPI_OBJECT));
    Info.FreeSpace += ExternalObject->Package.Count *
        ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (ACPI_OBJECT));

    Status = AcpiUtWalkPackageTree (InternalObject, ExternalObject,
        AcpiUtCopyIelementToEelement, &Info);

    *SpaceUsed = Info.Length;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyIobjectToEobject
 *
 * PARAMETERS:  InternalObject      - The internal object to be converted
 *              RetBuffer           - Where the object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to build an API object to be returned
 *              to the caller.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtCopyIobjectToEobject (
    ACPI_OPERAND_OBJECT     *InternalObject,
    ACPI_BUFFER             *RetBuffer)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (UtCopyIobjectToEobject);


    if (InternalObject->Common.Type == ACPI_TYPE_PACKAGE)
    {
        /*
         * Package object:  Copy all subobjects (including
         * nested packages)
         */
        Status = AcpiUtCopyIpackageToEpackage (InternalObject,
            RetBuffer->Pointer, &RetBuffer->Length);
    }
    else
    {
        /*
         * Build a simple object (no nested objects)
         */
        Status = AcpiUtCopyIsimpleToEsimple (InternalObject,
            ACPI_CAST_PTR (ACPI_OBJECT, RetBuffer->Pointer),
            ACPI_ADD_PTR (UINT8, RetBuffer->Pointer,
                ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (ACPI_OBJECT))),
            &RetBuffer->Length);
        /*
         * build simple does not include the object size in the length
         * so we add it in here
         */
        RetBuffer->Length += sizeof (ACPI_OBJECT);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyEsimpleToIsimple
 *
 * PARAMETERS:  ExternalObject      - The external object to be converted
 *              RetInternalObject   - Where the internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function copies an external object to an internal one.
 *              NOTE: Pointers can be copied, we don't need to copy data.
 *              (The pointers have to be valid in our address space no matter
 *              what we do with them!)
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtCopyEsimpleToIsimple (
    ACPI_OBJECT             *ExternalObject,
    ACPI_OPERAND_OBJECT     **RetInternalObject)
{
    ACPI_OPERAND_OBJECT     *InternalObject;


    ACPI_FUNCTION_TRACE (UtCopyEsimpleToIsimple);


    /*
     * Simple types supported are: String, Buffer, Integer
     */
    switch (ExternalObject->Type)
    {
    case ACPI_TYPE_STRING:
    case ACPI_TYPE_BUFFER:
    case ACPI_TYPE_INTEGER:
    case ACPI_TYPE_LOCAL_REFERENCE:

        InternalObject = AcpiUtCreateInternalObject (
            (UINT8) ExternalObject->Type);
        if (!InternalObject)
        {
            return_ACPI_STATUS (AE_NO_MEMORY);
        }
        break;

    case ACPI_TYPE_ANY: /* This is the case for a NULL object */

        *RetInternalObject = NULL;
        return_ACPI_STATUS (AE_OK);

    default:

        /* All other types are not supported */

        ACPI_ERROR ((AE_INFO,
            "Unsupported object type, cannot convert to internal object: %s",
            AcpiUtGetTypeName (ExternalObject->Type)));

        return_ACPI_STATUS (AE_SUPPORT);
    }


    /* Must COPY string and buffer contents */

    switch (ExternalObject->Type)
    {
    case ACPI_TYPE_STRING:

        InternalObject->String.Pointer =
            ACPI_ALLOCATE_ZEROED ((ACPI_SIZE)
                ExternalObject->String.Length + 1);

        if (!InternalObject->String.Pointer)
        {
            goto ErrorExit;
        }

        memcpy (InternalObject->String.Pointer,
            ExternalObject->String.Pointer,
            ExternalObject->String.Length);

        InternalObject->String.Length = ExternalObject->String.Length;
        break;

    case ACPI_TYPE_BUFFER:

        InternalObject->Buffer.Pointer =
            ACPI_ALLOCATE_ZEROED (ExternalObject->Buffer.Length);
        if (!InternalObject->Buffer.Pointer)
        {
            goto ErrorExit;
        }

        memcpy (InternalObject->Buffer.Pointer,
            ExternalObject->Buffer.Pointer,
            ExternalObject->Buffer.Length);

        InternalObject->Buffer.Length = ExternalObject->Buffer.Length;

        /* Mark buffer data valid */

        InternalObject->Buffer.Flags |= AOPOBJ_DATA_VALID;
        break;

    case ACPI_TYPE_INTEGER:

        InternalObject->Integer.Value = ExternalObject->Integer.Value;
        break;

    case ACPI_TYPE_LOCAL_REFERENCE:

        /* An incoming reference is defined to be a namespace node */

        InternalObject->Reference.Class = ACPI_REFCLASS_REFOF;
        InternalObject->Reference.Object = ExternalObject->Reference.Handle;
        break;

    default:

        /* Other types can't get here */

        break;
    }

    *RetInternalObject = InternalObject;
    return_ACPI_STATUS (AE_OK);


ErrorExit:
    AcpiUtRemoveReference (InternalObject);
    return_ACPI_STATUS (AE_NO_MEMORY);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyEpackageToIpackage
 *
 * PARAMETERS:  ExternalObject      - The external object to be converted
 *              InternalObject      - Where the internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy an external package object to an internal package.
 *              Handles nested packages.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtCopyEpackageToIpackage (
    ACPI_OBJECT             *ExternalObject,
    ACPI_OPERAND_OBJECT     **InternalObject)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_OPERAND_OBJECT     *PackageObject;
    ACPI_OPERAND_OBJECT     **PackageElements;
    UINT32                  i;


    ACPI_FUNCTION_TRACE (UtCopyEpackageToIpackage);


    /* Create the package object */

    PackageObject = AcpiUtCreatePackageObject (
        ExternalObject->Package.Count);
    if (!PackageObject)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    PackageElements = PackageObject->Package.Elements;

    /*
     * Recursive implementation. Probably ok, since nested external
     * packages as parameters should be very rare.
     */
    for (i = 0; i < ExternalObject->Package.Count; i++)
    {
        Status = AcpiUtCopyEobjectToIobject (
            &ExternalObject->Package.Elements[i],
            &PackageElements[i]);
        if (ACPI_FAILURE (Status))
        {
            /* Truncate package and delete it */

            PackageObject->Package.Count = i;
            PackageElements[i] = NULL;
            AcpiUtRemoveReference (PackageObject);
            return_ACPI_STATUS (Status);
        }
    }

    /* Mark package data valid */

    PackageObject->Package.Flags |= AOPOBJ_DATA_VALID;

    *InternalObject = PackageObject;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyEobjectToIobject
 *
 * PARAMETERS:  ExternalObject      - The external object to be converted
 *              InternalObject      - Where the internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Converts an external object to an internal object.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtCopyEobjectToIobject (
    ACPI_OBJECT             *ExternalObject,
    ACPI_OPERAND_OBJECT     **InternalObject)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (UtCopyEobjectToIobject);


    if (ExternalObject->Type == ACPI_TYPE_PACKAGE)
    {
        Status = AcpiUtCopyEpackageToIpackage (
            ExternalObject, InternalObject);
    }
    else
    {
        /*
         * Build a simple object (no nested objects)
         */
        Status = AcpiUtCopyEsimpleToIsimple (ExternalObject,
            InternalObject);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopySimpleObject
 *
 * PARAMETERS:  SourceDesc          - The internal object to be copied
 *              DestDesc            - New target object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Simple copy of one internal object to another. Reference count
 *              of the destination object is preserved.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtCopySimpleObject (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_OPERAND_OBJECT     *DestDesc)
{
    UINT16                  ReferenceCount;
    ACPI_OPERAND_OBJECT     *NextObject;
    ACPI_STATUS             Status;
    ACPI_SIZE               CopySize;


    /* Save fields from destination that we don't want to overwrite */

    ReferenceCount = DestDesc->Common.ReferenceCount;
    NextObject = DestDesc->Common.NextObject;

    /*
     * Copy the entire source object over the destination object.
     * Note: Source can be either an operand object or namespace node.
     */
    CopySize = sizeof (ACPI_OPERAND_OBJECT);
    if (ACPI_GET_DESCRIPTOR_TYPE (SourceDesc) == ACPI_DESC_TYPE_NAMED)
    {
        CopySize = sizeof (ACPI_NAMESPACE_NODE);
    }

    memcpy (ACPI_CAST_PTR (char, DestDesc),
        ACPI_CAST_PTR (char, SourceDesc), CopySize);

    /* Restore the saved fields */

    DestDesc->Common.ReferenceCount = ReferenceCount;
    DestDesc->Common.NextObject = NextObject;

    /* New object is not static, regardless of source */

    DestDesc->Common.Flags &= ~AOPOBJ_STATIC_POINTER;

    /* Handle the objects with extra data */

    switch (DestDesc->Common.Type)
    {
    case ACPI_TYPE_BUFFER:
        /*
         * Allocate and copy the actual buffer if and only if:
         * 1) There is a valid buffer pointer
         * 2) The buffer has a length > 0
         */
        if ((SourceDesc->Buffer.Pointer) &&
            (SourceDesc->Buffer.Length))
        {
            DestDesc->Buffer.Pointer =
                ACPI_ALLOCATE (SourceDesc->Buffer.Length);
            if (!DestDesc->Buffer.Pointer)
            {
                return (AE_NO_MEMORY);
            }

            /* Copy the actual buffer data */

            memcpy (DestDesc->Buffer.Pointer,
                SourceDesc->Buffer.Pointer, SourceDesc->Buffer.Length);
        }
        break;

    case ACPI_TYPE_STRING:
        /*
         * Allocate and copy the actual string if and only if:
         * 1) There is a valid string pointer
         * (Pointer to a NULL string is allowed)
         */
        if (SourceDesc->String.Pointer)
        {
            DestDesc->String.Pointer =
                ACPI_ALLOCATE ((ACPI_SIZE) SourceDesc->String.Length + 1);
            if (!DestDesc->String.Pointer)
            {
                return (AE_NO_MEMORY);
            }

            /* Copy the actual string data */

            memcpy (DestDesc->String.Pointer, SourceDesc->String.Pointer,
                (ACPI_SIZE) SourceDesc->String.Length + 1);
        }
        break;

    case ACPI_TYPE_LOCAL_REFERENCE:
        /*
         * We copied the reference object, so we now must add a reference
         * to the object pointed to by the reference
         *
         * DDBHandle reference (from Load/LoadTable) is a special reference,
         * it does not have a Reference.Object, so does not need to
         * increase the reference count
         */
        if (SourceDesc->Reference.Class == ACPI_REFCLASS_TABLE)
        {
            break;
        }

        AcpiUtAddReference (SourceDesc->Reference.Object);
        break;

    case ACPI_TYPE_REGION:
        /*
         * We copied the Region Handler, so we now must add a reference
         */
        if (DestDesc->Region.Handler)
        {
            AcpiUtAddReference (DestDesc->Region.Handler);
        }
        break;

    /*
     * For Mutex and Event objects, we cannot simply copy the underlying
     * OS object. We must create a new one.
     */
    case ACPI_TYPE_MUTEX:

        Status = AcpiOsCreateMutex (&DestDesc->Mutex.OsMutex);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        break;

    case ACPI_TYPE_EVENT:

        Status = AcpiOsCreateSemaphore (ACPI_NO_UNIT_LIMIT, 0,
            &DestDesc->Event.OsSemaphore);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        break;

    default:

        /* Nothing to do for other simple objects */

        break;
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyIelementToIelement
 *
 * PARAMETERS:  ACPI_PKG_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy one package element to another package element
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtCopyIelementToIelement (
    UINT8                   ObjectType,
    ACPI_OPERAND_OBJECT     *SourceObject,
    ACPI_GENERIC_STATE      *State,
    void                    *Context)
{
    ACPI_STATUS             Status = AE_OK;
    UINT32                  ThisIndex;
    ACPI_OPERAND_OBJECT     **ThisTargetPtr;
    ACPI_OPERAND_OBJECT     *TargetObject;


    ACPI_FUNCTION_ENTRY ();


    ThisIndex = State->Pkg.Index;
    ThisTargetPtr = (ACPI_OPERAND_OBJECT **)
        &State->Pkg.DestObject->Package.Elements[ThisIndex];

    switch (ObjectType)
    {
    case ACPI_COPY_TYPE_SIMPLE:

        /* A null source object indicates a (legal) null package element */

        if (SourceObject)
        {
            /*
             * This is a simple object, just copy it
             */
            TargetObject = AcpiUtCreateInternalObject (
                SourceObject->Common.Type);
            if (!TargetObject)
            {
                return (AE_NO_MEMORY);
            }

            Status = AcpiUtCopySimpleObject (SourceObject, TargetObject);
            if (ACPI_FAILURE (Status))
            {
                goto ErrorExit;
            }

            *ThisTargetPtr = TargetObject;
        }
        else
        {
            /* Pass through a null element */

            *ThisTargetPtr = NULL;
        }
        break;

    case ACPI_COPY_TYPE_PACKAGE:
        /*
         * This object is a package - go down another nesting level
         * Create and build the package object
         */
        TargetObject = AcpiUtCreatePackageObject (
            SourceObject->Package.Count);
        if (!TargetObject)
        {
            return (AE_NO_MEMORY);
        }

        TargetObject->Common.Flags = SourceObject->Common.Flags;

        /* Pass the new package object back to the package walk routine */

        State->Pkg.ThisTargetObj = TargetObject;

        /* Store the object pointer in the parent package object */

        *ThisTargetPtr = TargetObject;
        break;

    default:

        return (AE_BAD_PARAMETER);
    }

    return (Status);

ErrorExit:
    AcpiUtRemoveReference (TargetObject);
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyIpackageToIpackage
 *
 * PARAMETERS:  SourceObj       - Pointer to the source package object
 *              DestObj         - Where the internal object is returned
 *              WalkState       - Current Walk state descriptor
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to copy an internal package object
 *              into another internal package object.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtCopyIpackageToIpackage (
    ACPI_OPERAND_OBJECT     *SourceObj,
    ACPI_OPERAND_OBJECT     *DestObj,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE (UtCopyIpackageToIpackage);


    DestObj->Common.Type = SourceObj->Common.Type;
    DestObj->Common.Flags = SourceObj->Common.Flags;
    DestObj->Package.Count = SourceObj->Package.Count;

    /*
     * Create the object array and walk the source package tree
     */
    DestObj->Package.Elements = ACPI_ALLOCATE_ZEROED (
        ((ACPI_SIZE) SourceObj->Package.Count + 1) *
        sizeof (void *));
    if (!DestObj->Package.Elements)
    {
        ACPI_ERROR ((AE_INFO, "Package allocation failure"));
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /*
     * Copy the package element-by-element by walking the package "tree".
     * This handles nested packages of arbitrary depth.
     */
    Status = AcpiUtWalkPackageTree (SourceObj, DestObj,
        AcpiUtCopyIelementToIelement, WalkState);
    if (ACPI_FAILURE (Status))
    {
        /* On failure, delete the destination package object */

        AcpiUtRemoveReference (DestObj);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCopyIobjectToIobject
 *
 * PARAMETERS:  SourceDesc          - The internal object to be copied
 *              DestDesc            - Where the copied object is returned
 *              WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy an internal object to a new internal object
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtCopyIobjectToIobject (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_OPERAND_OBJECT     **DestDesc,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE (UtCopyIobjectToIobject);


    /* Create the top level object */

    *DestDesc = AcpiUtCreateInternalObject (SourceDesc->Common.Type);
    if (!*DestDesc)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Copy the object and possible subobjects */

    if (SourceDesc->Common.Type == ACPI_TYPE_PACKAGE)
    {
        Status = AcpiUtCopyIpackageToIpackage (
            SourceDesc, *DestDesc, WalkState);
    }
    else
    {
        Status = AcpiUtCopySimpleObject (SourceDesc, *DestDesc);
    }

    /* Delete the allocated object if copy failed */

    if (ACPI_FAILURE (Status))
    {
        AcpiUtRemoveReference (*DestDesc);
    }

    return_ACPI_STATUS (Status);
}
