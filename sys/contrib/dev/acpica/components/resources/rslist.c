/*******************************************************************************
 *
 * Module Name: rslist - Linked list utilities
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

#define _COMPONENT          ACPI_RESOURCES
        ACPI_MODULE_NAME    ("rslist")


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsConvertAmlToResources
 *
 * PARAMETERS:  ACPI_WALK_AML_CALLBACK
 *              ResourcePtr             - Pointer to the buffer that will
 *                                        contain the output structures
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an AML resource to an internal representation of the
 *              resource that is aligned and easier to access.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsConvertAmlToResources (
    UINT8                   *Aml,
    UINT32                  Length,
    UINT32                  Offset,
    UINT8                   ResourceIndex,
    void                    **Context)
{
    ACPI_RESOURCE           **ResourcePtr = ACPI_CAST_INDIRECT_PTR (
                                ACPI_RESOURCE, Context);
    ACPI_RESOURCE           *Resource;
    AML_RESOURCE            *AmlResource;
    ACPI_RSCONVERT_INFO     *ConversionTable;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (RsConvertAmlToResources);


    /*
     * Check that the input buffer and all subsequent pointers into it
     * are aligned on a native word boundary. Most important on IA64
     */
    Resource = *ResourcePtr;
    if (ACPI_IS_MISALIGNED (Resource))
    {
        ACPI_WARNING ((AE_INFO,
            "Misaligned resource pointer %p", Resource));
    }

    /* Get the appropriate conversion info table */

    AmlResource = ACPI_CAST_PTR (AML_RESOURCE, Aml);

    if (AcpiUtGetResourceType (Aml) ==
        ACPI_RESOURCE_NAME_SERIAL_BUS)
    {
        if (AmlResource->CommonSerialBus.Type >
            AML_RESOURCE_MAX_SERIALBUSTYPE)
        {
            ConversionTable = NULL;
        }
        else
        {
            /* This is an I2C, SPI, or UART SerialBus descriptor */

            ConversionTable = AcpiGbl_ConvertResourceSerialBusDispatch [
                AmlResource->CommonSerialBus.Type];
        }
    }
    else
    {
        ConversionTable = AcpiGbl_GetResourceDispatch[ResourceIndex];
    }

    if (!ConversionTable)
    {
        ACPI_ERROR ((AE_INFO,
            "Invalid/unsupported resource descriptor: Type 0x%2.2X",
            ResourceIndex));
        return_ACPI_STATUS (AE_AML_INVALID_RESOURCE_TYPE);
    }

     /* Convert the AML byte stream resource to a local resource struct */

    Status = AcpiRsConvertAmlToResource (
        Resource, AmlResource, ConversionTable);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status,
            "Could not convert AML resource (Type 0x%X)", *Aml));
        return_ACPI_STATUS (Status);
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_RESOURCES,
        "Type %.2X, AmlLength %.2X InternalLength %.2X\n",
        AcpiUtGetResourceType (Aml), Length,
        Resource->Length));

    /* Point to the next structure in the output buffer */

    *ResourcePtr = ACPI_NEXT_RESOURCE (Resource);
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsConvertResourcesToAml
 *
 * PARAMETERS:  Resource            - Pointer to the resource linked list
 *              AmlSizeNeeded       - Calculated size of the byte stream
 *                                    needed from calling AcpiRsGetAmlLength()
 *                                    The size of the OutputBuffer is
 *                                    guaranteed to be >= AmlSizeNeeded
 *              OutputBuffer        - Pointer to the buffer that will
 *                                    contain the byte stream
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes the resource linked list and parses it, creating a
 *              byte stream of resources in the caller's output buffer
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsConvertResourcesToAml (
    ACPI_RESOURCE           *Resource,
    ACPI_SIZE               AmlSizeNeeded,
    UINT8                   *OutputBuffer)
{
    UINT8                   *Aml = OutputBuffer;
    UINT8                   *EndAml = OutputBuffer + AmlSizeNeeded;
    ACPI_RSCONVERT_INFO     *ConversionTable;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (RsConvertResourcesToAml);


    /* Walk the resource descriptor list, convert each descriptor */

    while (Aml < EndAml)
    {
        /* Validate the (internal) Resource Type */

        if (Resource->Type > ACPI_RESOURCE_TYPE_MAX)
        {
            ACPI_ERROR ((AE_INFO,
                "Invalid descriptor type (0x%X) in resource list",
                Resource->Type));
            return_ACPI_STATUS (AE_BAD_DATA);
        }

        /* Sanity check the length. It must not be zero, or we loop forever */

        if (!Resource->Length)
        {
            ACPI_ERROR ((AE_INFO,
                "Invalid zero length descriptor in resource list\n"));
            return_ACPI_STATUS (AE_AML_BAD_RESOURCE_LENGTH);
        }

        /* Perform the conversion */

        if (Resource->Type == ACPI_RESOURCE_TYPE_SERIAL_BUS)
        {
            if (Resource->Data.CommonSerialBus.Type >
                AML_RESOURCE_MAX_SERIALBUSTYPE)
            {
                ConversionTable = NULL;
            }
            else
            {
                /* This is an I2C, SPI, or UART SerialBus descriptor */

                ConversionTable = AcpiGbl_ConvertResourceSerialBusDispatch[
                    Resource->Data.CommonSerialBus.Type];
            }
        }
        else
        {
            ConversionTable = AcpiGbl_SetResourceDispatch[Resource->Type];
        }

        if (!ConversionTable)
        {
            ACPI_ERROR ((AE_INFO,
                "Invalid/unsupported resource descriptor: Type 0x%2.2X",
                Resource->Type));
            return_ACPI_STATUS (AE_AML_INVALID_RESOURCE_TYPE);
        }

        Status = AcpiRsConvertResourceToAml (Resource,
            ACPI_CAST_PTR (AML_RESOURCE, Aml), ConversionTable);
        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status,
                "Could not convert resource (type 0x%X) to AML",
                Resource->Type));
            return_ACPI_STATUS (Status);
        }

        /* Perform final sanity check on the new AML resource descriptor */

        Status = AcpiUtValidateResource (
            NULL, ACPI_CAST_PTR (AML_RESOURCE, Aml), NULL);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        /* Check for end-of-list, normal exit */

        if (Resource->Type == ACPI_RESOURCE_TYPE_END_TAG)
        {
            /* An End Tag indicates the end of the input Resource Template */

            return_ACPI_STATUS (AE_OK);
        }

        /*
         * Extract the total length of the new descriptor and set the
         * Aml to point to the next (output) resource descriptor
         */
        Aml += AcpiUtGetDescriptorLength (Aml);

        /* Point to the next input resource descriptor */

        Resource = ACPI_NEXT_RESOURCE (Resource);
    }

    /* Completed buffer, but did not find an EndTag resource descriptor */

    return_ACPI_STATUS (AE_AML_NO_RESOURCE_END_TAG);
}
