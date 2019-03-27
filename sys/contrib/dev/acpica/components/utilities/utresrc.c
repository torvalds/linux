/*******************************************************************************
 *
 * Module Name: utresrc - Resource management utilities
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


#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utresrc")


/*
 * Base sizes of the raw AML resource descriptors, indexed by resource type.
 * Zero indicates a reserved (and therefore invalid) resource type.
 */
const UINT8                 AcpiGbl_ResourceAmlSizes[] =
{
    /* Small descriptors */

    0,
    0,
    0,
    0,
    ACPI_AML_SIZE_SMALL (AML_RESOURCE_IRQ),
    ACPI_AML_SIZE_SMALL (AML_RESOURCE_DMA),
    ACPI_AML_SIZE_SMALL (AML_RESOURCE_START_DEPENDENT),
    ACPI_AML_SIZE_SMALL (AML_RESOURCE_END_DEPENDENT),
    ACPI_AML_SIZE_SMALL (AML_RESOURCE_IO),
    ACPI_AML_SIZE_SMALL (AML_RESOURCE_FIXED_IO),
    ACPI_AML_SIZE_SMALL (AML_RESOURCE_FIXED_DMA),
    0,
    0,
    0,
    ACPI_AML_SIZE_SMALL (AML_RESOURCE_VENDOR_SMALL),
    ACPI_AML_SIZE_SMALL (AML_RESOURCE_END_TAG),

    /* Large descriptors */

    0,
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_MEMORY24),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_GENERIC_REGISTER),
    0,
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_VENDOR_LARGE),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_MEMORY32),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_FIXED_MEMORY32),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_ADDRESS32),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_ADDRESS16),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_EXTENDED_IRQ),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_ADDRESS64),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_EXTENDED_ADDRESS64),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_GPIO),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_PIN_FUNCTION),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_COMMON_SERIALBUS),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_PIN_CONFIG),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_PIN_GROUP),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_PIN_GROUP_FUNCTION),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_PIN_GROUP_CONFIG),
};

const UINT8                 AcpiGbl_ResourceAmlSerialBusSizes[] =
{
    0,
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_I2C_SERIALBUS),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_SPI_SERIALBUS),
    ACPI_AML_SIZE_LARGE (AML_RESOURCE_UART_SERIALBUS),
};


/*
 * Resource types, used to validate the resource length field.
 * The length of fixed-length types must match exactly, variable
 * lengths must meet the minimum required length, etc.
 * Zero indicates a reserved (and therefore invalid) resource type.
 */
static const UINT8          AcpiGbl_ResourceTypes[] =
{
    /* Small descriptors */

    0,
    0,
    0,
    0,
    ACPI_SMALL_VARIABLE_LENGTH,     /* 04 IRQ */
    ACPI_FIXED_LENGTH,              /* 05 DMA */
    ACPI_SMALL_VARIABLE_LENGTH,     /* 06 StartDependentFunctions */
    ACPI_FIXED_LENGTH,              /* 07 EndDependentFunctions */
    ACPI_FIXED_LENGTH,              /* 08 IO */
    ACPI_FIXED_LENGTH,              /* 09 FixedIO */
    ACPI_FIXED_LENGTH,              /* 0A FixedDMA */
    0,
    0,
    0,
    ACPI_VARIABLE_LENGTH,           /* 0E VendorShort */
    ACPI_FIXED_LENGTH,              /* 0F EndTag */

    /* Large descriptors */

    0,
    ACPI_FIXED_LENGTH,              /* 01 Memory24 */
    ACPI_FIXED_LENGTH,              /* 02 GenericRegister */
    0,
    ACPI_VARIABLE_LENGTH,           /* 04 VendorLong */
    ACPI_FIXED_LENGTH,              /* 05 Memory32 */
    ACPI_FIXED_LENGTH,              /* 06 Memory32Fixed */
    ACPI_VARIABLE_LENGTH,           /* 07 Dword* address */
    ACPI_VARIABLE_LENGTH,           /* 08 Word* address */
    ACPI_VARIABLE_LENGTH,           /* 09 ExtendedIRQ */
    ACPI_VARIABLE_LENGTH,           /* 0A Qword* address */
    ACPI_FIXED_LENGTH,              /* 0B Extended* address */
    ACPI_VARIABLE_LENGTH,           /* 0C Gpio* */
    ACPI_VARIABLE_LENGTH,           /* 0D PinFunction */
    ACPI_VARIABLE_LENGTH,           /* 0E *SerialBus */
    ACPI_VARIABLE_LENGTH,           /* 0F PinConfig */
    ACPI_VARIABLE_LENGTH,           /* 10 PinGroup */
    ACPI_VARIABLE_LENGTH,           /* 11 PinGroupFunction */
    ACPI_VARIABLE_LENGTH,           /* 12 PinGroupConfig */
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtWalkAmlResources
 *
 * PARAMETERS:  WalkState           - Current walk info
 * PARAMETERS:  Aml                 - Pointer to the raw AML resource template
 *              AmlLength           - Length of the entire template
 *              UserFunction        - Called once for each descriptor found. If
 *                                    NULL, a pointer to the EndTag is returned
 *              Context             - Passed to UserFunction
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk a raw AML resource list(buffer). User function called
 *              once for each resource found.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtWalkAmlResources (
    ACPI_WALK_STATE         *WalkState,
    UINT8                   *Aml,
    ACPI_SIZE               AmlLength,
    ACPI_WALK_AML_CALLBACK  UserFunction,
    void                    **Context)
{
    ACPI_STATUS             Status;
    UINT8                   *EndAml;
    UINT8                   ResourceIndex;
    UINT32                  Length;
    UINT32                  Offset = 0;
    UINT8                   EndTag[2] = {0x79, 0x00};


    ACPI_FUNCTION_TRACE (UtWalkAmlResources);


    /* The absolute minimum resource template is one EndTag descriptor */

    if (AmlLength < sizeof (AML_RESOURCE_END_TAG))
    {
        return_ACPI_STATUS (AE_AML_NO_RESOURCE_END_TAG);
    }

    /* Point to the end of the resource template buffer */

    EndAml = Aml + AmlLength;

    /* Walk the byte list, abort on any invalid descriptor type or length */

    while (Aml < EndAml)
    {
        /* Validate the Resource Type and Resource Length */

        Status = AcpiUtValidateResource (WalkState, Aml, &ResourceIndex);
        if (ACPI_FAILURE (Status))
        {
            /*
             * Exit on failure. Cannot continue because the descriptor
             * length may be bogus also.
             */
            return_ACPI_STATUS (Status);
        }

        /* Get the length of this descriptor */

        Length = AcpiUtGetDescriptorLength (Aml);

        /* Invoke the user function */

        if (UserFunction)
        {
            Status = UserFunction (
                Aml, Length, Offset, ResourceIndex, Context);
            if (ACPI_FAILURE (Status))
            {
                return_ACPI_STATUS (Status);
            }
        }

        /* An EndTag descriptor terminates this resource template */

        if (AcpiUtGetResourceType (Aml) == ACPI_RESOURCE_NAME_END_TAG)
        {
            /*
             * There must be at least one more byte in the buffer for
             * the 2nd byte of the EndTag
             */
            if ((Aml + 1) >= EndAml)
            {
                return_ACPI_STATUS (AE_AML_NO_RESOURCE_END_TAG);
            }

            /*
             * Don't attempt to perform any validation on the 2nd byte.
             * Although all known ASL compilers insert a zero for the 2nd
             * byte, it can also be a checksum (as per the ACPI spec),
             * and this is occasionally seen in the field. July 2017.
             */

            /* Return the pointer to the EndTag if requested */

            if (!UserFunction)
            {
                *Context = Aml;
            }

            /* Normal exit */

            return_ACPI_STATUS (AE_OK);
        }

        Aml += Length;
        Offset += Length;
    }

    /* Did not find an EndTag descriptor */

    if (UserFunction)
    {
        /* Insert an EndTag anyway. AcpiRsGetListLength always leaves room */

        (void) AcpiUtValidateResource (WalkState, EndTag, &ResourceIndex);
        Status = UserFunction (EndTag, 2, Offset, ResourceIndex, Context);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }

    return_ACPI_STATUS (AE_AML_NO_RESOURCE_END_TAG);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtValidateResource
 *
 * PARAMETERS:  WalkState           - Current walk info
 *              Aml                 - Pointer to the raw AML resource descriptor
 *              ReturnIndex         - Where the resource index is returned. NULL
 *                                    if the index is not required.
 *
 * RETURN:      Status, and optionally the Index into the global resource tables
 *
 * DESCRIPTION: Validate an AML resource descriptor by checking the Resource
 *              Type and Resource Length. Returns an index into the global
 *              resource information/dispatch tables for later use.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtValidateResource (
    ACPI_WALK_STATE         *WalkState,
    void                    *Aml,
    UINT8                   *ReturnIndex)
{
    AML_RESOURCE            *AmlResource;
    UINT8                   ResourceType;
    UINT8                   ResourceIndex;
    ACPI_RS_LENGTH          ResourceLength;
    ACPI_RS_LENGTH          MinimumResourceLength;


    ACPI_FUNCTION_ENTRY ();


    /*
     * 1) Validate the ResourceType field (Byte 0)
     */
    ResourceType = ACPI_GET8 (Aml);

    /*
     * Byte 0 contains the descriptor name (Resource Type)
     * Examine the large/small bit in the resource header
     */
    if (ResourceType & ACPI_RESOURCE_NAME_LARGE)
    {
        /* Verify the large resource type (name) against the max */

        if (ResourceType > ACPI_RESOURCE_NAME_LARGE_MAX)
        {
            goto InvalidResource;
        }

        /*
         * Large Resource Type -- bits 6:0 contain the name
         * Translate range 0x80-0x8B to index range 0x10-0x1B
         */
        ResourceIndex = (UINT8) (ResourceType - 0x70);
    }
    else
    {
        /*
         * Small Resource Type -- bits 6:3 contain the name
         * Shift range to index range 0x00-0x0F
         */
        ResourceIndex = (UINT8)
            ((ResourceType & ACPI_RESOURCE_NAME_SMALL_MASK) >> 3);
    }

    /*
     * Check validity of the resource type, via AcpiGbl_ResourceTypes.
     * Zero indicates an invalid resource.
     */
    if (!AcpiGbl_ResourceTypes[ResourceIndex])
    {
        goto InvalidResource;
    }

    /*
     * Validate the ResourceLength field. This ensures that the length
     * is at least reasonable, and guarantees that it is non-zero.
     */
    ResourceLength = AcpiUtGetResourceLength (Aml);
    MinimumResourceLength = AcpiGbl_ResourceAmlSizes[ResourceIndex];

    /* Validate based upon the type of resource - fixed length or variable */

    switch (AcpiGbl_ResourceTypes[ResourceIndex])
    {
    case ACPI_FIXED_LENGTH:

        /* Fixed length resource, length must match exactly */

        if (ResourceLength != MinimumResourceLength)
        {
            goto BadResourceLength;
        }
        break;

    case ACPI_VARIABLE_LENGTH:

        /* Variable length resource, length must be at least the minimum */

        if (ResourceLength < MinimumResourceLength)
        {
            goto BadResourceLength;
        }
        break;

    case ACPI_SMALL_VARIABLE_LENGTH:

        /* Small variable length resource, length can be (Min) or (Min-1) */

        if ((ResourceLength > MinimumResourceLength) ||
            (ResourceLength < (MinimumResourceLength - 1)))
        {
            goto BadResourceLength;
        }
        break;

    default:

        /* Shouldn't happen (because of validation earlier), but be sure */

        goto InvalidResource;
    }

    AmlResource = ACPI_CAST_PTR (AML_RESOURCE, Aml);
    if (ResourceType == ACPI_RESOURCE_NAME_SERIAL_BUS)
    {
        /* Validate the BusType field */

        if ((AmlResource->CommonSerialBus.Type == 0) ||
            (AmlResource->CommonSerialBus.Type > AML_RESOURCE_MAX_SERIALBUSTYPE))
        {
            if (WalkState)
            {
                ACPI_ERROR ((AE_INFO,
                    "Invalid/unsupported SerialBus resource descriptor: BusType 0x%2.2X",
                    AmlResource->CommonSerialBus.Type));
            }
            return (AE_AML_INVALID_RESOURCE_TYPE);
        }
    }

    /* Optionally return the resource table index */

    if (ReturnIndex)
    {
        *ReturnIndex = ResourceIndex;
    }

    return (AE_OK);


InvalidResource:

    if (WalkState)
    {
        ACPI_ERROR ((AE_INFO,
            "Invalid/unsupported resource descriptor: Type 0x%2.2X",
            ResourceType));
    }
    return (AE_AML_INVALID_RESOURCE_TYPE);

BadResourceLength:

    if (WalkState)
    {
        ACPI_ERROR ((AE_INFO,
            "Invalid resource descriptor length: Type "
            "0x%2.2X, Length 0x%4.4X, MinLength 0x%4.4X",
            ResourceType, ResourceLength, MinimumResourceLength));
    }
    return (AE_AML_BAD_RESOURCE_LENGTH);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetResourceType
 *
 * PARAMETERS:  Aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      The Resource Type with no extraneous bits (except the
 *              Large/Small descriptor bit -- this is left alone)
 *
 * DESCRIPTION: Extract the Resource Type/Name from the first byte of
 *              a resource descriptor.
 *
 ******************************************************************************/

UINT8
AcpiUtGetResourceType (
    void                    *Aml)
{
    ACPI_FUNCTION_ENTRY ();


    /*
     * Byte 0 contains the descriptor name (Resource Type)
     * Examine the large/small bit in the resource header
     */
    if (ACPI_GET8 (Aml) & ACPI_RESOURCE_NAME_LARGE)
    {
        /* Large Resource Type -- bits 6:0 contain the name */

        return (ACPI_GET8 (Aml));
    }
    else
    {
        /* Small Resource Type -- bits 6:3 contain the name */

        return ((UINT8) (ACPI_GET8 (Aml) & ACPI_RESOURCE_NAME_SMALL_MASK));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetResourceLength
 *
 * PARAMETERS:  Aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      Byte Length
 *
 * DESCRIPTION: Get the "Resource Length" of a raw AML descriptor. By
 *              definition, this does not include the size of the descriptor
 *              header or the length field itself.
 *
 ******************************************************************************/

UINT16
AcpiUtGetResourceLength (
    void                    *Aml)
{
    ACPI_RS_LENGTH          ResourceLength;


    ACPI_FUNCTION_ENTRY ();


    /*
     * Byte 0 contains the descriptor name (Resource Type)
     * Examine the large/small bit in the resource header
     */
    if (ACPI_GET8 (Aml) & ACPI_RESOURCE_NAME_LARGE)
    {
        /* Large Resource type -- bytes 1-2 contain the 16-bit length */

        ACPI_MOVE_16_TO_16 (&ResourceLength, ACPI_ADD_PTR (UINT8, Aml, 1));

    }
    else
    {
        /* Small Resource type -- bits 2:0 of byte 0 contain the length */

        ResourceLength = (UINT16) (ACPI_GET8 (Aml) &
            ACPI_RESOURCE_NAME_SMALL_LENGTH_MASK);
    }

    return (ResourceLength);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetResourceHeaderLength
 *
 * PARAMETERS:  Aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      Length of the AML header (depends on large/small descriptor)
 *
 * DESCRIPTION: Get the length of the header for this resource.
 *
 ******************************************************************************/

UINT8
AcpiUtGetResourceHeaderLength (
    void                    *Aml)
{
    ACPI_FUNCTION_ENTRY ();


    /* Examine the large/small bit in the resource header */

    if (ACPI_GET8 (Aml) & ACPI_RESOURCE_NAME_LARGE)
    {
        return (sizeof (AML_RESOURCE_LARGE_HEADER));
    }
    else
    {
        return (sizeof (AML_RESOURCE_SMALL_HEADER));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetDescriptorLength
 *
 * PARAMETERS:  Aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      Byte length
 *
 * DESCRIPTION: Get the total byte length of a raw AML descriptor, including the
 *              length of the descriptor header and the length field itself.
 *              Used to walk descriptor lists.
 *
 ******************************************************************************/

UINT32
AcpiUtGetDescriptorLength (
    void                    *Aml)
{
    ACPI_FUNCTION_ENTRY ();


    /*
     * Get the Resource Length (does not include header length) and add
     * the header length (depends on if this is a small or large resource)
     */
    return (AcpiUtGetResourceLength (Aml) +
        AcpiUtGetResourceHeaderLength (Aml));
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetResourceEndTag
 *
 * PARAMETERS:  ObjDesc         - The resource template buffer object
 *              EndTag          - Where the pointer to the EndTag is returned
 *
 * RETURN:      Status, pointer to the end tag
 *
 * DESCRIPTION: Find the EndTag resource descriptor in an AML resource template
 *              Note: allows a buffer length of zero.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtGetResourceEndTag (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    UINT8                   **EndTag)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (UtGetResourceEndTag);


    /* Allow a buffer length of zero */

    if (!ObjDesc->Buffer.Length)
    {
        *EndTag = ObjDesc->Buffer.Pointer;
        return_ACPI_STATUS (AE_OK);
    }

    /* Validate the template and get a pointer to the EndTag */

    Status = AcpiUtWalkAmlResources (NULL, ObjDesc->Buffer.Pointer,
        ObjDesc->Buffer.Length, NULL, (void **) EndTag);

    return_ACPI_STATUS (Status);
}
