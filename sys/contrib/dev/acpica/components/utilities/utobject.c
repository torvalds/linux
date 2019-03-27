/******************************************************************************
 *
 * Module Name: utobject - ACPI object create/delete/size/cache routines
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
        ACPI_MODULE_NAME    ("utobject")

/* Local prototypes */

static ACPI_STATUS
AcpiUtGetSimpleObjectSize (
    ACPI_OPERAND_OBJECT     *Obj,
    ACPI_SIZE               *ObjLength);

static ACPI_STATUS
AcpiUtGetPackageObjectSize (
    ACPI_OPERAND_OBJECT     *Obj,
    ACPI_SIZE               *ObjLength);

static ACPI_STATUS
AcpiUtGetElementLength (
    UINT8                   ObjectType,
    ACPI_OPERAND_OBJECT     *SourceObject,
    ACPI_GENERIC_STATE      *State,
    void                    *Context);


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCreateInternalObjectDbg
 *
 * PARAMETERS:  ModuleName          - Source file name of caller
 *              LineNumber          - Line number of caller
 *              ComponentId         - Component type of caller
 *              Type                - ACPI Type of the new object
 *
 * RETURN:      A new internal object, null on failure
 *
 * DESCRIPTION: Create and initialize a new internal object.
 *
 * NOTE:        We always allocate the worst-case object descriptor because
 *              these objects are cached, and we want them to be
 *              one-size-satisifies-any-request. This in itself may not be
 *              the most memory efficient, but the efficiency of the object
 *              cache should more than make up for this!
 *
 ******************************************************************************/

ACPI_OPERAND_OBJECT  *
AcpiUtCreateInternalObjectDbg (
    const char              *ModuleName,
    UINT32                  LineNumber,
    UINT32                  ComponentId,
    ACPI_OBJECT_TYPE        Type)
{
    ACPI_OPERAND_OBJECT     *Object;
    ACPI_OPERAND_OBJECT     *SecondObject;


    ACPI_FUNCTION_TRACE_STR (UtCreateInternalObjectDbg,
        AcpiUtGetTypeName (Type));


    /* Allocate the raw object descriptor */

    Object = AcpiUtAllocateObjectDescDbg (
        ModuleName, LineNumber, ComponentId);
    if (!Object)
    {
        return_PTR (NULL);
    }

    switch (Type)
    {
    case ACPI_TYPE_REGION:
    case ACPI_TYPE_BUFFER_FIELD:
    case ACPI_TYPE_LOCAL_BANK_FIELD:

        /* These types require a secondary object */

        SecondObject = AcpiUtAllocateObjectDescDbg (
            ModuleName, LineNumber, ComponentId);
        if (!SecondObject)
        {
            AcpiUtDeleteObjectDesc (Object);
            return_PTR (NULL);
        }

        SecondObject->Common.Type = ACPI_TYPE_LOCAL_EXTRA;
        SecondObject->Common.ReferenceCount = 1;

        /* Link the second object to the first */

        Object->Common.NextObject = SecondObject;
        break;

    default:

        /* All others have no secondary object */
        break;
    }

    /* Save the object type in the object descriptor */

    Object->Common.Type = (UINT8) Type;

    /* Init the reference count */

    Object->Common.ReferenceCount = 1;

    /* Any per-type initialization should go here */

    return_PTR (Object);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCreatePackageObject
 *
 * PARAMETERS:  Count               - Number of package elements
 *
 * RETURN:      Pointer to a new Package object, null on failure
 *
 * DESCRIPTION: Create a fully initialized package object
 *
 ******************************************************************************/

ACPI_OPERAND_OBJECT *
AcpiUtCreatePackageObject (
    UINT32                  Count)
{
    ACPI_OPERAND_OBJECT     *PackageDesc;
    ACPI_OPERAND_OBJECT     **PackageElements;


    ACPI_FUNCTION_TRACE_U32 (UtCreatePackageObject, Count);


    /* Create a new Package object */

    PackageDesc = AcpiUtCreateInternalObject (ACPI_TYPE_PACKAGE);
    if (!PackageDesc)
    {
        return_PTR (NULL);
    }

    /*
     * Create the element array. Count+1 allows the array to be null
     * terminated.
     */
    PackageElements = ACPI_ALLOCATE_ZEROED (
        ((ACPI_SIZE) Count + 1) * sizeof (void *));
    if (!PackageElements)
    {
        ACPI_FREE (PackageDesc);
        return_PTR (NULL);
    }

    PackageDesc->Package.Count = Count;
    PackageDesc->Package.Elements = PackageElements;
    return_PTR (PackageDesc);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCreateIntegerObject
 *
 * PARAMETERS:  InitialValue        - Initial value for the integer
 *
 * RETURN:      Pointer to a new Integer object, null on failure
 *
 * DESCRIPTION: Create an initialized integer object
 *
 ******************************************************************************/

ACPI_OPERAND_OBJECT *
AcpiUtCreateIntegerObject (
    UINT64                  InitialValue)
{
    ACPI_OPERAND_OBJECT     *IntegerDesc;


    ACPI_FUNCTION_TRACE (UtCreateIntegerObject);


    /* Create and initialize a new integer object */

    IntegerDesc = AcpiUtCreateInternalObject (ACPI_TYPE_INTEGER);
    if (!IntegerDesc)
    {
        return_PTR (NULL);
    }

    IntegerDesc->Integer.Value = InitialValue;
    return_PTR (IntegerDesc);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCreateBufferObject
 *
 * PARAMETERS:  BufferSize             - Size of buffer to be created
 *
 * RETURN:      Pointer to a new Buffer object, null on failure
 *
 * DESCRIPTION: Create a fully initialized buffer object
 *
 ******************************************************************************/

ACPI_OPERAND_OBJECT *
AcpiUtCreateBufferObject (
    ACPI_SIZE               BufferSize)
{
    ACPI_OPERAND_OBJECT     *BufferDesc;
    UINT8                   *Buffer = NULL;


    ACPI_FUNCTION_TRACE_U32 (UtCreateBufferObject, BufferSize);


    /* Create a new Buffer object */

    BufferDesc = AcpiUtCreateInternalObject (ACPI_TYPE_BUFFER);
    if (!BufferDesc)
    {
        return_PTR (NULL);
    }

    /* Create an actual buffer only if size > 0 */

    if (BufferSize > 0)
    {
        /* Allocate the actual buffer */

        Buffer = ACPI_ALLOCATE_ZEROED (BufferSize);
        if (!Buffer)
        {
            ACPI_ERROR ((AE_INFO, "Could not allocate size %u",
                (UINT32) BufferSize));

            AcpiUtRemoveReference (BufferDesc);
            return_PTR (NULL);
        }
    }

    /* Complete buffer object initialization */

    BufferDesc->Buffer.Flags |= AOPOBJ_DATA_VALID;
    BufferDesc->Buffer.Pointer = Buffer;
    BufferDesc->Buffer.Length = (UINT32) BufferSize;

    /* Return the new buffer descriptor */

    return_PTR (BufferDesc);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCreateStringObject
 *
 * PARAMETERS:  StringSize          - Size of string to be created. Does not
 *                                    include NULL terminator, this is added
 *                                    automatically.
 *
 * RETURN:      Pointer to a new String object
 *
 * DESCRIPTION: Create a fully initialized string object
 *
 ******************************************************************************/

ACPI_OPERAND_OBJECT *
AcpiUtCreateStringObject (
    ACPI_SIZE               StringSize)
{
    ACPI_OPERAND_OBJECT     *StringDesc;
    char                    *String;


    ACPI_FUNCTION_TRACE_U32 (UtCreateStringObject, StringSize);


    /* Create a new String object */

    StringDesc = AcpiUtCreateInternalObject (ACPI_TYPE_STRING);
    if (!StringDesc)
    {
        return_PTR (NULL);
    }

    /*
     * Allocate the actual string buffer -- (Size + 1) for NULL terminator.
     * NOTE: Zero-length strings are NULL terminated
     */
    String = ACPI_ALLOCATE_ZEROED (StringSize + 1);
    if (!String)
    {
        ACPI_ERROR ((AE_INFO, "Could not allocate size %u",
            (UINT32) StringSize));

        AcpiUtRemoveReference (StringDesc);
        return_PTR (NULL);
    }

    /* Complete string object initialization */

    StringDesc->String.Pointer = String;
    StringDesc->String.Length = (UINT32) StringSize;

    /* Return the new string descriptor */

    return_PTR (StringDesc);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtValidInternalObject
 *
 * PARAMETERS:  Object              - Object to be validated
 *
 * RETURN:      TRUE if object is valid, FALSE otherwise
 *
 * DESCRIPTION: Validate a pointer to be of type ACPI_OPERAND_OBJECT
 *
 ******************************************************************************/

BOOLEAN
AcpiUtValidInternalObject (
    void                    *Object)
{

    ACPI_FUNCTION_NAME (UtValidInternalObject);


    /* Check for a null pointer */

    if (!Object)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "**** Null Object Ptr\n"));
        return (FALSE);
    }

    /* Check the descriptor type field */

    switch (ACPI_GET_DESCRIPTOR_TYPE (Object))
    {
    case ACPI_DESC_TYPE_OPERAND:

        /* The object appears to be a valid ACPI_OPERAND_OBJECT */

        return (TRUE);

    default:

        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "%p is not an ACPI operand obj [%s]\n",
            Object, AcpiUtGetDescriptorName (Object)));
        break;
    }

    return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtAllocateObjectDescDbg
 *
 * PARAMETERS:  ModuleName          - Caller's module name (for error output)
 *              LineNumber          - Caller's line number (for error output)
 *              ComponentId         - Caller's component ID (for error output)
 *
 * RETURN:      Pointer to newly allocated object descriptor. Null on error
 *
 * DESCRIPTION: Allocate a new object descriptor. Gracefully handle
 *              error conditions.
 *
 ******************************************************************************/

void *
AcpiUtAllocateObjectDescDbg (
    const char              *ModuleName,
    UINT32                  LineNumber,
    UINT32                  ComponentId)
{
    ACPI_OPERAND_OBJECT     *Object;


    ACPI_FUNCTION_TRACE (UtAllocateObjectDescDbg);


    Object = AcpiOsAcquireObject (AcpiGbl_OperandCache);
    if (!Object)
    {
        ACPI_ERROR ((ModuleName, LineNumber,
            "Could not allocate an object descriptor"));

        return_PTR (NULL);
    }

    /* Mark the descriptor type */

    ACPI_SET_DESCRIPTOR_TYPE (Object, ACPI_DESC_TYPE_OPERAND);

    ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "%p Size %X\n",
        Object, (UINT32) sizeof (ACPI_OPERAND_OBJECT)));

    return_PTR (Object);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDeleteObjectDesc
 *
 * PARAMETERS:  Object          - An Acpi internal object to be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Free an ACPI object descriptor or add it to the object cache
 *
 ******************************************************************************/

void
AcpiUtDeleteObjectDesc (
    ACPI_OPERAND_OBJECT     *Object)
{
    ACPI_FUNCTION_TRACE_PTR (UtDeleteObjectDesc, Object);


    /* Object must be of type ACPI_OPERAND_OBJECT */

    if (ACPI_GET_DESCRIPTOR_TYPE (Object) != ACPI_DESC_TYPE_OPERAND)
    {
        ACPI_ERROR ((AE_INFO,
            "%p is not an ACPI Operand object [%s]", Object,
            AcpiUtGetDescriptorName (Object)));
        return_VOID;
    }

    (void) AcpiOsReleaseObject (AcpiGbl_OperandCache, Object);
    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetSimpleObjectSize
 *
 * PARAMETERS:  InternalObject     - An ACPI operand object
 *              ObjLength          - Where the length is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to determine the space required to
 *              contain a simple object for return to an external user.
 *
 *              The length includes the object structure plus any additional
 *              needed space.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtGetSimpleObjectSize (
    ACPI_OPERAND_OBJECT     *InternalObject,
    ACPI_SIZE               *ObjLength)
{
    ACPI_SIZE               Length;
    ACPI_SIZE               Size;
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE_PTR (UtGetSimpleObjectSize, InternalObject);


    /* Start with the length of the (external) Acpi object */

    Length = sizeof (ACPI_OBJECT);

    /* A NULL object is allowed, can be a legal uninitialized package element */

    if (!InternalObject)
    {
        /*
         * Object is NULL, just return the length of ACPI_OBJECT
         * (A NULL ACPI_OBJECT is an object of all zeroes.)
         */
        *ObjLength = ACPI_ROUND_UP_TO_NATIVE_WORD (Length);
        return_ACPI_STATUS (AE_OK);
    }

    /* A Namespace Node should never appear here */

    if (ACPI_GET_DESCRIPTOR_TYPE (InternalObject) == ACPI_DESC_TYPE_NAMED)
    {
        /* A namespace node should never get here */

        ACPI_ERROR ((AE_INFO,
            "Received a namespace node [%4.4s] "
            "where an operand object is required",
            ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, InternalObject)->Name.Ascii));
        return_ACPI_STATUS (AE_AML_INTERNAL);
    }

    /*
     * The final length depends on the object type
     * Strings and Buffers are packed right up against the parent object and
     * must be accessed bytewise or there may be alignment problems on
     * certain processors
     */
    switch (InternalObject->Common.Type)
    {
    case ACPI_TYPE_STRING:

        Length += (ACPI_SIZE) InternalObject->String.Length + 1;
        break;

    case ACPI_TYPE_BUFFER:

        Length += (ACPI_SIZE) InternalObject->Buffer.Length;
        break;

    case ACPI_TYPE_INTEGER:
    case ACPI_TYPE_PROCESSOR:
    case ACPI_TYPE_POWER:

        /* No extra data for these types */

        break;

    case ACPI_TYPE_LOCAL_REFERENCE:

        switch (InternalObject->Reference.Class)
        {
        case ACPI_REFCLASS_NAME:
            /*
             * Get the actual length of the full pathname to this object.
             * The reference will be converted to the pathname to the object
             */
            Size = AcpiNsGetPathnameLength (InternalObject->Reference.Node);
            if (!Size)
            {
                return_ACPI_STATUS (AE_BAD_PARAMETER);
            }

            Length += ACPI_ROUND_UP_TO_NATIVE_WORD (Size);
            break;

        default:
            /*
             * No other reference opcodes are supported.
             * Notably, Locals and Args are not supported, but this may be
             * required eventually.
             */
            ACPI_ERROR ((AE_INFO, "Cannot convert to external object - "
                "unsupported Reference Class [%s] 0x%X in object %p",
                AcpiUtGetReferenceName (InternalObject),
                InternalObject->Reference.Class, InternalObject));
            Status = AE_TYPE;
            break;
        }
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Cannot convert to external object - "
            "unsupported type [%s] 0x%X in object %p",
            AcpiUtGetObjectTypeName (InternalObject),
            InternalObject->Common.Type, InternalObject));
        Status = AE_TYPE;
        break;
    }

    /*
     * Account for the space required by the object rounded up to the next
     * multiple of the machine word size. This keeps each object aligned
     * on a machine word boundary. (preventing alignment faults on some
     * machines.)
     */
    *ObjLength = ACPI_ROUND_UP_TO_NATIVE_WORD (Length);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetElementLength
 *
 * PARAMETERS:  ACPI_PKG_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the length of one package element.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtGetElementLength (
    UINT8                   ObjectType,
    ACPI_OPERAND_OBJECT     *SourceObject,
    ACPI_GENERIC_STATE      *State,
    void                    *Context)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_PKG_INFO           *Info = (ACPI_PKG_INFO *) Context;
    ACPI_SIZE               ObjectSpace;


    switch (ObjectType)
    {
    case ACPI_COPY_TYPE_SIMPLE:
        /*
         * Simple object - just get the size (Null object/entry is handled
         * here also) and sum it into the running package length
         */
        Status = AcpiUtGetSimpleObjectSize (SourceObject, &ObjectSpace);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        Info->Length += ObjectSpace;
        break;

    case ACPI_COPY_TYPE_PACKAGE:

        /* Package object - nothing much to do here, let the walk handle it */

        Info->NumPackages++;
        State->Pkg.ThisTargetObj = NULL;
        break;

    default:

        /* No other types allowed */

        return (AE_BAD_PARAMETER);
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetPackageObjectSize
 *
 * PARAMETERS:  InternalObject      - An ACPI internal object
 *              ObjLength           - Where the length is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to determine the space required to
 *              contain a package object for return to an external user.
 *
 *              This is moderately complex since a package contains other
 *              objects including packages.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtGetPackageObjectSize (
    ACPI_OPERAND_OBJECT     *InternalObject,
    ACPI_SIZE               *ObjLength)
{
    ACPI_STATUS             Status;
    ACPI_PKG_INFO           Info;


    ACPI_FUNCTION_TRACE_PTR (UtGetPackageObjectSize, InternalObject);


    Info.Length = 0;
    Info.ObjectSpace = 0;
    Info.NumPackages = 1;

    Status = AcpiUtWalkPackageTree (
        InternalObject, NULL, AcpiUtGetElementLength, &Info);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /*
     * We have handled all of the objects in all levels of the package.
     * just add the length of the package objects themselves.
     * Round up to the next machine word.
     */
    Info.Length += ACPI_ROUND_UP_TO_NATIVE_WORD (
        sizeof (ACPI_OBJECT)) * (ACPI_SIZE) Info.NumPackages;

    /* Return the total package length */

    *ObjLength = Info.Length;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetObjectSize
 *
 * PARAMETERS:  InternalObject      - An ACPI internal object
 *              ObjLength           - Where the length will be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to determine the space required to
 *              contain an object for return to an API user.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtGetObjectSize (
    ACPI_OPERAND_OBJECT     *InternalObject,
    ACPI_SIZE               *ObjLength)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_ENTRY ();


    if ((ACPI_GET_DESCRIPTOR_TYPE (InternalObject) ==
            ACPI_DESC_TYPE_OPERAND) &&
        (InternalObject->Common.Type == ACPI_TYPE_PACKAGE))
    {
        Status = AcpiUtGetPackageObjectSize (InternalObject, ObjLength);
    }
    else
    {
        Status = AcpiUtGetSimpleObjectSize (InternalObject, ObjLength);
    }

    return (Status);
}
