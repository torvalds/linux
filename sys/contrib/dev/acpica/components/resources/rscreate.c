/*******************************************************************************
 *
 * Module Name: rscreate - Create resource lists/tables
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
#include <contrib/dev/acpica/include/acresrc.h>
#include <contrib/dev/acpica/include/acnamesp.h>

#define _COMPONENT          ACPI_RESOURCES
        ACPI_MODULE_NAME    ("rscreate")


/*******************************************************************************
 *
 * FUNCTION:    AcpiBufferToResource
 *
 * PARAMETERS:  AmlBuffer           - Pointer to the resource byte stream
 *              AmlBufferLength     - Length of the AmlBuffer
 *              ResourcePtr         - Where the converted resource is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a raw AML buffer to a resource list
 *
 ******************************************************************************/

ACPI_STATUS
AcpiBufferToResource (
    UINT8                   *AmlBuffer,
    UINT16                  AmlBufferLength,
    ACPI_RESOURCE           **ResourcePtr)
{
    ACPI_STATUS             Status;
    ACPI_SIZE               ListSizeNeeded;
    void                    *Resource;
    void                    *CurrentResourcePtr;


    ACPI_FUNCTION_TRACE (AcpiBufferToResource);


    /*
     * Note: we allow AE_AML_NO_RESOURCE_END_TAG, since an end tag
     * is not required here.
     */

    /* Get the required length for the converted resource */

    Status = AcpiRsGetListLength (
        AmlBuffer, AmlBufferLength, &ListSizeNeeded);
    if (Status == AE_AML_NO_RESOURCE_END_TAG)
    {
        Status = AE_OK;
    }
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Allocate a buffer for the converted resource */

    Resource = ACPI_ALLOCATE_ZEROED (ListSizeNeeded);
    CurrentResourcePtr = Resource;
    if (!Resource)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Perform the AML-to-Resource conversion */

    Status = AcpiUtWalkAmlResources (NULL, AmlBuffer, AmlBufferLength,
        AcpiRsConvertAmlToResources, &CurrentResourcePtr);
    if (Status == AE_AML_NO_RESOURCE_END_TAG)
    {
        Status = AE_OK;
    }
    if (ACPI_FAILURE (Status))
    {
        ACPI_FREE (Resource);
    }
    else
    {
        *ResourcePtr = Resource;
    }

    return_ACPI_STATUS (Status);
}

ACPI_EXPORT_SYMBOL (AcpiBufferToResource)


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsCreateResourceList
 *
 * PARAMETERS:  AmlBuffer           - Pointer to the resource byte stream
 *              OutputBuffer        - Pointer to the user's buffer
 *
 * RETURN:      Status: AE_OK if okay, else a valid ACPI_STATUS code
 *              If OutputBuffer is not large enough, OutputBufferLength
 *              indicates how large OutputBuffer should be, else it
 *              indicates how may UINT8 elements of OutputBuffer are valid.
 *
 * DESCRIPTION: Takes the byte stream returned from a _CRS, _PRS control method
 *              execution and parses the stream to create a linked list
 *              of device resources.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsCreateResourceList (
    ACPI_OPERAND_OBJECT     *AmlBuffer,
    ACPI_BUFFER             *OutputBuffer)
{

    ACPI_STATUS             Status;
    UINT8                   *AmlStart;
    ACPI_SIZE               ListSizeNeeded = 0;
    UINT32                  AmlBufferLength;
    void                    *Resource;


    ACPI_FUNCTION_TRACE (RsCreateResourceList);


    ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "AmlBuffer = %p\n",
        AmlBuffer));

    /* Params already validated, so we don't re-validate here */

    AmlBufferLength = AmlBuffer->Buffer.Length;
    AmlStart = AmlBuffer->Buffer.Pointer;

    /*
     * Pass the AmlBuffer into a module that can calculate
     * the buffer size needed for the linked list
     */
    Status = AcpiRsGetListLength (AmlStart, AmlBufferLength,
                &ListSizeNeeded);

    ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Status=%X ListSizeNeeded=%X\n",
        Status, (UINT32) ListSizeNeeded));
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Validate/Allocate/Clear caller buffer */

    Status = AcpiUtInitializeBuffer (OutputBuffer, ListSizeNeeded);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Do the conversion */

    Resource = OutputBuffer->Pointer;
    Status = AcpiUtWalkAmlResources (NULL, AmlStart, AmlBufferLength,
        AcpiRsConvertAmlToResources, &Resource);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "OutputBuffer %p Length %X\n",
        OutputBuffer->Pointer, (UINT32) OutputBuffer->Length));
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsCreatePciRoutingTable
 *
 * PARAMETERS:  PackageObject           - Pointer to a package containing one
 *                                        of more ACPI_OPERAND_OBJECTs
 *              OutputBuffer            - Pointer to the user's buffer
 *
 * RETURN:      Status  AE_OK if okay, else a valid ACPI_STATUS code.
 *              If the OutputBuffer is too small, the error will be
 *              AE_BUFFER_OVERFLOW and OutputBuffer->Length will point
 *              to the size buffer needed.
 *
 * DESCRIPTION: Takes the ACPI_OPERAND_OBJECT package and creates a
 *              linked list of PCI interrupt descriptions
 *
 * NOTE: It is the caller's responsibility to ensure that the start of the
 * output buffer is aligned properly (if necessary).
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsCreatePciRoutingTable (
    ACPI_OPERAND_OBJECT     *PackageObject,
    ACPI_BUFFER             *OutputBuffer)
{
    UINT8                   *Buffer;
    ACPI_OPERAND_OBJECT     **TopObjectList;
    ACPI_OPERAND_OBJECT     **SubObjectList;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_SIZE               BufferSizeNeeded = 0;
    UINT32                  NumberOfElements;
    UINT32                  Index;
    ACPI_PCI_ROUTING_TABLE  *UserPrt;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_STATUS             Status;
    ACPI_BUFFER             PathBuffer;


    ACPI_FUNCTION_TRACE (RsCreatePciRoutingTable);


    /* Params already validated, so we don't re-validate here */

    /* Get the required buffer length */

    Status = AcpiRsGetPciRoutingTableLength (
        PackageObject,&BufferSizeNeeded);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "BufferSizeNeeded = %X\n",
        (UINT32) BufferSizeNeeded));

    /* Validate/Allocate/Clear caller buffer */

    Status = AcpiUtInitializeBuffer (OutputBuffer, BufferSizeNeeded);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /*
     * Loop through the ACPI_INTERNAL_OBJECTS - Each object should be a
     * package that in turn contains an UINT64 Address, a UINT8 Pin,
     * a Name, and a UINT8 SourceIndex.
     */
    TopObjectList = PackageObject->Package.Elements;
    NumberOfElements = PackageObject->Package.Count;
    Buffer = OutputBuffer->Pointer;
    UserPrt = ACPI_CAST_PTR (ACPI_PCI_ROUTING_TABLE, Buffer);

    for (Index = 0; Index < NumberOfElements; Index++)
    {
        /*
         * Point UserPrt past this current structure
         *
         * NOTE: On the first iteration, UserPrt->Length will
         * be zero because we cleared the return buffer earlier
         */
        Buffer += UserPrt->Length;
        UserPrt = ACPI_CAST_PTR (ACPI_PCI_ROUTING_TABLE, Buffer);

        /*
         * Fill in the Length field with the information we have at this
         * point. The minus four is to subtract the size of the UINT8
         * Source[4] member because it is added below.
         */
        UserPrt->Length = (sizeof (ACPI_PCI_ROUTING_TABLE) - 4);

        /* Each subpackage must be of length 4 */

        if ((*TopObjectList)->Package.Count != 4)
        {
            ACPI_ERROR ((AE_INFO,
                "(PRT[%u]) Need package of length 4, found length %u",
                Index, (*TopObjectList)->Package.Count));
            return_ACPI_STATUS (AE_AML_PACKAGE_LIMIT);
        }

        /*
         * Dereference the subpackage.
         * The SubObjectList will now point to an array of the four IRQ
         * elements: [Address, Pin, Source, SourceIndex]
         */
        SubObjectList = (*TopObjectList)->Package.Elements;

        /* 1) First subobject: Dereference the PRT.Address */

        ObjDesc = SubObjectList[0];
        if (!ObjDesc || ObjDesc->Common.Type != ACPI_TYPE_INTEGER)
        {
            ACPI_ERROR ((AE_INFO,
                "(PRT[%u].Address) Need Integer, found %s",
                Index, AcpiUtGetObjectTypeName (ObjDesc)));
            return_ACPI_STATUS (AE_BAD_DATA);
        }

        UserPrt->Address = ObjDesc->Integer.Value;

        /* 2) Second subobject: Dereference the PRT.Pin */

        ObjDesc = SubObjectList[1];
        if (!ObjDesc || ObjDesc->Common.Type != ACPI_TYPE_INTEGER)
        {
            ACPI_ERROR ((AE_INFO, "(PRT[%u].Pin) Need Integer, found %s",
                Index, AcpiUtGetObjectTypeName (ObjDesc)));
            return_ACPI_STATUS (AE_BAD_DATA);
        }

        UserPrt->Pin = (UINT32) ObjDesc->Integer.Value;

        /*
         * 3) Third subobject: Dereference the PRT.SourceName
         * The name may be unresolved (slack mode), so allow a null object
         */
        ObjDesc = SubObjectList[2];
        if (ObjDesc)
        {
            switch (ObjDesc->Common.Type)
            {
            case ACPI_TYPE_LOCAL_REFERENCE:

                if (ObjDesc->Reference.Class != ACPI_REFCLASS_NAME)
                {
                    ACPI_ERROR ((AE_INFO,
                        "(PRT[%u].Source) Need name, found Reference Class 0x%X",
                        Index, ObjDesc->Reference.Class));
                    return_ACPI_STATUS (AE_BAD_DATA);
                }

                Node = ObjDesc->Reference.Node;

                /* Use *remaining* length of the buffer as max for pathname */

                PathBuffer.Length = OutputBuffer->Length -
                    (UINT32) ((UINT8 *) UserPrt->Source -
                    (UINT8 *) OutputBuffer->Pointer);
                PathBuffer.Pointer = UserPrt->Source;

                Status = AcpiNsHandleToPathname (
                    (ACPI_HANDLE) Node, &PathBuffer, FALSE);

                /* +1 to include null terminator */

                UserPrt->Length += (UINT32) strlen (UserPrt->Source) + 1;
                break;

            case ACPI_TYPE_STRING:

                strcpy (UserPrt->Source, ObjDesc->String.Pointer);

                /*
                 * Add to the Length field the length of the string
                 * (add 1 for terminator)
                 */
                UserPrt->Length += ObjDesc->String.Length + 1;
                break;

            case ACPI_TYPE_INTEGER:
                /*
                 * If this is a number, then the Source Name is NULL, since
                 * the entire buffer was zeroed out, we can leave this alone.
                 *
                 * Add to the Length field the length of the UINT32 NULL
                 */
                UserPrt->Length += sizeof (UINT32);
                break;

            default:

               ACPI_ERROR ((AE_INFO,
                   "(PRT[%u].Source) Need Ref/String/Integer, found %s",
                   Index, AcpiUtGetObjectTypeName (ObjDesc)));
               return_ACPI_STATUS (AE_BAD_DATA);
            }
        }

        /* Now align the current length */

        UserPrt->Length = (UINT32) ACPI_ROUND_UP_TO_64BIT (UserPrt->Length);

        /* 4) Fourth subobject: Dereference the PRT.SourceIndex */

        ObjDesc = SubObjectList[3];
        if (!ObjDesc || ObjDesc->Common.Type != ACPI_TYPE_INTEGER)
        {
            ACPI_ERROR ((AE_INFO,
                "(PRT[%u].SourceIndex) Need Integer, found %s",
                Index, AcpiUtGetObjectTypeName (ObjDesc)));
            return_ACPI_STATUS (AE_BAD_DATA);
        }

        UserPrt->SourceIndex = (UINT32) ObjDesc->Integer.Value;

        /* Point to the next ACPI_OPERAND_OBJECT in the top level package */

        TopObjectList++;
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "OutputBuffer %p Length %X\n",
        OutputBuffer->Pointer, (UINT32) OutputBuffer->Length));
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsCreateAmlResources
 *
 * PARAMETERS:  ResourceList            - Pointer to the resource list buffer
 *              OutputBuffer            - Where the AML buffer is returned
 *
 * RETURN:      Status  AE_OK if okay, else a valid ACPI_STATUS code.
 *              If the OutputBuffer is too small, the error will be
 *              AE_BUFFER_OVERFLOW and OutputBuffer->Length will point
 *              to the size buffer needed.
 *
 * DESCRIPTION: Converts a list of device resources to an AML bytestream
 *              to be used as input for the _SRS control method.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsCreateAmlResources (
    ACPI_BUFFER             *ResourceList,
    ACPI_BUFFER             *OutputBuffer)
{
    ACPI_STATUS             Status;
    ACPI_SIZE               AmlSizeNeeded = 0;


    ACPI_FUNCTION_TRACE (RsCreateAmlResources);


    /* Params already validated, no need to re-validate here */

    ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "ResourceList Buffer = %p\n",
        ResourceList->Pointer));

    /* Get the buffer size needed for the AML byte stream */

    Status = AcpiRsGetAmlLength (
        ResourceList->Pointer, ResourceList->Length, &AmlSizeNeeded);

    ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "AmlSizeNeeded=%X, %s\n",
        (UINT32) AmlSizeNeeded, AcpiFormatException (Status)));
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Validate/Allocate/Clear caller buffer */

    Status = AcpiUtInitializeBuffer (OutputBuffer, AmlSizeNeeded);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Do the conversion */

    Status = AcpiRsConvertResourcesToAml (ResourceList->Pointer,
        AmlSizeNeeded, OutputBuffer->Pointer);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "OutputBuffer %p Length %X\n",
        OutputBuffer->Pointer, (UINT32) OutputBuffer->Length));
    return_ACPI_STATUS (AE_OK);
}
