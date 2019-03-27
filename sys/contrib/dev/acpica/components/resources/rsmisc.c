/*******************************************************************************
 *
 * Module Name: rsmisc - Miscellaneous resource descriptors
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
        ACPI_MODULE_NAME    ("rsmisc")


#define INIT_RESOURCE_TYPE(i)       i->ResourceOffset
#define INIT_RESOURCE_LENGTH(i)     i->AmlOffset
#define INIT_TABLE_LENGTH(i)        i->Value

#define COMPARE_OPCODE(i)           i->ResourceOffset
#define COMPARE_TARGET(i)           i->AmlOffset
#define COMPARE_VALUE(i)            i->Value


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsConvertAmlToResource
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *              Info                - Pointer to appropriate conversion table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an external AML resource descriptor to the corresponding
 *              internal resource descriptor
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsConvertAmlToResource (
    ACPI_RESOURCE           *Resource,
    AML_RESOURCE            *Aml,
    ACPI_RSCONVERT_INFO     *Info)
{
    ACPI_RS_LENGTH          AmlResourceLength;
    void                    *Source;
    void                    *Destination;
    char                    *Target;
    UINT8                   Count;
    UINT8                   FlagsMode = FALSE;
    UINT16                  ItemCount = 0;
    UINT16                  Temp16 = 0;


    ACPI_FUNCTION_TRACE (RsConvertAmlToResource);


    if (!Info)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    if (((ACPI_SIZE) Resource) & 0x3)
    {
        /* Each internal resource struct is expected to be 32-bit aligned */

        ACPI_WARNING ((AE_INFO,
            "Misaligned resource pointer (get): %p Type 0x%2.2X Length %u",
            Resource, Resource->Type, Resource->Length));
    }

    /* Extract the resource Length field (does not include header length) */

    AmlResourceLength = AcpiUtGetResourceLength (Aml);

    /*
     * First table entry must be ACPI_RSC_INITxxx and must contain the
     * table length (# of table entries)
     */
    Count = INIT_TABLE_LENGTH (Info);
    while (Count)
    {
        /*
         * Source is the external AML byte stream buffer,
         * destination is the internal resource descriptor
         */
        Source = ACPI_ADD_PTR (void, Aml, Info->AmlOffset);
        Destination = ACPI_ADD_PTR (void, Resource, Info->ResourceOffset);

        switch (Info->Opcode)
        {
        case ACPI_RSC_INITGET:
            /*
             * Get the resource type and the initial (minimum) length
             */
            memset (Resource, 0, INIT_RESOURCE_LENGTH (Info));
            Resource->Type = INIT_RESOURCE_TYPE (Info);
            Resource->Length = INIT_RESOURCE_LENGTH (Info);
            break;

        case ACPI_RSC_INITSET:
            break;

        case ACPI_RSC_FLAGINIT:

            FlagsMode = TRUE;
            break;

        case ACPI_RSC_1BITFLAG:
            /*
             * Mask and shift the flag bit
             */
            ACPI_SET8 (Destination,
                ((ACPI_GET8 (Source) >> Info->Value) & 0x01));
            break;

        case ACPI_RSC_2BITFLAG:
            /*
             * Mask and shift the flag bits
             */
            ACPI_SET8 (Destination,
                ((ACPI_GET8 (Source) >> Info->Value) & 0x03));
            break;

        case ACPI_RSC_3BITFLAG:
            /*
             * Mask and shift the flag bits
             */
            ACPI_SET8 (Destination,
                ((ACPI_GET8 (Source) >> Info->Value) & 0x07));
            break;

        case ACPI_RSC_COUNT:

            ItemCount = ACPI_GET8 (Source);
            ACPI_SET8 (Destination, ItemCount);

            Resource->Length = Resource->Length +
                (Info->Value * (ItemCount - 1));
            break;

        case ACPI_RSC_COUNT16:

            ItemCount = AmlResourceLength;
            ACPI_SET16 (Destination, ItemCount);

            Resource->Length = Resource->Length +
                (Info->Value * (ItemCount - 1));
            break;

        case ACPI_RSC_COUNT_GPIO_PIN:

            Target = ACPI_ADD_PTR (void, Aml, Info->Value);
            ItemCount = ACPI_GET16 (Target) - ACPI_GET16 (Source);

            Resource->Length = Resource->Length + ItemCount;
            ItemCount = ItemCount / 2;
            ACPI_SET16 (Destination, ItemCount);
            break;

        case ACPI_RSC_COUNT_GPIO_VEN:

            ItemCount = ACPI_GET8 (Source);
            ACPI_SET8 (Destination, ItemCount);

            Resource->Length = Resource->Length + (Info->Value * ItemCount);
            break;

        case ACPI_RSC_COUNT_GPIO_RES:
            /*
             * Vendor data is optional (length/offset may both be zero)
             * Examine vendor data length field first
             */
            Target = ACPI_ADD_PTR (void, Aml, (Info->Value + 2));
            if (ACPI_GET16 (Target))
            {
                /* Use vendor offset to get resource source length */

                Target = ACPI_ADD_PTR (void, Aml, Info->Value);
                ItemCount = ACPI_GET16 (Target) - ACPI_GET16 (Source);
            }
            else
            {
                /* No vendor data to worry about */

                ItemCount = Aml->LargeHeader.ResourceLength +
                    sizeof (AML_RESOURCE_LARGE_HEADER) -
                    ACPI_GET16 (Source);
            }

            Resource->Length = Resource->Length + ItemCount;
            ACPI_SET16 (Destination, ItemCount);
            break;

        case ACPI_RSC_COUNT_SERIAL_VEN:

            ItemCount = ACPI_GET16 (Source) - Info->Value;

            Resource->Length = Resource->Length + ItemCount;
            ACPI_SET16 (Destination, ItemCount);
            break;

        case ACPI_RSC_COUNT_SERIAL_RES:

            ItemCount = (AmlResourceLength +
                sizeof (AML_RESOURCE_LARGE_HEADER)) -
                ACPI_GET16 (Source) - Info->Value;

            Resource->Length = Resource->Length + ItemCount;
            ACPI_SET16 (Destination, ItemCount);
            break;

        case ACPI_RSC_LENGTH:

            Resource->Length = Resource->Length + Info->Value;
            break;

        case ACPI_RSC_MOVE8:
        case ACPI_RSC_MOVE16:
        case ACPI_RSC_MOVE32:
        case ACPI_RSC_MOVE64:
            /*
             * Raw data move. Use the Info value field unless ItemCount has
             * been previously initialized via a COUNT opcode
             */
            if (Info->Value)
            {
                ItemCount = Info->Value;
            }
            AcpiRsMoveData (Destination, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_MOVE_GPIO_PIN:

            /* Generate and set the PIN data pointer */

            Target = (char *) ACPI_ADD_PTR (void, Resource,
                (Resource->Length - ItemCount * 2));
            *(UINT16 **) Destination = ACPI_CAST_PTR (UINT16, Target);

            /* Copy the PIN data */

            Source = ACPI_ADD_PTR (void, Aml, ACPI_GET16 (Source));
            AcpiRsMoveData (Target, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_MOVE_GPIO_RES:

            /* Generate and set the ResourceSource string pointer */

            Target = (char *) ACPI_ADD_PTR (void, Resource,
                (Resource->Length - ItemCount));
            *(UINT8 **) Destination = ACPI_CAST_PTR (UINT8, Target);

            /* Copy the ResourceSource string */

            Source = ACPI_ADD_PTR (void, Aml, ACPI_GET16 (Source));
            AcpiRsMoveData (Target, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_MOVE_SERIAL_VEN:

            /* Generate and set the Vendor Data pointer */

            Target = (char *) ACPI_ADD_PTR (void, Resource,
                (Resource->Length - ItemCount));
            *(UINT8 **) Destination = ACPI_CAST_PTR (UINT8, Target);

            /* Copy the Vendor Data */

            Source = ACPI_ADD_PTR (void, Aml, Info->Value);
            AcpiRsMoveData (Target, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_MOVE_SERIAL_RES:

            /* Generate and set the ResourceSource string pointer */

            Target = (char *) ACPI_ADD_PTR (void, Resource,
                (Resource->Length - ItemCount));
            *(UINT8 **) Destination = ACPI_CAST_PTR (UINT8, Target);

            /* Copy the ResourceSource string */

            Source = ACPI_ADD_PTR (
                void, Aml, (ACPI_GET16 (Source) + Info->Value));
            AcpiRsMoveData (Target, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_SET8:

            memset (Destination, Info->AmlOffset, Info->Value);
            break;

        case ACPI_RSC_DATA8:

            Target = ACPI_ADD_PTR (char, Resource, Info->Value);
            memcpy (Destination, Source,  ACPI_GET16 (Target));
            break;

        case ACPI_RSC_ADDRESS:
            /*
             * Common handler for address descriptor flags
             */
            if (!AcpiRsGetAddressCommon (Resource, Aml))
            {
                return_ACPI_STATUS (AE_AML_INVALID_RESOURCE_TYPE);
            }
            break;

        case ACPI_RSC_SOURCE:
            /*
             * Optional ResourceSource (Index and String)
             */
            Resource->Length +=
                AcpiRsGetResourceSource (AmlResourceLength, Info->Value,
                    Destination, Aml, NULL);
            break;

        case ACPI_RSC_SOURCEX:
            /*
             * Optional ResourceSource (Index and String). This is the more
             * complicated case used by the Interrupt() macro
             */
            Target = ACPI_ADD_PTR (char, Resource,
                Info->AmlOffset + (ItemCount * 4));

            Resource->Length +=
                AcpiRsGetResourceSource (AmlResourceLength, (ACPI_RS_LENGTH)
                    (((ItemCount - 1) * sizeof (UINT32)) + Info->Value),
                    Destination, Aml, Target);
            break;

        case ACPI_RSC_BITMASK:
            /*
             * 8-bit encoded bitmask (DMA macro)
             */
            ItemCount = AcpiRsDecodeBitmask (ACPI_GET8 (Source), Destination);
            if (ItemCount)
            {
                Resource->Length += (ItemCount - 1);
            }

            Target = ACPI_ADD_PTR (char, Resource, Info->Value);
            ACPI_SET8 (Target, ItemCount);
            break;

        case ACPI_RSC_BITMASK16:
            /*
             * 16-bit encoded bitmask (IRQ macro)
             */
            ACPI_MOVE_16_TO_16 (&Temp16, Source);

            ItemCount = AcpiRsDecodeBitmask (Temp16, Destination);
            if (ItemCount)
            {
                Resource->Length += (ItemCount - 1);
            }

            Target = ACPI_ADD_PTR (char, Resource, Info->Value);
            ACPI_SET8 (Target, ItemCount);
            break;

        case ACPI_RSC_EXIT_NE:
            /*
             * Control - Exit conversion if not equal
             */
            switch (Info->ResourceOffset)
            {
            case ACPI_RSC_COMPARE_AML_LENGTH:

                if (AmlResourceLength != Info->Value)
                {
                    goto Exit;
                }
                break;

            case ACPI_RSC_COMPARE_VALUE:

                if (ACPI_GET8 (Source) != Info->Value)
                {
                    goto Exit;
                }
                break;

            default:

                ACPI_ERROR ((AE_INFO, "Invalid conversion sub-opcode"));
                return_ACPI_STATUS (AE_BAD_PARAMETER);
            }
            break;

        default:

            ACPI_ERROR ((AE_INFO, "Invalid conversion opcode"));
            return_ACPI_STATUS (AE_BAD_PARAMETER);
        }

        Count--;
        Info++;
    }

Exit:
    if (!FlagsMode)
    {
        /* Round the resource struct length up to the next boundary (32 or 64) */

        Resource->Length = (UINT32)
            ACPI_ROUND_UP_TO_NATIVE_WORD (Resource->Length);
    }
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsConvertResourceToAml
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *              Info                - Pointer to appropriate conversion table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an internal resource descriptor to the corresponding
 *              external AML resource descriptor.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiRsConvertResourceToAml (
    ACPI_RESOURCE           *Resource,
    AML_RESOURCE            *Aml,
    ACPI_RSCONVERT_INFO     *Info)
{
    void                    *Source = NULL;
    void                    *Destination;
    char                    *Target;
    ACPI_RSDESC_SIZE        AmlLength = 0;
    UINT8                   Count;
    UINT16                  Temp16 = 0;
    UINT16                  ItemCount = 0;


    ACPI_FUNCTION_TRACE (RsConvertResourceToAml);


    if (!Info)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /*
     * First table entry must be ACPI_RSC_INITxxx and must contain the
     * table length (# of table entries)
     */
    Count = INIT_TABLE_LENGTH (Info);

    while (Count)
    {
        /*
         * Source is the internal resource descriptor,
         * destination is the external AML byte stream buffer
         */
        Source = ACPI_ADD_PTR (void, Resource, Info->ResourceOffset);
        Destination = ACPI_ADD_PTR (void, Aml, Info->AmlOffset);

        switch (Info->Opcode)
        {
        case ACPI_RSC_INITSET:

            memset (Aml, 0, INIT_RESOURCE_LENGTH (Info));
            AmlLength = INIT_RESOURCE_LENGTH (Info);
            AcpiRsSetResourceHeader (
                INIT_RESOURCE_TYPE (Info), AmlLength, Aml);
            break;

        case ACPI_RSC_INITGET:
            break;

        case ACPI_RSC_FLAGINIT:
            /*
             * Clear the flag byte
             */
            ACPI_SET8 (Destination, 0);
            break;

        case ACPI_RSC_1BITFLAG:
            /*
             * Mask and shift the flag bit
             */
            ACPI_SET_BIT (*ACPI_CAST8 (Destination), (UINT8)
                ((ACPI_GET8 (Source) & 0x01) << Info->Value));
            break;

        case ACPI_RSC_2BITFLAG:
            /*
             * Mask and shift the flag bits
             */
            ACPI_SET_BIT (*ACPI_CAST8 (Destination), (UINT8)
                ((ACPI_GET8 (Source) & 0x03) << Info->Value));
            break;

        case ACPI_RSC_3BITFLAG:
            /*
             * Mask and shift the flag bits
             */
            ACPI_SET_BIT (*ACPI_CAST8 (Destination), (UINT8)
                ((ACPI_GET8 (Source) & 0x07) << Info->Value));
            break;

        case ACPI_RSC_COUNT:

            ItemCount = ACPI_GET8 (Source);
            ACPI_SET8 (Destination, ItemCount);

            AmlLength = (UINT16)
                (AmlLength + (Info->Value * (ItemCount - 1)));
            break;

        case ACPI_RSC_COUNT16:

            ItemCount = ACPI_GET16 (Source);
            AmlLength = (UINT16) (AmlLength + ItemCount);
            AcpiRsSetResourceLength (AmlLength, Aml);
            break;

        case ACPI_RSC_COUNT_GPIO_PIN:

            ItemCount = ACPI_GET16 (Source);
            ACPI_SET16 (Destination, AmlLength);

            AmlLength = (UINT16) (AmlLength + ItemCount * 2);
            Target = ACPI_ADD_PTR (void, Aml, Info->Value);
            ACPI_SET16 (Target, AmlLength);
            AcpiRsSetResourceLength (AmlLength, Aml);
            break;

        case ACPI_RSC_COUNT_GPIO_VEN:

            ItemCount = ACPI_GET16 (Source);
            ACPI_SET16 (Destination, ItemCount);

            AmlLength = (UINT16) (
                AmlLength + (Info->Value * ItemCount));
            AcpiRsSetResourceLength (AmlLength, Aml);
            break;

        case ACPI_RSC_COUNT_GPIO_RES:

            /* Set resource source string length */

            ItemCount = ACPI_GET16 (Source);
            ACPI_SET16 (Destination, AmlLength);

            /* Compute offset for the Vendor Data */

            AmlLength = (UINT16) (AmlLength + ItemCount);
            Target = ACPI_ADD_PTR (void, Aml, Info->Value);

            /* Set vendor offset only if there is vendor data */

            ACPI_SET16 (Target, AmlLength);

            AcpiRsSetResourceLength (AmlLength, Aml);
            break;

        case ACPI_RSC_COUNT_SERIAL_VEN:

            ItemCount = ACPI_GET16 (Source);
            ACPI_SET16 (Destination, ItemCount + Info->Value);
            AmlLength = (UINT16) (AmlLength + ItemCount);
            AcpiRsSetResourceLength (AmlLength, Aml);
            break;

        case ACPI_RSC_COUNT_SERIAL_RES:

            ItemCount = ACPI_GET16 (Source);
            AmlLength = (UINT16) (AmlLength + ItemCount);
            AcpiRsSetResourceLength (AmlLength, Aml);
            break;

        case ACPI_RSC_LENGTH:

            AcpiRsSetResourceLength (Info->Value, Aml);
            break;

        case ACPI_RSC_MOVE8:
        case ACPI_RSC_MOVE16:
        case ACPI_RSC_MOVE32:
        case ACPI_RSC_MOVE64:

            if (Info->Value)
            {
                ItemCount = Info->Value;
            }
            AcpiRsMoveData (Destination, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_MOVE_GPIO_PIN:

            Destination = (char *) ACPI_ADD_PTR (void, Aml,
                ACPI_GET16 (Destination));
            Source = * (UINT16 **) Source;
            AcpiRsMoveData (Destination, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_MOVE_GPIO_RES:

            /* Used for both ResourceSource string and VendorData */

            Destination = (char *) ACPI_ADD_PTR (void, Aml,
                ACPI_GET16 (Destination));
            Source = * (UINT8 **) Source;
            AcpiRsMoveData (Destination, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_MOVE_SERIAL_VEN:

            Destination = (char *) ACPI_ADD_PTR (void, Aml,
                (AmlLength - ItemCount));
            Source = * (UINT8 **) Source;
            AcpiRsMoveData (Destination, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_MOVE_SERIAL_RES:

            Destination = (char *) ACPI_ADD_PTR (void, Aml,
                (AmlLength - ItemCount));
            Source = * (UINT8 **) Source;
            AcpiRsMoveData (Destination, Source, ItemCount, Info->Opcode);
            break;

        case ACPI_RSC_ADDRESS:

            /* Set the Resource Type, General Flags, and Type-Specific Flags */

            AcpiRsSetAddressCommon (Aml, Resource);
            break;

        case ACPI_RSC_SOURCEX:
            /*
             * Optional ResourceSource (Index and String)
             */
            AmlLength = AcpiRsSetResourceSource (
                Aml, (ACPI_RS_LENGTH) AmlLength, Source);
            AcpiRsSetResourceLength (AmlLength, Aml);
            break;

        case ACPI_RSC_SOURCE:
            /*
             * Optional ResourceSource (Index and String). This is the more
             * complicated case used by the Interrupt() macro
             */
            AmlLength = AcpiRsSetResourceSource (Aml, Info->Value, Source);
            AcpiRsSetResourceLength (AmlLength, Aml);
            break;

        case ACPI_RSC_BITMASK:
            /*
             * 8-bit encoded bitmask (DMA macro)
             */
            ACPI_SET8 (Destination,
                AcpiRsEncodeBitmask (Source,
                    *ACPI_ADD_PTR (UINT8, Resource, Info->Value)));
            break;

        case ACPI_RSC_BITMASK16:
            /*
             * 16-bit encoded bitmask (IRQ macro)
             */
            Temp16 = AcpiRsEncodeBitmask (
                Source, *ACPI_ADD_PTR (UINT8, Resource, Info->Value));
            ACPI_MOVE_16_TO_16 (Destination, &Temp16);
            break;

        case ACPI_RSC_EXIT_LE:
            /*
             * Control - Exit conversion if less than or equal
             */
            if (ItemCount <= Info->Value)
            {
                goto Exit;
            }
            break;

        case ACPI_RSC_EXIT_NE:
            /*
             * Control - Exit conversion if not equal
             */
            switch (COMPARE_OPCODE (Info))
            {
            case ACPI_RSC_COMPARE_VALUE:

                if (*ACPI_ADD_PTR (UINT8, Resource,
                    COMPARE_TARGET (Info)) != COMPARE_VALUE (Info))
                {
                    goto Exit;
                }
                break;

            default:

                ACPI_ERROR ((AE_INFO, "Invalid conversion sub-opcode"));
                return_ACPI_STATUS (AE_BAD_PARAMETER);
            }
            break;

        case ACPI_RSC_EXIT_EQ:
            /*
             * Control - Exit conversion if equal
             */
            if (*ACPI_ADD_PTR (UINT8, Resource,
                COMPARE_TARGET (Info)) == COMPARE_VALUE (Info))
            {
                goto Exit;
            }
            break;

        default:

            ACPI_ERROR ((AE_INFO, "Invalid conversion opcode"));
            return_ACPI_STATUS (AE_BAD_PARAMETER);
        }

        Count--;
        Info++;
    }

Exit:
    return_ACPI_STATUS (AE_OK);
}


#if 0
/* Previous resource validations */

    if (Aml->ExtAddress64.RevisionID !=
        AML_RESOURCE_EXTENDED_ADDRESS_REVISION)
    {
        return_ACPI_STATUS (AE_SUPPORT);
    }

    if (Resource->Data.StartDpf.PerformanceRobustness >= 3)
    {
        return_ACPI_STATUS (AE_AML_BAD_RESOURCE_VALUE);
    }

    if (((Aml->Irq.Flags & 0x09) == 0x00) ||
        ((Aml->Irq.Flags & 0x09) == 0x09))
    {
        /*
         * Only [ActiveHigh, EdgeSensitive] or [ActiveLow, LevelSensitive]
         * polarity/trigger interrupts are allowed (ACPI spec, section
         * "IRQ Format"), so 0x00 and 0x09 are illegal.
         */
        ACPI_ERROR ((AE_INFO,
            "Invalid interrupt polarity/trigger in resource list, 0x%X",
            Aml->Irq.Flags));
        return_ACPI_STATUS (AE_BAD_DATA);
    }

    Resource->Data.ExtendedIrq.InterruptCount = Temp8;
    if (Temp8 < 1)
    {
        /* Must have at least one IRQ */

        return_ACPI_STATUS (AE_AML_BAD_RESOURCE_LENGTH);
    }

    if (Resource->Data.Dma.Transfer == 0x03)
    {
        ACPI_ERROR ((AE_INFO,
            "Invalid DMA.Transfer preference (3)"));
        return_ACPI_STATUS (AE_BAD_DATA);
    }
#endif
