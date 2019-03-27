/******************************************************************************
 *
 * Module Name: aslrestype2q - Large QWord address resource descriptors
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

#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include "aslcompiler.y.h"

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslrestype2q")

/*
 * This module contains the QWord (64-bit) address space descriptors:
 *
 * QWordIO
 * QWordMemory
 * QWordSpace
 */

/*******************************************************************************
 *
 * FUNCTION:    RsDoQwordIoDescriptor
 *
 * PARAMETERS:  Info                - Parse Op and resource template offset
 *
 * RETURN:      Completed resource node
 *
 * DESCRIPTION: Construct a long "QwordIO" descriptor
 *
 ******************************************************************************/

ASL_RESOURCE_NODE *
RsDoQwordIoDescriptor (
    ASL_RESOURCE_INFO       *Info)
{
    AML_RESOURCE            *Descriptor;
    ACPI_PARSE_OBJECT       *InitializerOp;
    ACPI_PARSE_OBJECT       *MinOp = NULL;
    ACPI_PARSE_OBJECT       *MaxOp = NULL;
    ACPI_PARSE_OBJECT       *LengthOp = NULL;
    ACPI_PARSE_OBJECT       *GranOp = NULL;
    ASL_RESOURCE_NODE       *Rnode;
    UINT8                   *OptionalFields;
    UINT16                  StringLength = 0;
    UINT32                  OptionIndex = 0;
    UINT32                  CurrentByteOffset;
    UINT32                  i;
    BOOLEAN                 ResSourceIndex = FALSE;


    InitializerOp = Info->DescriptorTypeOp->Asl.Child;
    StringLength = RsGetStringDataLength (InitializerOp);
    CurrentByteOffset = Info->CurrentByteOffset;

    Rnode = RsAllocateResourceNode (
        sizeof (AML_RESOURCE_ADDRESS64) + 1 + StringLength);

    Descriptor = Rnode->Buffer;
    Descriptor->Address64.DescriptorType = ACPI_RESOURCE_NAME_ADDRESS64;
    Descriptor->Address64.ResourceType = ACPI_ADDRESS_TYPE_IO_RANGE;

    /*
     * Initial descriptor length -- may be enlarged if there are
     * optional fields present
     */
    OptionalFields = ((UINT8 *) Descriptor) + sizeof (AML_RESOURCE_ADDRESS64);
    Descriptor->Address64.ResourceLength = (UINT16)
        (sizeof (AML_RESOURCE_ADDRESS64) -
         sizeof (AML_RESOURCE_LARGE_HEADER));

    /* Process all child initialization nodes */

    for (i = 0; InitializerOp; i++)
    {
        switch (i)
        {
        case 0: /* Resource Usage */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 0, 1);
            break;

        case 1: /* MinType */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 2, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_MINTYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Flags), 2);
            break;

        case 2: /* MaxType */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 3, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_MAXTYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Flags), 3);
            break;

        case 3: /* DecodeType */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 1, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_DECODE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Flags), 1);
            break;

        case 4: /* Range Type */

            RsSetFlagBits (&Descriptor->Address64.SpecificFlags, InitializerOp, 0, 3);
            RsCreateMultiBitField (InitializerOp, ACPI_RESTAG_RANGETYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.SpecificFlags), 0, 2);
            break;

        case 5: /* Address Granularity */

            Descriptor->Address64.Granularity = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_GRANULARITY,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Granularity));
            GranOp = InitializerOp;
            break;

        case 6: /* Address Min */

            Descriptor->Address64.Minimum = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_MINADDR,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Minimum));
            MinOp = InitializerOp;
            break;

        case 7: /* Address Max */

            Descriptor->Address64.Maximum = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_MAXADDR,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Maximum));
            MaxOp = InitializerOp;
            break;

        case 8: /* Translation Offset */

            Descriptor->Address64.TranslationOffset = InitializerOp->Asl.Value.Integer;
            RsCreateByteField (InitializerOp, ACPI_RESTAG_TRANSLATION,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.TranslationOffset));
            break;

        case 9: /* Address Length */

            Descriptor->Address64.AddressLength = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_LENGTH,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.AddressLength));
            LengthOp = InitializerOp;
            break;

        case 10: /* ResSourceIndex [Optional Field - BYTE] */

            if (InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
            {
                OptionalFields[0] = (UINT8) InitializerOp->Asl.Value.Integer;
                OptionIndex++;
                Descriptor->Address64.ResourceLength++;
                ResSourceIndex = TRUE;
            }
            break;

        case 11: /* ResSource [Optional Field - STRING] */

            if ((InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG) &&
                (InitializerOp->Asl.Value.String))
            {
                if (StringLength)
                {
                    Descriptor->Address64.ResourceLength = (UINT16)
                        (Descriptor->Address64.ResourceLength + StringLength);

                    strcpy ((char *)
                        &OptionalFields[OptionIndex],
                        InitializerOp->Asl.Value.String);

                    /* ResourceSourceIndex must also be valid */

                    if (!ResSourceIndex)
                    {
                        AslError (ASL_ERROR, ASL_MSG_RESOURCE_INDEX,
                            InitializerOp, NULL);
                    }
                }
            }

#if 0
            /*
             * Not a valid ResourceSource, ResourceSourceIndex must also
             * be invalid
             */
            else if (ResSourceIndex)
            {
                AslError (ASL_ERROR, ASL_MSG_RESOURCE_SOURCE,
                    InitializerOp, NULL);
            }
#endif
            break;

        case 12: /* ResourceTag */

            UtAttachNamepathToOwner (Info->DescriptorTypeOp, InitializerOp);
            break;

        case 13: /* Type */

            RsSetFlagBits (&Descriptor->Address64.SpecificFlags, InitializerOp, 4, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_TYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.SpecificFlags), 4);
            break;

        case 14: /* Translation Type */

            RsSetFlagBits (&Descriptor->Address64.SpecificFlags, InitializerOp, 5, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_TRANSTYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.SpecificFlags), 5);
            break;

        default:

            AslError (ASL_ERROR, ASL_MSG_RESOURCE_LIST, InitializerOp, NULL);
            break;
        }

        InitializerOp = RsCompleteNodeAndGetNext (InitializerOp);
    }

    /* Validate the Min/Max/Len/Gran values */

    RsLargeAddressCheck (
        Descriptor->Address64.Minimum,
        Descriptor->Address64.Maximum,
        Descriptor->Address64.AddressLength,
        Descriptor->Address64.Granularity,
        Descriptor->Address64.Flags,
        MinOp, MaxOp, LengthOp, GranOp, Info->DescriptorTypeOp);

    Rnode->BufferLength = sizeof (AML_RESOURCE_ADDRESS64) +
        OptionIndex + StringLength;
    return (Rnode);
}


/*******************************************************************************
 *
 * FUNCTION:    RsDoQwordMemoryDescriptor
 *
 * PARAMETERS:  Info                - Parse Op and resource template offset
 *
 * RETURN:      Completed resource node
 *
 * DESCRIPTION: Construct a long "QwordMemory" descriptor
 *
 ******************************************************************************/

ASL_RESOURCE_NODE *
RsDoQwordMemoryDescriptor (
    ASL_RESOURCE_INFO       *Info)
{
    AML_RESOURCE            *Descriptor;
    ACPI_PARSE_OBJECT       *InitializerOp;
    ACPI_PARSE_OBJECT       *MinOp = NULL;
    ACPI_PARSE_OBJECT       *MaxOp = NULL;
    ACPI_PARSE_OBJECT       *LengthOp = NULL;
    ACPI_PARSE_OBJECT       *GranOp = NULL;
    ASL_RESOURCE_NODE       *Rnode;
    UINT8                   *OptionalFields;
    UINT16                  StringLength = 0;
    UINT32                  OptionIndex = 0;
    UINT32                  CurrentByteOffset;
    UINT32                  i;
    BOOLEAN                 ResSourceIndex = FALSE;


    InitializerOp = Info->DescriptorTypeOp->Asl.Child;
    StringLength = RsGetStringDataLength (InitializerOp);
    CurrentByteOffset = Info->CurrentByteOffset;

    Rnode = RsAllocateResourceNode (
        sizeof (AML_RESOURCE_ADDRESS64) + 1 + StringLength);

    Descriptor = Rnode->Buffer;
    Descriptor->Address64.DescriptorType = ACPI_RESOURCE_NAME_ADDRESS64;
    Descriptor->Address64.ResourceType = ACPI_ADDRESS_TYPE_MEMORY_RANGE;

    /*
     * Initial descriptor length -- may be enlarged if there are
     * optional fields present
     */
    OptionalFields = ((UINT8 *) Descriptor) + sizeof (AML_RESOURCE_ADDRESS64);
    Descriptor->Address64.ResourceLength = (UINT16)
        (sizeof (AML_RESOURCE_ADDRESS64) -
         sizeof (AML_RESOURCE_LARGE_HEADER));

    /* Process all child initialization nodes */

    for (i = 0; InitializerOp; i++)
    {
        switch (i)
        {
        case 0: /* Resource Usage */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 0, 1);
            break;

        case 1: /* DecodeType */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 1, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_DECODE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Flags), 1);
            break;

        case 2: /* MinType */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 2, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_MINTYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Flags), 2);
            break;

        case 3: /* MaxType */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 3, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_MAXTYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Flags), 3);
            break;

        case 4: /* Memory Type */

            RsSetFlagBits (&Descriptor->Address64.SpecificFlags, InitializerOp, 1, 0);
            RsCreateMultiBitField (InitializerOp, ACPI_RESTAG_MEMTYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.SpecificFlags), 1, 2);
            break;

        case 5: /* Read/Write Type */

            RsSetFlagBits (&Descriptor->Address64.SpecificFlags, InitializerOp, 0, 1);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_READWRITETYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.SpecificFlags), 0);
            break;

        case 6: /* Address Granularity */

            Descriptor->Address64.Granularity = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_GRANULARITY,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Granularity));
            GranOp = InitializerOp;
            break;

        case 7: /* Min Address */

            Descriptor->Address64.Minimum = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_MINADDR,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Minimum));
            MinOp = InitializerOp;
            break;

        case 8: /* Max Address */

            Descriptor->Address64.Maximum = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_MAXADDR,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Maximum));
            MaxOp = InitializerOp;
            break;

        case 9: /* Translation Offset */

            Descriptor->Address64.TranslationOffset = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_TRANSLATION,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.TranslationOffset));
            break;

        case 10: /* Address Length */

            Descriptor->Address64.AddressLength = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_LENGTH,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.AddressLength));
            LengthOp = InitializerOp;
            break;

        case 11: /* ResSourceIndex [Optional Field - BYTE] */

            if (InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
            {
                OptionalFields[0] = (UINT8) InitializerOp->Asl.Value.Integer;
                OptionIndex++;
                Descriptor->Address64.ResourceLength++;
                ResSourceIndex = TRUE;
            }
            break;

        case 12: /* ResSource [Optional Field - STRING] */

            if ((InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG) &&
                (InitializerOp->Asl.Value.String))
            {
                if (StringLength)
                {
                    Descriptor->Address64.ResourceLength = (UINT16)
                        (Descriptor->Address64.ResourceLength + StringLength);

                    strcpy ((char *)
                        &OptionalFields[OptionIndex],
                        InitializerOp->Asl.Value.String);

                    /* ResourceSourceIndex must also be valid */

                    if (!ResSourceIndex)
                    {
                        AslError (ASL_ERROR, ASL_MSG_RESOURCE_INDEX,
                            InitializerOp, NULL);
                    }
                }
            }

#if 0
            /*
             * Not a valid ResourceSource, ResourceSourceIndex must also
             * be invalid
             */
            else if (ResSourceIndex)
            {
                AslError (ASL_ERROR, ASL_MSG_RESOURCE_SOURCE,
                    InitializerOp, NULL);
            }
#endif
            break;

        case 13: /* ResourceTag */

            UtAttachNamepathToOwner (Info->DescriptorTypeOp, InitializerOp);
            break;


        case 14: /* Address Range */

            RsSetFlagBits (&Descriptor->Address64.SpecificFlags, InitializerOp, 3, 0);
            RsCreateMultiBitField (InitializerOp, ACPI_RESTAG_MEMATTRIBUTES,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.SpecificFlags), 3, 2);
            break;

        case 15: /* Type */

            RsSetFlagBits (&Descriptor->Address64.SpecificFlags, InitializerOp, 5, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_TYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.SpecificFlags), 5);
            break;

        default:

            AslError (ASL_ERROR, ASL_MSG_RESOURCE_LIST, InitializerOp, NULL);
            break;
        }

        InitializerOp = RsCompleteNodeAndGetNext (InitializerOp);
    }

    /* Validate the Min/Max/Len/Gran values */

    RsLargeAddressCheck (
        Descriptor->Address64.Minimum,
        Descriptor->Address64.Maximum,
        Descriptor->Address64.AddressLength,
        Descriptor->Address64.Granularity,
        Descriptor->Address64.Flags,
        MinOp, MaxOp, LengthOp, GranOp, Info->DescriptorTypeOp);

    Rnode->BufferLength = sizeof (AML_RESOURCE_ADDRESS64) +
        OptionIndex + StringLength;
    return (Rnode);
}


/*******************************************************************************
 *
 * FUNCTION:    RsDoQwordSpaceDescriptor
 *
 * PARAMETERS:  Info                - Parse Op and resource template offset
 *
 * RETURN:      Completed resource node
 *
 * DESCRIPTION: Construct a long "QwordSpace" descriptor
 *
 ******************************************************************************/

ASL_RESOURCE_NODE *
RsDoQwordSpaceDescriptor (
    ASL_RESOURCE_INFO       *Info)
{
    AML_RESOURCE            *Descriptor;
    ACPI_PARSE_OBJECT       *InitializerOp;
    ACPI_PARSE_OBJECT       *MinOp = NULL;
    ACPI_PARSE_OBJECT       *MaxOp = NULL;
    ACPI_PARSE_OBJECT       *LengthOp = NULL;
    ACPI_PARSE_OBJECT       *GranOp = NULL;
    ASL_RESOURCE_NODE       *Rnode;
    UINT8                   *OptionalFields;
    UINT16                  StringLength = 0;
    UINT32                  OptionIndex = 0;
    UINT32                  CurrentByteOffset;
    UINT32                  i;
    BOOLEAN                 ResSourceIndex = FALSE;


    InitializerOp = Info->DescriptorTypeOp->Asl.Child;
    StringLength = RsGetStringDataLength (InitializerOp);
    CurrentByteOffset = Info->CurrentByteOffset;

    Rnode = RsAllocateResourceNode (
        sizeof (AML_RESOURCE_ADDRESS64) + 1 + StringLength);

    Descriptor = Rnode->Buffer;
    Descriptor->Address64.DescriptorType = ACPI_RESOURCE_NAME_ADDRESS64;

    /*
     * Initial descriptor length -- may be enlarged if there are
     * optional fields present
     */
    OptionalFields = ((UINT8 *) Descriptor) + sizeof (AML_RESOURCE_ADDRESS64);
    Descriptor->Address64.ResourceLength = (UINT16)
        (sizeof (AML_RESOURCE_ADDRESS64) -
         sizeof (AML_RESOURCE_LARGE_HEADER));

    /* Process all child initialization nodes */

    for (i = 0; InitializerOp; i++)
    {
        switch (i)
        {
        case 0: /* Resource Type */

            Descriptor->Address64.ResourceType =
                (UINT8) InitializerOp->Asl.Value.Integer;
            break;

        case 1: /* Resource Usage */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 0, 1);
            break;

        case 2: /* DecodeType */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 1, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_DECODE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Flags), 1);
            break;

        case 3: /* MinType */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 2, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_MINTYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Flags), 2);
            break;

        case 4: /* MaxType */

            RsSetFlagBits (&Descriptor->Address64.Flags, InitializerOp, 3, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_MAXTYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Flags), 3);
            break;

        case 5: /* Type-Specific flags */

            Descriptor->Address64.SpecificFlags =
                (UINT8) InitializerOp->Asl.Value.Integer;
            break;

        case 6: /* Address Granularity */

            Descriptor->Address64.Granularity = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_GRANULARITY,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Granularity));
            GranOp = InitializerOp;
            break;

        case 7: /* Min Address */

            Descriptor->Address64.Minimum = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_MINADDR,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Minimum));
            MinOp = InitializerOp;
            break;

        case 8: /* Max Address */

            Descriptor->Address64.Maximum = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_MAXADDR,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.Maximum));
            MaxOp = InitializerOp;
            break;

        case 9: /* Translation Offset */

            Descriptor->Address64.TranslationOffset = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_TRANSLATION,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.TranslationOffset));
            break;

        case 10: /* Address Length */

            Descriptor->Address64.AddressLength = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_LENGTH,
                CurrentByteOffset + ASL_RESDESC_OFFSET (Address64.AddressLength));
            LengthOp = InitializerOp;
            break;

        case 11: /* ResSourceIndex [Optional Field - BYTE] */

            if (InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
            {
                OptionalFields[0] = (UINT8) InitializerOp->Asl.Value.Integer;
                OptionIndex++;
                Descriptor->Address64.ResourceLength++;
                ResSourceIndex = TRUE;
            }
            break;

        case 12: /* ResSource [Optional Field - STRING] */

            if ((InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG) &&
                (InitializerOp->Asl.Value.String))
            {
                if (StringLength)
                {
                    Descriptor->Address64.ResourceLength = (UINT16)
                        (Descriptor->Address64.ResourceLength + StringLength);

                    strcpy ((char *)
                        &OptionalFields[OptionIndex],
                        InitializerOp->Asl.Value.String);

                    /* ResourceSourceIndex must also be valid */

                    if (!ResSourceIndex)
                    {
                        AslError (ASL_ERROR, ASL_MSG_RESOURCE_INDEX,
                            InitializerOp, NULL);
                    }
                }
            }

#if 0
            /*
             * Not a valid ResourceSource, ResourceSourceIndex must also
             * be invalid
             */
            else if (ResSourceIndex)
            {
                AslError (ASL_ERROR, ASL_MSG_RESOURCE_SOURCE,
                    InitializerOp, NULL);
            }
#endif
            break;

        case 13: /* ResourceTag */

            UtAttachNamepathToOwner (Info->DescriptorTypeOp, InitializerOp);
            break;

        default:

            AslError (ASL_ERROR, ASL_MSG_RESOURCE_LIST, InitializerOp, NULL);
            break;
        }

        InitializerOp = RsCompleteNodeAndGetNext (InitializerOp);
    }

    /* Validate the Min/Max/Len/Gran values */

    RsLargeAddressCheck (
        Descriptor->Address64.Minimum,
        Descriptor->Address64.Maximum,
        Descriptor->Address64.AddressLength,
        Descriptor->Address64.Granularity,
        Descriptor->Address64.Flags,
        MinOp, MaxOp, LengthOp, GranOp, Info->DescriptorTypeOp);

    Rnode->BufferLength = sizeof (AML_RESOURCE_ADDRESS64) +
        OptionIndex + StringLength;
    return (Rnode);
}
