/******************************************************************************
 *
 * Module Name: aslrestype2 - Miscellaneous Large resource descriptors
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
#include <contrib/dev/acpica/include/amlcode.h>

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslrestype2")

/*
 * This module contains miscellaneous large resource descriptors:
 *
 * Register
 * Interrupt
 * VendorLong
 */

/*******************************************************************************
 *
 * FUNCTION:    RsDoGeneralRegisterDescriptor
 *
 * PARAMETERS:  Info                - Parse Op and resource template offset
 *
 * RETURN:      Completed resource node
 *
 * DESCRIPTION: Construct a long "Register" descriptor
 *
 ******************************************************************************/

ASL_RESOURCE_NODE *
RsDoGeneralRegisterDescriptor (
    ASL_RESOURCE_INFO       *Info)
{
    AML_RESOURCE            *Descriptor;
    ACPI_PARSE_OBJECT       *InitializerOp;
    ASL_RESOURCE_NODE       *Rnode;
    UINT32                  CurrentByteOffset;
    UINT32                  i;


    InitializerOp = Info->DescriptorTypeOp->Asl.Child;
    CurrentByteOffset = Info->CurrentByteOffset;
    Rnode = RsAllocateResourceNode (sizeof (AML_RESOURCE_GENERIC_REGISTER));

    Descriptor = Rnode->Buffer;
    Descriptor->GenericReg.DescriptorType = ACPI_RESOURCE_NAME_GENERIC_REGISTER;
    Descriptor->GenericReg.ResourceLength = 12;

    /* Process all child initialization nodes */

    for (i = 0; InitializerOp; i++)
    {
        switch (i)
        {
        case 0: /* Address space */

            Descriptor->GenericReg.AddressSpaceId = (UINT8) InitializerOp->Asl.Value.Integer;
            RsCreateByteField (InitializerOp, ACPI_RESTAG_ADDRESSSPACE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (GenericReg.AddressSpaceId));
           break;

        case 1: /* Register Bit Width */

            Descriptor->GenericReg.BitWidth = (UINT8) InitializerOp->Asl.Value.Integer;
            RsCreateByteField (InitializerOp, ACPI_RESTAG_REGISTERBITWIDTH,
                CurrentByteOffset + ASL_RESDESC_OFFSET (GenericReg.BitWidth));
            break;

        case 2: /* Register Bit Offset */

            Descriptor->GenericReg.BitOffset = (UINT8) InitializerOp->Asl.Value.Integer;
            RsCreateByteField (InitializerOp, ACPI_RESTAG_REGISTERBITOFFSET,
                CurrentByteOffset + ASL_RESDESC_OFFSET (GenericReg.BitOffset));
            break;

        case 3: /* Register Address */

            Descriptor->GenericReg.Address = InitializerOp->Asl.Value.Integer;
            RsCreateQwordField (InitializerOp, ACPI_RESTAG_ADDRESS,
                CurrentByteOffset + ASL_RESDESC_OFFSET (GenericReg.Address));
            break;

        case 4: /* Access Size (ACPI 3.0) */

            Descriptor->GenericReg.AccessSize = (UINT8) InitializerOp->Asl.Value.Integer;
            RsCreateByteField (InitializerOp, ACPI_RESTAG_ACCESSSIZE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (GenericReg.AccessSize));

            if (Descriptor->GenericReg.AddressSpaceId == ACPI_ADR_SPACE_PLATFORM_COMM)
            {
                break;
            }

            if (Descriptor->GenericReg.AccessSize > AML_FIELD_ACCESS_QWORD)
            {
                AslError (ASL_ERROR, ASL_MSG_INVALID_ACCESS_SIZE,
                    InitializerOp, NULL);
            }
            break;

        case 5: /* ResourceTag (ACPI 3.0b) */

            UtAttachNamepathToOwner (Info->DescriptorTypeOp, InitializerOp);
            break;

        default:

            AslError (ASL_ERROR, ASL_MSG_RESOURCE_LIST, InitializerOp, NULL);
            break;
        }

        InitializerOp = RsCompleteNodeAndGetNext (InitializerOp);
    }

    return (Rnode);
}


/*******************************************************************************
 *
 * FUNCTION:    RsDoInterruptDescriptor
 *
 * PARAMETERS:  Info                - Parse Op and resource template offset
 *
 * RETURN:      Completed resource node
 *
 * DESCRIPTION: Construct a long "Interrupt" descriptor
 *
 ******************************************************************************/

ASL_RESOURCE_NODE *
RsDoInterruptDescriptor (
    ASL_RESOURCE_INFO       *Info)
{
    AML_RESOURCE            *Descriptor;
    AML_RESOURCE            *Rover = NULL;
    ACPI_PARSE_OBJECT       *InitializerOp;
    ASL_RESOURCE_NODE       *Rnode;
    UINT16                  StringLength = 0;
    UINT32                  OptionIndex = 0;
    UINT32                  CurrentByteOffset;
    UINT32                  i;
    BOOLEAN                 HasResSourceIndex = FALSE;
    UINT8                   ResSourceIndex = 0;
    UINT8                   *ResSourceString = NULL;


    InitializerOp = Info->DescriptorTypeOp->Asl.Child;
    CurrentByteOffset = Info->CurrentByteOffset;
    StringLength = RsGetStringDataLength (InitializerOp);

    /* Count the interrupt numbers */

    for (i = 0; InitializerOp; i++)
    {
        InitializerOp = ASL_GET_PEER_NODE (InitializerOp);

        if (i <= 6)
        {
            if (i == 3 &&
                InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
            {
                /*
                 * ResourceSourceIndex was specified, always make room for
                 * it, even if the ResourceSource was omitted.
                 */
                OptionIndex++;
            }

            continue;
        }

        OptionIndex += 4;
    }

    InitializerOp = Info->DescriptorTypeOp->Asl.Child;
    Rnode = RsAllocateResourceNode (sizeof (AML_RESOURCE_EXTENDED_IRQ) +
        1 + OptionIndex + StringLength);

    Descriptor = Rnode->Buffer;
    Descriptor->ExtendedIrq.DescriptorType = ACPI_RESOURCE_NAME_EXTENDED_IRQ;

    /*
     * Initial descriptor length -- may be enlarged if there are
     * optional fields present
     */
    Descriptor->ExtendedIrq.ResourceLength  = 2;  /* Flags and table length byte */
    Descriptor->ExtendedIrq.InterruptCount  = 0;

    Rover = ACPI_CAST_PTR (AML_RESOURCE,
        (&(Descriptor->ExtendedIrq.Interrupts[0])));

    /* Process all child initialization nodes */

    for (i = 0; InitializerOp; i++)
    {
        switch (i)
        {
        case 0: /* Resource Usage (Default: consumer (1) */

            RsSetFlagBits (&Descriptor->ExtendedIrq.Flags, InitializerOp, 0, 1);
            break;

        case 1: /* Interrupt Type (or Mode - edge/level) */

            RsSetFlagBits (&Descriptor->ExtendedIrq.Flags, InitializerOp, 1, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_INTERRUPTTYPE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (ExtendedIrq.Flags), 1);
            break;

        case 2: /* Interrupt Level (or Polarity - Active high/low) */

            RsSetFlagBits (&Descriptor->ExtendedIrq.Flags, InitializerOp, 2, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_INTERRUPTLEVEL,
                CurrentByteOffset + ASL_RESDESC_OFFSET (ExtendedIrq.Flags), 2);
            break;

        case 3: /* Share Type - Default: exclusive (0) */

            RsSetFlagBits (&Descriptor->ExtendedIrq.Flags, InitializerOp, 3, 0);
            RsCreateBitField (InitializerOp, ACPI_RESTAG_INTERRUPTSHARE,
                CurrentByteOffset + ASL_RESDESC_OFFSET (ExtendedIrq.Flags), 3);
            break;

        case 4: /* ResSourceIndex [Optional Field - BYTE] */

            if (InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
            {
                HasResSourceIndex = TRUE;
                ResSourceIndex = (UINT8) InitializerOp->Asl.Value.Integer;
            }
            break;

        case 5: /* ResSource [Optional Field - STRING] */

            if ((InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG) &&
                (InitializerOp->Asl.Value.String))
            {
                if (StringLength)
                {
                    ResSourceString = (UINT8 *) InitializerOp->Asl.Value.String;
                }

                /* ResourceSourceIndex must also be valid */

                if (!HasResSourceIndex)
                {
                    AslError (ASL_ERROR, ASL_MSG_RESOURCE_INDEX,
                        InitializerOp, NULL);
                }
            }

#if 0
            /*
             * Not a valid ResourceSource, ResourceSourceIndex must also
             * be invalid
             */
            else if (HasResSourceIndex)
            {
                AslError (ASL_ERROR, ASL_MSG_RESOURCE_SOURCE,
                    InitializerOp, NULL);
            }
#endif
            break;

        case 6: /* ResourceTag */

            UtAttachNamepathToOwner (Info->DescriptorTypeOp, InitializerOp);
            break;

        default:
            /*
             * Interrupt Numbers come through here, repeatedly
             */

            /* Maximum 255 interrupts allowed for this descriptor */

            if (Descriptor->ExtendedIrq.InterruptCount == 255)
            {
                AslError (ASL_ERROR, ASL_MSG_EX_INTERRUPT_LIST,
                    InitializerOp, NULL);
                return (Rnode);
            }

            /* Each interrupt number must be a 32-bit value */

            if (InitializerOp->Asl.Value.Integer > ACPI_UINT32_MAX)
            {
                AslError (ASL_ERROR, ASL_MSG_EX_INTERRUPT_NUMBER,
                    InitializerOp, NULL);
            }

            /* Save the integer and move pointer to the next one */

            Rover->DwordItem = (UINT32) InitializerOp->Asl.Value.Integer;
            Rover = ACPI_ADD_PTR (AML_RESOURCE, &(Rover->DwordItem), 4);
            Descriptor->ExtendedIrq.InterruptCount++;
            Descriptor->ExtendedIrq.ResourceLength += 4;

            /* Case 7: First interrupt number in list */

            if (i == 7)
            {
                if (InitializerOp->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
                {
                    /* Must be at least one interrupt */

                    AslError (ASL_ERROR, ASL_MSG_EX_INTERRUPT_LIST_MIN,
                        InitializerOp, NULL);
                }

                /* Check now for duplicates in list */

                RsCheckListForDuplicates (InitializerOp);

                /* Create a named field at the start of the list */

                RsCreateDwordField (InitializerOp, ACPI_RESTAG_INTERRUPT,
                    CurrentByteOffset +
                    ASL_RESDESC_OFFSET (ExtendedIrq.Interrupts[0]));
            }
        }

        InitializerOp = RsCompleteNodeAndGetNext (InitializerOp);
    }


    /* Add optional ResSourceIndex if present */

    if (HasResSourceIndex)
    {
        Rover->ByteItem = ResSourceIndex;
        Rover = ACPI_ADD_PTR (AML_RESOURCE, &(Rover->ByteItem), 1);
        Descriptor->ExtendedIrq.ResourceLength += 1;
    }

    /* Add optional ResSource string if present */

    if (StringLength && ResSourceString)
    {

        strcpy ((char *) Rover, (char *) ResSourceString);
        Rover = ACPI_ADD_PTR (
                    AML_RESOURCE, &(Rover->ByteItem), StringLength);

        Descriptor->ExtendedIrq.ResourceLength = (UINT16)
            (Descriptor->ExtendedIrq.ResourceLength + StringLength);
    }

    Rnode->BufferLength =
        (ASL_RESDESC_OFFSET (ExtendedIrq.Interrupts[0]) -
        ASL_RESDESC_OFFSET (ExtendedIrq.DescriptorType))
        + OptionIndex + StringLength;
    return (Rnode);
}


/*******************************************************************************
 *
 * FUNCTION:    RsDoVendorLargeDescriptor
 *
 * PARAMETERS:  Info                - Parse Op and resource template offset
 *
 * RETURN:      Completed resource node
 *
 * DESCRIPTION: Construct a long "VendorLong" descriptor
 *
 ******************************************************************************/

ASL_RESOURCE_NODE *
RsDoVendorLargeDescriptor (
    ASL_RESOURCE_INFO       *Info)
{
    AML_RESOURCE            *Descriptor;
    ACPI_PARSE_OBJECT       *InitializerOp;
    ASL_RESOURCE_NODE       *Rnode;
    UINT8                   *VendorData;
    UINT32                  i;


    /* Count the number of data bytes */

    InitializerOp = Info->DescriptorTypeOp->Asl.Child;
    InitializerOp = RsCompleteNodeAndGetNext (InitializerOp);

    for (i = 0; InitializerOp; i++)
    {
        if (InitializerOp->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
        {
            break;
        }
        InitializerOp = InitializerOp->Asl.Next;
    }

    InitializerOp = Info->DescriptorTypeOp->Asl.Child;
    InitializerOp = RsCompleteNodeAndGetNext (InitializerOp);
    Rnode = RsAllocateResourceNode (sizeof (AML_RESOURCE_VENDOR_LARGE) + i);

    Descriptor = Rnode->Buffer;
    Descriptor->VendorLarge.DescriptorType = ACPI_RESOURCE_NAME_VENDOR_LARGE;
    Descriptor->VendorLarge.ResourceLength = (UINT16) i;

    /* Point to end-of-descriptor for vendor data */

    VendorData = ((UINT8 *) Descriptor) + sizeof (AML_RESOURCE_LARGE_HEADER);

    /* Process all child initialization nodes */

    for (i = 0; InitializerOp; i++)
    {
        if (InitializerOp->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
        {
            break;
        }

        VendorData[i] = (UINT8) InitializerOp->Asl.Value.Integer;
        InitializerOp = RsCompleteNodeAndGetNext (InitializerOp);
    }

    return (Rnode);
}
