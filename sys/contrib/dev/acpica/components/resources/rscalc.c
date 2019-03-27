/*******************************************************************************
 *
 * Module Name: rscalc - Calculate stream and list lengths
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
        ACPI_MODULE_NAME    ("rscalc")


/* Local prototypes */

static UINT8
AcpiRsCountSetBits (
    UINT16                  BitField);

static ACPI_RS_LENGTH
AcpiRsStructOptionLength (
    ACPI_RESOURCE_SOURCE    *ResourceSource);

static UINT32
AcpiRsStreamOptionLength (
    UINT32                  ResourceLength,
    UINT32                  MinimumTotalLength);


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsCountSetBits
 *
 * PARAMETERS:  BitField        - Field in which to count bits
 *
 * RETURN:      Number of bits set within the field
 *
 * DESCRIPTION: Count the number of bits set in a resource field. Used for
 *              (Short descriptor) interrupt and DMA lists.
 *
 ******************************************************************************/

static UINT8
AcpiRsCountSetBits (
    UINT16                  BitField)
{
    UINT8                   BitsSet;


    ACPI_FUNCTION_ENTRY ();


    for (BitsSet = 0; BitField; BitsSet++)
    {
        /* Zero the least significant bit that is set */

        BitField &= (UINT16) (BitField - 1);
    }

    return (BitsSet);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsStructOptionLength
 *
 * PARAMETERS:  ResourceSource      - Pointer to optional descriptor field
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Common code to handle optional ResourceSourceIndex and
 *              ResourceSource fields in some Large descriptors. Used during
 *              list-to-stream conversion
 *
 ******************************************************************************/

static ACPI_RS_LENGTH
AcpiRsStructOptionLength (
    ACPI_RESOURCE_SOURCE    *ResourceSource)
{
    ACPI_FUNCTION_ENTRY ();


    /*
     * If the ResourceSource string is valid, return the size of the string
     * (StringLength includes the NULL terminator) plus the size of the
     * ResourceSourceIndex (1).
     */
    if (ResourceSource->StringPtr)
    {
        return ((ACPI_RS_LENGTH) (ResourceSource->StringLength + 1));
    }

    return (0);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsStreamOptionLength
 *
 * PARAMETERS:  ResourceLength      - Length from the resource header
 *              MinimumTotalLength  - Minimum length of this resource, before
 *                                    any optional fields. Includes header size
 *
 * RETURN:      Length of optional string (0 if no string present)
 *
 * DESCRIPTION: Common code to handle optional ResourceSourceIndex and
 *              ResourceSource fields in some Large descriptors. Used during
 *              stream-to-list conversion
 *
 ******************************************************************************/

static UINT32
AcpiRsStreamOptionLength (
    UINT32                  ResourceLength,
    UINT32                  MinimumAmlResourceLength)
{
    UINT32                  StringLength = 0;


    ACPI_FUNCTION_ENTRY ();


    /*
     * The ResourceSourceIndex and ResourceSource are optional elements of
     * some Large-type resource descriptors.
     */

    /*
     * If the length of the actual resource descriptor is greater than the
     * ACPI spec-defined minimum length, it means that a ResourceSourceIndex
     * exists and is followed by a (required) null terminated string. The
     * string length (including the null terminator) is the resource length
     * minus the minimum length, minus one byte for the ResourceSourceIndex
     * itself.
     */
    if (ResourceLength > MinimumAmlResourceLength)
    {
        /* Compute the length of the optional string */

        StringLength = ResourceLength - MinimumAmlResourceLength - 1;
    }

    /*
     * Round the length up to a multiple of the native word in order to
     * guarantee that the entire resource descriptor is native word aligned
     */
    return ((UINT32) ACPI_ROUND_UP_TO_NATIVE_WORD (StringLength));
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsGetAmlLength
 *
 * PARAMETERS:  Resource            - Pointer to the resource linked list
 *              ResourceListSize    - Size of the resource linked list
 *              SizeNeeded          - Where the required size is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes a linked list of internal resource descriptors and
 *              calculates the size buffer needed to hold the corresponding
 *              external resource byte stream.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsGetAmlLength (
    ACPI_RESOURCE           *Resource,
    ACPI_SIZE               ResourceListSize,
    ACPI_SIZE               *SizeNeeded)
{
    ACPI_SIZE               AmlSizeNeeded = 0;
    ACPI_RESOURCE           *ResourceEnd;
    ACPI_RS_LENGTH          TotalSize;


    ACPI_FUNCTION_TRACE (RsGetAmlLength);


    /* Traverse entire list of internal resource descriptors */

    ResourceEnd = ACPI_ADD_PTR (ACPI_RESOURCE, Resource, ResourceListSize);
    while (Resource < ResourceEnd)
    {
        /* Validate the descriptor type */

        if (Resource->Type > ACPI_RESOURCE_TYPE_MAX)
        {
            return_ACPI_STATUS (AE_AML_INVALID_RESOURCE_TYPE);
        }

        /* Sanity check the length. It must not be zero, or we loop forever */

        if (!Resource->Length)
        {
            return_ACPI_STATUS (AE_AML_BAD_RESOURCE_LENGTH);
        }

        /* Get the base size of the (external stream) resource descriptor */

        TotalSize = AcpiGbl_AmlResourceSizes [Resource->Type];

        /*
         * Augment the base size for descriptors with optional and/or
         * variable-length fields
         */
        switch (Resource->Type)
        {
        case ACPI_RESOURCE_TYPE_IRQ:

            /* Length can be 3 or 2 */

            if (Resource->Data.Irq.DescriptorLength == 2)
            {
                TotalSize--;
            }
            break;


        case ACPI_RESOURCE_TYPE_START_DEPENDENT:

            /* Length can be 1 or 0 */

            if (Resource->Data.Irq.DescriptorLength == 0)
            {
                TotalSize--;
            }
            break;


        case ACPI_RESOURCE_TYPE_VENDOR:
            /*
             * Vendor Defined Resource:
             * For a Vendor Specific resource, if the Length is between 1 and 7
             * it will be created as a Small Resource data type, otherwise it
             * is a Large Resource data type.
             */
            if (Resource->Data.Vendor.ByteLength > 7)
            {
                /* Base size of a Large resource descriptor */

                TotalSize = sizeof (AML_RESOURCE_LARGE_HEADER);
            }

            /* Add the size of the vendor-specific data */

            TotalSize = (ACPI_RS_LENGTH)
                (TotalSize + Resource->Data.Vendor.ByteLength);
            break;


        case ACPI_RESOURCE_TYPE_END_TAG:
            /*
             * End Tag:
             * We are done -- return the accumulated total size.
             */
            *SizeNeeded = AmlSizeNeeded + TotalSize;

            /* Normal exit */

            return_ACPI_STATUS (AE_OK);


        case ACPI_RESOURCE_TYPE_ADDRESS16:
            /*
             * 16-Bit Address Resource:
             * Add the size of the optional ResourceSource info
             */
            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                AcpiRsStructOptionLength (
                    &Resource->Data.Address16.ResourceSource));
            break;


        case ACPI_RESOURCE_TYPE_ADDRESS32:
            /*
             * 32-Bit Address Resource:
             * Add the size of the optional ResourceSource info
             */
            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                AcpiRsStructOptionLength (
                    &Resource->Data.Address32.ResourceSource));
            break;


        case ACPI_RESOURCE_TYPE_ADDRESS64:
            /*
             * 64-Bit Address Resource:
             * Add the size of the optional ResourceSource info
             */
            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                AcpiRsStructOptionLength (
                    &Resource->Data.Address64.ResourceSource));
            break;


        case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
            /*
             * Extended IRQ Resource:
             * Add the size of each additional optional interrupt beyond the
             * required 1 (4 bytes for each UINT32 interrupt number)
             */
            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                ((Resource->Data.ExtendedIrq.InterruptCount - 1) * 4) +

                /* Add the size of the optional ResourceSource info */

                AcpiRsStructOptionLength (
                    &Resource->Data.ExtendedIrq.ResourceSource));
            break;


        case ACPI_RESOURCE_TYPE_GPIO:

            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                (Resource->Data.Gpio.PinTableLength * 2) +
                Resource->Data.Gpio.ResourceSource.StringLength +
                Resource->Data.Gpio.VendorLength);

            break;

        case ACPI_RESOURCE_TYPE_PIN_FUNCTION:

            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                (Resource->Data.PinFunction.PinTableLength * 2) +
                Resource->Data.PinFunction.ResourceSource.StringLength +
                Resource->Data.PinFunction.VendorLength);

            break;


        case ACPI_RESOURCE_TYPE_SERIAL_BUS:

            TotalSize = AcpiGbl_AmlResourceSerialBusSizes [
                Resource->Data.CommonSerialBus.Type];

            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                Resource->Data.I2cSerialBus.ResourceSource.StringLength +
                Resource->Data.I2cSerialBus.VendorLength);

            break;

        case ACPI_RESOURCE_TYPE_PIN_CONFIG:

            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                (Resource->Data.PinConfig.PinTableLength * 2) +
                Resource->Data.PinConfig.ResourceSource.StringLength +
                Resource->Data.PinConfig.VendorLength);

            break;

        case ACPI_RESOURCE_TYPE_PIN_GROUP:

            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                (Resource->Data.PinGroup.PinTableLength * 2) +
                Resource->Data.PinGroup.ResourceLabel.StringLength +
                Resource->Data.PinGroup.VendorLength);

            break;

        case ACPI_RESOURCE_TYPE_PIN_GROUP_FUNCTION:

            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                Resource->Data.PinGroupFunction.ResourceSource.StringLength +
                Resource->Data.PinGroupFunction.ResourceSourceLabel.StringLength +
                Resource->Data.PinGroupFunction.VendorLength);

            break;

        case ACPI_RESOURCE_TYPE_PIN_GROUP_CONFIG:

            TotalSize = (ACPI_RS_LENGTH) (TotalSize +
                Resource->Data.PinGroupConfig.ResourceSource.StringLength +
                Resource->Data.PinGroupConfig.ResourceSourceLabel.StringLength +
                Resource->Data.PinGroupConfig.VendorLength);

            break;

        default:

            break;
        }

        /* Update the total */

        AmlSizeNeeded += TotalSize;

        /* Point to the next object */

        Resource = ACPI_ADD_PTR (ACPI_RESOURCE, Resource, Resource->Length);
    }

    /* Did not find an EndTag resource descriptor */

    return_ACPI_STATUS (AE_AML_NO_RESOURCE_END_TAG);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsGetListLength
 *
 * PARAMETERS:  AmlBuffer           - Pointer to the resource byte stream
 *              AmlBufferLength     - Size of AmlBuffer
 *              SizeNeeded          - Where the size needed is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes an external resource byte stream and calculates the size
 *              buffer needed to hold the corresponding internal resource
 *              descriptor linked list.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsGetListLength (
    UINT8                   *AmlBuffer,
    UINT32                  AmlBufferLength,
    ACPI_SIZE               *SizeNeeded)
{
    ACPI_STATUS             Status;
    UINT8                   *EndAml;
    UINT8                   *Buffer;
    UINT32                  BufferSize;
    UINT16                  Temp16;
    UINT16                  ResourceLength;
    UINT32                  ExtraStructBytes;
    UINT8                   ResourceIndex;
    UINT8                   MinimumAmlResourceLength;
    AML_RESOURCE            *AmlResource;


    ACPI_FUNCTION_TRACE (RsGetListLength);


    *SizeNeeded = ACPI_RS_SIZE_MIN;         /* Minimum size is one EndTag */
    EndAml = AmlBuffer + AmlBufferLength;

    /* Walk the list of AML resource descriptors */

    while (AmlBuffer < EndAml)
    {
        /* Validate the Resource Type and Resource Length */

        Status = AcpiUtValidateResource (NULL, AmlBuffer, &ResourceIndex);
        if (ACPI_FAILURE (Status))
        {
            /*
             * Exit on failure. Cannot continue because the descriptor length
             * may be bogus also.
             */
            return_ACPI_STATUS (Status);
        }

        AmlResource = (void *) AmlBuffer;

        /* Get the resource length and base (minimum) AML size */

        ResourceLength = AcpiUtGetResourceLength (AmlBuffer);
        MinimumAmlResourceLength = AcpiGbl_ResourceAmlSizes[ResourceIndex];

        /*
         * Augment the size for descriptors with optional
         * and/or variable length fields
         */
        ExtraStructBytes = 0;
        Buffer = AmlBuffer + AcpiUtGetResourceHeaderLength (AmlBuffer);

        switch (AcpiUtGetResourceType (AmlBuffer))
        {
        case ACPI_RESOURCE_NAME_IRQ:
            /*
             * IRQ Resource:
             * Get the number of bits set in the 16-bit IRQ mask
             */
            ACPI_MOVE_16_TO_16 (&Temp16, Buffer);
            ExtraStructBytes = AcpiRsCountSetBits (Temp16);
            break;


        case ACPI_RESOURCE_NAME_DMA:
            /*
             * DMA Resource:
             * Get the number of bits set in the 8-bit DMA mask
             */
            ExtraStructBytes = AcpiRsCountSetBits (*Buffer);
            break;


        case ACPI_RESOURCE_NAME_VENDOR_SMALL:
        case ACPI_RESOURCE_NAME_VENDOR_LARGE:
            /*
             * Vendor Resource:
             * Get the number of vendor data bytes
             */
            ExtraStructBytes = ResourceLength;

            /*
             * There is already one byte included in the minimum
             * descriptor size. If there are extra struct bytes,
             * subtract one from the count.
             */
            if (ExtraStructBytes)
            {
                ExtraStructBytes--;
            }
            break;


        case ACPI_RESOURCE_NAME_END_TAG:
            /*
             * End Tag: This is the normal exit
             */
            return_ACPI_STATUS (AE_OK);


        case ACPI_RESOURCE_NAME_ADDRESS32:
        case ACPI_RESOURCE_NAME_ADDRESS16:
        case ACPI_RESOURCE_NAME_ADDRESS64:
            /*
             * Address Resource:
             * Add the size of the optional ResourceSource
             */
            ExtraStructBytes = AcpiRsStreamOptionLength (
                ResourceLength, MinimumAmlResourceLength);
            break;


        case ACPI_RESOURCE_NAME_EXTENDED_IRQ:
            /*
             * Extended IRQ Resource:
             * Using the InterruptTableLength, add 4 bytes for each additional
             * interrupt. Note: at least one interrupt is required and is
             * included in the minimum descriptor size (reason for the -1)
             */
            ExtraStructBytes = (Buffer[1] - 1) * sizeof (UINT32);

            /* Add the size of the optional ResourceSource */

            ExtraStructBytes += AcpiRsStreamOptionLength (
                ResourceLength - ExtraStructBytes, MinimumAmlResourceLength);
            break;

        case ACPI_RESOURCE_NAME_GPIO:

            /* Vendor data is optional */

            if (AmlResource->Gpio.VendorLength)
            {
                ExtraStructBytes +=
                    AmlResource->Gpio.VendorOffset -
                    AmlResource->Gpio.PinTableOffset +
                    AmlResource->Gpio.VendorLength;
            }
            else
            {
                ExtraStructBytes +=
                    AmlResource->LargeHeader.ResourceLength +
                    sizeof (AML_RESOURCE_LARGE_HEADER) -
                    AmlResource->Gpio.PinTableOffset;
            }
            break;

        case ACPI_RESOURCE_NAME_PIN_FUNCTION:

            /* Vendor data is optional */

            if (AmlResource->PinFunction.VendorLength)
            {
                ExtraStructBytes +=
                    AmlResource->PinFunction.VendorOffset -
                    AmlResource->PinFunction.PinTableOffset +
                    AmlResource->PinFunction.VendorLength;
            }
            else
            {
                ExtraStructBytes +=
                    AmlResource->LargeHeader.ResourceLength +
                    sizeof (AML_RESOURCE_LARGE_HEADER) -
                    AmlResource->PinFunction.PinTableOffset;
            }
            break;

        case ACPI_RESOURCE_NAME_SERIAL_BUS:

            MinimumAmlResourceLength = AcpiGbl_ResourceAmlSerialBusSizes[
                AmlResource->CommonSerialBus.Type];
            ExtraStructBytes +=
                AmlResource->CommonSerialBus.ResourceLength -
                MinimumAmlResourceLength;
            break;

        case ACPI_RESOURCE_NAME_PIN_CONFIG:

            /* Vendor data is optional */

            if (AmlResource->PinConfig.VendorLength)
            {
                ExtraStructBytes +=
                    AmlResource->PinConfig.VendorOffset -
                    AmlResource->PinConfig.PinTableOffset +
                    AmlResource->PinConfig.VendorLength;
            }
            else
            {
                ExtraStructBytes +=
                    AmlResource->LargeHeader.ResourceLength +
                    sizeof (AML_RESOURCE_LARGE_HEADER) -
                    AmlResource->PinConfig.PinTableOffset;
            }
            break;

        case ACPI_RESOURCE_NAME_PIN_GROUP:

            ExtraStructBytes +=
                AmlResource->PinGroup.VendorOffset -
                AmlResource->PinGroup.PinTableOffset +
                AmlResource->PinGroup.VendorLength;

            break;

        case ACPI_RESOURCE_NAME_PIN_GROUP_FUNCTION:

            ExtraStructBytes +=
                AmlResource->PinGroupFunction.VendorOffset -
                AmlResource->PinGroupFunction.ResSourceOffset +
                AmlResource->PinGroupFunction.VendorLength;

            break;

        case ACPI_RESOURCE_NAME_PIN_GROUP_CONFIG:

            ExtraStructBytes +=
                AmlResource->PinGroupConfig.VendorOffset -
                AmlResource->PinGroupConfig.ResSourceOffset +
                AmlResource->PinGroupConfig.VendorLength;

            break;

        default:

            break;
        }

        /*
         * Update the required buffer size for the internal descriptor structs
         *
         * Important: Round the size up for the appropriate alignment. This
         * is a requirement on IA64.
         */
        if (AcpiUtGetResourceType (AmlBuffer) ==
            ACPI_RESOURCE_NAME_SERIAL_BUS)
        {
            BufferSize = AcpiGbl_ResourceStructSerialBusSizes[
                AmlResource->CommonSerialBus.Type] + ExtraStructBytes;
        }
        else
        {
            BufferSize = AcpiGbl_ResourceStructSizes[ResourceIndex] +
                ExtraStructBytes;
        }

        BufferSize = (UINT32) ACPI_ROUND_UP_TO_NATIVE_WORD (BufferSize);
        *SizeNeeded += BufferSize;

        ACPI_DEBUG_PRINT ((ACPI_DB_RESOURCES,
            "Type %.2X, AmlLength %.2X InternalLength %.2X\n",
            AcpiUtGetResourceType (AmlBuffer),
            AcpiUtGetDescriptorLength (AmlBuffer), BufferSize));

        /*
         * Point to the next resource within the AML stream using the length
         * contained in the resource descriptor header
         */
        AmlBuffer += AcpiUtGetDescriptorLength (AmlBuffer);
    }

    /* Did not find an EndTag resource descriptor */

    return_ACPI_STATUS (AE_AML_NO_RESOURCE_END_TAG);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsGetPciRoutingTableLength
 *
 * PARAMETERS:  PackageObject           - Pointer to the package object
 *              BufferSizeNeeded        - UINT32 pointer of the size buffer
 *                                        needed to properly return the
 *                                        parsed data
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Given a package representing a PCI routing table, this
 *              calculates the size of the corresponding linked list of
 *              descriptions.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsGetPciRoutingTableLength (
    ACPI_OPERAND_OBJECT     *PackageObject,
    ACPI_SIZE               *BufferSizeNeeded)
{
    UINT32                  NumberOfElements;
    ACPI_SIZE               TempSizeNeeded = 0;
    ACPI_OPERAND_OBJECT     **TopObjectList;
    UINT32                  Index;
    ACPI_OPERAND_OBJECT     *PackageElement;
    ACPI_OPERAND_OBJECT     **SubObjectList;
    BOOLEAN                 NameFound;
    UINT32                  TableIndex;


    ACPI_FUNCTION_TRACE (RsGetPciRoutingTableLength);


    NumberOfElements = PackageObject->Package.Count;

    /*
     * Calculate the size of the return buffer.
     * The base size is the number of elements * the sizes of the
     * structures. Additional space for the strings is added below.
     * The minus one is to subtract the size of the UINT8 Source[1]
     * member because it is added below.
     *
     * But each PRT_ENTRY structure has a pointer to a string and
     * the size of that string must be found.
     */
    TopObjectList = PackageObject->Package.Elements;

    for (Index = 0; Index < NumberOfElements; Index++)
    {
        /* Dereference the subpackage */

        PackageElement = *TopObjectList;

        /* We must have a valid Package object */

        if (!PackageElement ||
            (PackageElement->Common.Type != ACPI_TYPE_PACKAGE))
        {
            return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
        }

        /*
         * The SubObjectList will now point to an array of the
         * four IRQ elements: Address, Pin, Source and SourceIndex
         */
        SubObjectList = PackageElement->Package.Elements;

        /* Scan the IrqTableElements for the Source Name String */

        NameFound = FALSE;

        for (TableIndex = 0;
             TableIndex < PackageElement->Package.Count && !NameFound;
             TableIndex++)
        {
            if (*SubObjectList && /* Null object allowed */

                ((ACPI_TYPE_STRING ==
                    (*SubObjectList)->Common.Type) ||

                ((ACPI_TYPE_LOCAL_REFERENCE ==
                    (*SubObjectList)->Common.Type) &&

                    ((*SubObjectList)->Reference.Class ==
                        ACPI_REFCLASS_NAME))))
            {
                NameFound = TRUE;
            }
            else
            {
                /* Look at the next element */

                SubObjectList++;
            }
        }

        TempSizeNeeded += (sizeof (ACPI_PCI_ROUTING_TABLE) - 4);

        /* Was a String type found? */

        if (NameFound)
        {
            if ((*SubObjectList)->Common.Type == ACPI_TYPE_STRING)
            {
                /*
                 * The length String.Length field does not include the
                 * terminating NULL, add 1
                 */
                TempSizeNeeded += ((ACPI_SIZE)
                    (*SubObjectList)->String.Length + 1);
            }
            else
            {
                TempSizeNeeded += AcpiNsGetPathnameLength (
                    (*SubObjectList)->Reference.Node);
            }
        }
        else
        {
            /*
             * If no name was found, then this is a NULL, which is
             * translated as a UINT32 zero.
             */
            TempSizeNeeded += sizeof (UINT32);
        }

        /* Round up the size since each element must be aligned */

        TempSizeNeeded = ACPI_ROUND_UP_TO_64BIT (TempSizeNeeded);

        /* Point to the next ACPI_OPERAND_OBJECT */

        TopObjectList++;
    }

    /*
     * Add an extra element to the end of the list, essentially a
     * NULL terminator
     */
    *BufferSizeNeeded = TempSizeNeeded + sizeof (ACPI_PCI_ROUTING_TABLE);
    return_ACPI_STATUS (AE_OK);
}
