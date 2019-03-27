/******************************************************************************
 *
 * Module Name: aslresource - Resource template/descriptor utilities
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
        ACPI_MODULE_NAME    ("aslresource")


/*******************************************************************************
 *
 * FUNCTION:    RsSmallAddressCheck
 *
 * PARAMETERS:  Minimum             - Address Min value
 *              Maximum             - Address Max value
 *              Length              - Address range value
 *              Alignment           - Address alignment value
 *              MinOp               - Original Op for Address Min
 *              MaxOp               - Original Op for Address Max
 *              LengthOp            - Original Op for address range
 *              AlignOp             - Original Op for address alignment. If
 *                                    NULL, means "zero value for alignment is
 *                                    OK, and means 64K alignment" (for
 *                                    Memory24 descriptor)
 *              Op                  - Parent Op for entire construct
 *
 * RETURN:      None. Adds error messages to error log if necessary
 *
 * DESCRIPTION: Perform common value checks for "small" address descriptors.
 *              Currently:
 *                  Io, Memory24, Memory32
 *
 ******************************************************************************/

void
RsSmallAddressCheck (
    UINT8                   Type,
    UINT32                  Minimum,
    UINT32                  Maximum,
    UINT32                  Length,
    UINT32                  Alignment,
    ACPI_PARSE_OBJECT       *MinOp,
    ACPI_PARSE_OBJECT       *MaxOp,
    ACPI_PARSE_OBJECT       *LengthOp,
    ACPI_PARSE_OBJECT       *AlignOp,
    ACPI_PARSE_OBJECT       *Op)
{

    if (AslGbl_NoResourceChecking)
    {
        return;
    }

    /*
     * Check for a so-called "null descriptor". These are descriptors that are
     * created with most fields set to zero. The intent is that the descriptor
     * will be updated/completed at runtime via a BufferField.
     *
     * If the descriptor does NOT have a resource tag, it cannot be referenced
     * by a BufferField and we will flag this as an error. Conversely, if
     * the descriptor has a resource tag, we will assume that a BufferField
     * will be used to dynamically update it, so no error.
     *
     * A possible enhancement to this check would be to verify that in fact
     * a BufferField is created using the resource tag, and perhaps even
     * verify that a Store is performed to the BufferField.
     *
     * Note: for these descriptors, Alignment is allowed to be zero
     */
    if (!Minimum && !Maximum && !Length)
    {
        if (!Op->Asl.ExternalName)
        {
            /* No resource tag. Descriptor is fixed and is also illegal */

            AslError (ASL_ERROR, ASL_MSG_NULL_DESCRIPTOR, Op, NULL);
        }

        return;
    }

    /*
     * Range checks for Memory24 and Memory32.
     * IO descriptor has different definition of min/max, don't check.
     */
    if (Type != ACPI_RESOURCE_NAME_IO)
    {
        /* Basic checks on Min/Max/Length */

        if (Minimum > Maximum)
        {
            AslError (ASL_ERROR, ASL_MSG_INVALID_MIN_MAX, MinOp, NULL);
        }
        else if (Length > (Maximum - Minimum + 1))
        {
            AslError (ASL_ERROR, ASL_MSG_INVALID_LENGTH, LengthOp, NULL);
        }

        /* Special case for Memory24, min/max values are compressed */

        if (Type == ACPI_RESOURCE_NAME_MEMORY24)
        {
            if (!Alignment) /* Alignment==0 means 64K alignment */
            {
                Alignment = ACPI_UINT16_MAX + 1;
            }

            Minimum <<= 8;
            Maximum <<= 8;
        }
    }

    /* Alignment of zero is not in ACPI spec, but is used to mean byte acc */

    if (!Alignment)
    {
        Alignment = 1;
    }

    /* Addresses must be an exact multiple of the alignment value */

    if (Minimum % Alignment)
    {
        AslError (ASL_ERROR, ASL_MSG_ALIGNMENT, MinOp, NULL);
    }
    if (Maximum % Alignment)
    {
        AslError (ASL_ERROR, ASL_MSG_ALIGNMENT, MaxOp, NULL);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    RsLargeAddressCheck
 *
 * PARAMETERS:  Minimum             - Address Min value
 *              Maximum             - Address Max value
 *              Length              - Address range value
 *              Granularity         - Address granularity value
 *              Flags               - General flags for address descriptors:
 *                                    _MIF, _MAF, _DEC
 *              MinOp               - Original Op for Address Min
 *              MaxOp               - Original Op for Address Max
 *              LengthOp            - Original Op for address range
 *              GranOp              - Original Op for address granularity
 *              Op                  - Parent Op for entire construct
 *
 * RETURN:      None. Adds error messages to error log if necessary
 *
 * DESCRIPTION: Perform common value checks for "large" address descriptors.
 *              Currently:
 *                  WordIo,     WordBusNumber,  WordSpace
 *                  DWordIo,    DWordMemory,    DWordSpace
 *                  QWordIo,    QWordMemory,    QWordSpace
 *                  ExtendedIo, ExtendedMemory, ExtendedSpace
 *
 * _MIF flag set means that the minimum address is fixed and is not relocatable
 * _MAF flag set means that the maximum address is fixed and is not relocatable
 * Length of zero means that the record size is variable
 *
 * This function implements the LEN/MIF/MAF/MIN/MAX/GRA rules within Table 6-40
 * of the ACPI 4.0a specification. Added 04/2010.
 *
 ******************************************************************************/

void
RsLargeAddressCheck (
    UINT64                  Minimum,
    UINT64                  Maximum,
    UINT64                  Length,
    UINT64                  Granularity,
    UINT8                   Flags,
    ACPI_PARSE_OBJECT       *MinOp,
    ACPI_PARSE_OBJECT       *MaxOp,
    ACPI_PARSE_OBJECT       *LengthOp,
    ACPI_PARSE_OBJECT       *GranOp,
    ACPI_PARSE_OBJECT       *Op)
{

    if (AslGbl_NoResourceChecking)
    {
        return;
    }

    /*
     * Check for a so-called "null descriptor". These are descriptors that are
     * created with most fields set to zero. The intent is that the descriptor
     * will be updated/completed at runtime via a BufferField.
     *
     * If the descriptor does NOT have a resource tag, it cannot be referenced
     * by a BufferField and we will flag this as an error. Conversely, if
     * the descriptor has a resource tag, we will assume that a BufferField
     * will be used to dynamically update it, so no error.
     *
     * A possible enhancement to this check would be to verify that in fact
     * a BufferField is created using the resource tag, and perhaps even
     * verify that a Store is performed to the BufferField.
     */
    if (!Minimum && !Maximum && !Length && !Granularity)
    {
        if (!Op->Asl.ExternalName)
        {
            /* No resource tag. Descriptor is fixed and is also illegal */

            AslError (ASL_ERROR, ASL_MSG_NULL_DESCRIPTOR, Op, NULL);
        }

        return;
    }

    /* Basic checks on Min/Max/Length */

    if (Minimum > Maximum)
    {
        AslError (ASL_ERROR, ASL_MSG_INVALID_MIN_MAX, MinOp, NULL);
        return;
    }
    else if (Length > (Maximum - Minimum + 1))
    {
        AslError (ASL_ERROR, ASL_MSG_INVALID_LENGTH, LengthOp, NULL);
        return;
    }

    /* If specified (non-zero), ensure granularity is a power-of-two minus one */

    if (Granularity)
    {
        if ((Granularity + 1) &
             Granularity)
        {
            AslError (ASL_ERROR, ASL_MSG_INVALID_GRANULARITY, GranOp, NULL);
            return;
        }
    }

    /*
     * Check the various combinations of Length, MinFixed, and MaxFixed
     */
    if (Length)
    {
        /* Fixed non-zero length */

        switch (Flags & (ACPI_RESOURCE_FLAG_MIF | ACPI_RESOURCE_FLAG_MAF))
        {
        case 0:
            /*
             * Fixed length, variable locations (both _MIN and _MAX).
             * Length must be a multiple of granularity
             */
            if (Granularity & Length)
            {
                AslError (ASL_ERROR, ASL_MSG_ALIGNMENT, LengthOp, NULL);
            }
            break;

        case (ACPI_RESOURCE_FLAG_MIF | ACPI_RESOURCE_FLAG_MAF):

            /* Fixed length, fixed location. Granularity must be zero */

            if (Granularity != 0)
            {
                AslError (ASL_ERROR, ASL_MSG_INVALID_GRAN_FIXED, GranOp, NULL);
            }

            /* Length must be exactly the size of the min/max window */

            if (Length != (Maximum - Minimum + 1))
            {
                AslError (ASL_ERROR, ASL_MSG_INVALID_LENGTH_FIXED, LengthOp, NULL);
            }
            break;

        /* All other combinations are invalid */

        case ACPI_RESOURCE_FLAG_MIF:
        case ACPI_RESOURCE_FLAG_MAF:
        default:

            AslError (ASL_ERROR, ASL_MSG_INVALID_ADDR_FLAGS, LengthOp, NULL);
        }
    }
    else
    {
        /* Variable length (length==0) */

        switch (Flags & (ACPI_RESOURCE_FLAG_MIF | ACPI_RESOURCE_FLAG_MAF))
        {
        case 0:
            /*
             * Both _MIN and _MAX are variable.
             * No additional requirements, just exit
             */
            break;

        case ACPI_RESOURCE_FLAG_MIF:

            /* _MIN is fixed. _MIN must be multiple of _GRA */

            /*
             * The granularity is defined by the ACPI specification to be a
             * power-of-two minus one, therefore the granularity is a
             * bitmask which can be used to easily validate the addresses.
             */
            if (Granularity & Minimum)
            {
                AslError (ASL_ERROR, ASL_MSG_ALIGNMENT, MinOp, NULL);
            }
            break;

        case ACPI_RESOURCE_FLAG_MAF:

            /* _MAX is fixed. (_MAX + 1) must be multiple of _GRA */

            if (Granularity & (Maximum + 1))
            {
                AslError (ASL_ERROR, ASL_MSG_ALIGNMENT, MaxOp, "-1");
            }
            break;

        /* Both MIF/MAF set is invalid if length is zero */

        case (ACPI_RESOURCE_FLAG_MIF | ACPI_RESOURCE_FLAG_MAF):
        default:

            AslError (ASL_ERROR, ASL_MSG_INVALID_ADDR_FLAGS, LengthOp, NULL);
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    RsGetStringDataLength
 *
 * PARAMETERS:  InitializerOp     - Start of a subtree of init nodes
 *
 * RETURN:      Valid string length if a string node is found (otherwise 0)
 *
 * DESCRIPTION: In a list of peer nodes, find the first one that contains a
 *              string and return the length of the string.
 *
 ******************************************************************************/

UINT16
RsGetStringDataLength (
    ACPI_PARSE_OBJECT       *InitializerOp)
{

    while (InitializerOp)
    {
        if (InitializerOp->Asl.ParseOpcode == PARSEOP_STRING_LITERAL)
        {
            return ((UINT16) (strlen (InitializerOp->Asl.Value.String) + 1));
        }

        InitializerOp = ASL_GET_PEER_NODE (InitializerOp);
    }

    return (0);
}


/*******************************************************************************
 *
 * FUNCTION:    RsAllocateResourceNode
 *
 * PARAMETERS:  Size        - Size of node in bytes
 *
 * RETURN:      The allocated node - aborts on allocation failure
 *
 * DESCRIPTION: Allocate a resource description node and the resource
 *              descriptor itself (the nodes are used to link descriptors).
 *
 ******************************************************************************/

ASL_RESOURCE_NODE *
RsAllocateResourceNode (
    UINT32                  Size)
{
    ASL_RESOURCE_NODE       *Rnode;


    /* Allocate the node */

    Rnode = UtLocalCalloc (sizeof (ASL_RESOURCE_NODE));

    /* Allocate the resource descriptor itself */

    Rnode->Buffer = UtLocalCalloc (Size);
    Rnode->BufferLength = Size;
    return (Rnode);
}


/*******************************************************************************
 *
 * FUNCTION:    RsCreateResourceField
 *
 * PARAMETERS:  Op              - Resource field node
 *              Name            - Name of the field (Used only to reference
 *                                the field in the ASL, not in the AML)
 *              ByteOffset      - Offset from the field start
 *              BitOffset       - Additional bit offset
 *              BitLength       - Number of bits in the field
 *
 * RETURN:      None, sets fields within the input node
 *
 * DESCRIPTION: Utility function to generate a named bit field within a
 *              resource descriptor. Mark a node as 1) a field in a resource
 *              descriptor, and 2) set the value to be a BIT offset
 *
 ******************************************************************************/

void
RsCreateResourceField (
    ACPI_PARSE_OBJECT       *Op,
    char                    *Name,
    UINT32                  ByteOffset,
    UINT32                  BitOffset,
    UINT32                  BitLength)
{

    Op->Asl.ExternalName = Name;
    Op->Asl.CompileFlags |= OP_IS_RESOURCE_FIELD;

    Op->Asl.Value.Tag.BitOffset = (ByteOffset * 8) + BitOffset;
    Op->Asl.Value.Tag.BitLength = BitLength;
}


/*******************************************************************************
 *
 * FUNCTION:    RsSetFlagBits
 *
 * PARAMETERS:  *Flags          - Pointer to the flag byte
 *              Op              - Flag initialization node
 *              Position        - Bit position within the flag byte
 *              Default         - Used if the node is DEFAULT.
 *
 * RETURN:      Sets bits within the *Flags output byte.
 *
 * DESCRIPTION: Set a bit in a cumulative flags word from an initialization
 *              node. Will use a default value if the node is DEFAULT, meaning
 *              that no value was specified in the ASL. Used to merge multiple
 *              keywords into a single flags byte.
 *
 ******************************************************************************/

void
RsSetFlagBits (
    UINT8                   *Flags,
    ACPI_PARSE_OBJECT       *Op,
    UINT8                   Position,
    UINT8                   DefaultBit)
{

    if (Op->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
    {
        /* Use the default bit */

        *Flags |= (DefaultBit << Position);
    }
    else
    {
        /* Use the bit specified in the initialization node */

        *Flags |= (((UINT8) Op->Asl.Value.Integer) << Position);
    }
}


void
RsSetFlagBits16 (
    UINT16                  *Flags,
    ACPI_PARSE_OBJECT       *Op,
    UINT8                   Position,
    UINT8                   DefaultBit)
{

    if (Op->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
    {
        /* Use the default bit */

        *Flags |= (DefaultBit << Position);
    }
    else
    {
        /* Use the bit specified in the initialization node */

        *Flags |= (((UINT16) Op->Asl.Value.Integer) << Position);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    RsCompleteNodeAndGetNext
 *
 * PARAMETERS:  Op            - Resource node to be completed
 *
 * RETURN:      The next peer to the input node.
 *
 * DESCRIPTION: Mark the current node completed and return the next peer.
 *              The node ParseOpcode is set to DEFAULT_ARG, meaning that
 *              this node is to be ignored from now on.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
RsCompleteNodeAndGetNext (
    ACPI_PARSE_OBJECT       *Op)
{

    /* Mark this node unused */

    Op->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;

    /* Move on to the next peer node in the initializer list */

    return (ASL_GET_PEER_NODE (Op));
}


/*******************************************************************************
 *
 * FUNCTION:    RsCheckListForDuplicates
 *
 * PARAMETERS:  Op                  - First op in the initializer list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check an initializer list for duplicate values. Emits an error
 *              if any duplicates are found.
 *
 ******************************************************************************/

void
RsCheckListForDuplicates (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *NextValueOp = Op;
    ACPI_PARSE_OBJECT       *NextOp;
    UINT32                  Value;


    if (!Op)
    {
        return;
    }

    /* Search list once for each value in the list */

    while (NextValueOp)
    {
        Value = (UINT32) NextValueOp->Asl.Value.Integer;

        /* Compare this value to all remaining values in the list */

        NextOp = ASL_GET_PEER_NODE (NextValueOp);
        while (NextOp)
        {
            if (NextOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
            {
                /* Compare values */

                if (Value == (UINT32) NextOp->Asl.Value.Integer)
                {
                    /* Emit error only once per duplicate node */

                    if (!(NextOp->Asl.CompileFlags & OP_IS_DUPLICATE))
                    {
                        NextOp->Asl.CompileFlags |= OP_IS_DUPLICATE;
                        AslError (ASL_ERROR, ASL_MSG_DUPLICATE_ITEM,
                            NextOp, NULL);
                    }
                }
            }

            NextOp = ASL_GET_PEER_NODE (NextOp);
        }

        NextValueOp = ASL_GET_PEER_NODE (NextValueOp);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    RsDoOneResourceDescriptor
 *
 * PARAMETERS:  DescriptorTypeOp    - Parent parse node of the descriptor
 *              CurrentByteOffset   - Offset in the resource descriptor
 *                                    buffer.
 *
 * RETURN:      A valid resource node for the descriptor
 *
 * DESCRIPTION: Dispatches the processing of one resource descriptor
 *
 ******************************************************************************/

ASL_RESOURCE_NODE *
RsDoOneResourceDescriptor (
    ASL_RESOURCE_INFO       *Info,
    UINT8                   *State)
{
    ASL_RESOURCE_NODE       *Rnode = NULL;


    /* Construct the resource */

    switch (Info->DescriptorTypeOp->Asl.ParseOpcode)
    {
    case PARSEOP_DMA:

        Rnode = RsDoDmaDescriptor (Info);
        break;

    case PARSEOP_FIXEDDMA:

        Rnode = RsDoFixedDmaDescriptor (Info);
        break;

    case PARSEOP_DWORDIO:

        Rnode = RsDoDwordIoDescriptor (Info);
        break;

    case PARSEOP_DWORDMEMORY:

        Rnode = RsDoDwordMemoryDescriptor (Info);
        break;

    case PARSEOP_DWORDSPACE:

        Rnode = RsDoDwordSpaceDescriptor (Info);
        break;

    case PARSEOP_ENDDEPENDENTFN:

        switch (*State)
        {
        case ACPI_RSTATE_NORMAL:

            AslError (ASL_ERROR, ASL_MSG_MISSING_STARTDEPENDENT,
                Info->DescriptorTypeOp, NULL);
            break;

        case ACPI_RSTATE_START_DEPENDENT:

            AslError (ASL_ERROR, ASL_MSG_DEPENDENT_NESTING,
                Info->DescriptorTypeOp, NULL);
            break;

        case ACPI_RSTATE_DEPENDENT_LIST:
        default:

            break;
        }

        *State = ACPI_RSTATE_NORMAL;
        Rnode = RsDoEndDependentDescriptor (Info);
        break;

    case PARSEOP_ENDTAG:

        Rnode = RsDoEndTagDescriptor (Info);
        break;

    case PARSEOP_EXTENDEDIO:

        Rnode = RsDoExtendedIoDescriptor (Info);
        break;

    case PARSEOP_EXTENDEDMEMORY:

        Rnode = RsDoExtendedMemoryDescriptor (Info);
        break;

    case PARSEOP_EXTENDEDSPACE:

        Rnode = RsDoExtendedSpaceDescriptor (Info);
        break;

    case PARSEOP_FIXEDIO:

        Rnode = RsDoFixedIoDescriptor (Info);
        break;

    case PARSEOP_INTERRUPT:

        Rnode = RsDoInterruptDescriptor (Info);
        break;

    case PARSEOP_IO:

        Rnode = RsDoIoDescriptor (Info);
        break;

    case PARSEOP_IRQ:

        Rnode = RsDoIrqDescriptor (Info);
        break;

    case PARSEOP_IRQNOFLAGS:

        Rnode = RsDoIrqNoFlagsDescriptor (Info);
        break;

    case PARSEOP_MEMORY24:

        Rnode = RsDoMemory24Descriptor (Info);
        break;

    case PARSEOP_MEMORY32:

        Rnode = RsDoMemory32Descriptor (Info);
        break;

    case PARSEOP_MEMORY32FIXED:

        Rnode = RsDoMemory32FixedDescriptor (Info);
        break;

    case PARSEOP_QWORDIO:

        Rnode = RsDoQwordIoDescriptor (Info);
        break;

    case PARSEOP_QWORDMEMORY:

        Rnode = RsDoQwordMemoryDescriptor (Info);
        break;

    case PARSEOP_QWORDSPACE:

        Rnode = RsDoQwordSpaceDescriptor (Info);
        break;

    case PARSEOP_REGISTER:

        Rnode = RsDoGeneralRegisterDescriptor (Info);
        break;

    case PARSEOP_STARTDEPENDENTFN:

        switch (*State)
        {
        case ACPI_RSTATE_START_DEPENDENT:

            AslError (ASL_ERROR, ASL_MSG_DEPENDENT_NESTING,
                Info->DescriptorTypeOp, NULL);
            break;

        case ACPI_RSTATE_NORMAL:
        case ACPI_RSTATE_DEPENDENT_LIST:
        default:

            break;
        }

        *State = ACPI_RSTATE_START_DEPENDENT;
        Rnode = RsDoStartDependentDescriptor (Info);
        *State = ACPI_RSTATE_DEPENDENT_LIST;
        break;

    case PARSEOP_STARTDEPENDENTFN_NOPRI:

        switch (*State)
        {
        case ACPI_RSTATE_START_DEPENDENT:

            AslError (ASL_ERROR, ASL_MSG_DEPENDENT_NESTING,
                Info->DescriptorTypeOp, NULL);
            break;

        case ACPI_RSTATE_NORMAL:
        case ACPI_RSTATE_DEPENDENT_LIST:
        default:

            break;
        }

        *State = ACPI_RSTATE_START_DEPENDENT;
        Rnode = RsDoStartDependentNoPriDescriptor (Info);
        *State = ACPI_RSTATE_DEPENDENT_LIST;
        break;

    case PARSEOP_VENDORLONG:

        Rnode = RsDoVendorLargeDescriptor (Info);
        break;

    case PARSEOP_VENDORSHORT:

        Rnode = RsDoVendorSmallDescriptor (Info);
        break;

    case PARSEOP_WORDBUSNUMBER:

        Rnode = RsDoWordBusNumberDescriptor (Info);
        break;

    case PARSEOP_WORDIO:

        Rnode = RsDoWordIoDescriptor (Info);
        break;

    case PARSEOP_WORDSPACE:

        Rnode = RsDoWordSpaceDescriptor (Info);
        break;

    case PARSEOP_GPIO_INT:

        Rnode = RsDoGpioIntDescriptor (Info);
        break;

    case PARSEOP_GPIO_IO:

        Rnode = RsDoGpioIoDescriptor (Info);
        break;

    case PARSEOP_I2C_SERIALBUS:
    case PARSEOP_I2C_SERIALBUS_V2:

        Rnode = RsDoI2cSerialBusDescriptor (Info);
        break;

    case PARSEOP_SPI_SERIALBUS:
    case PARSEOP_SPI_SERIALBUS_V2:

        Rnode = RsDoSpiSerialBusDescriptor (Info);
        break;

    case PARSEOP_UART_SERIALBUS:
    case PARSEOP_UART_SERIALBUS_V2:

        Rnode = RsDoUartSerialBusDescriptor (Info);
        break;

    case PARSEOP_PINCONFIG:

        Rnode = RsDoPinConfigDescriptor (Info);
        break;

    case PARSEOP_PINFUNCTION:

        Rnode = RsDoPinFunctionDescriptor (Info);
        break;

    case PARSEOP_PINGROUP:

        Rnode = RsDoPinGroupDescriptor (Info);
        break;

    case PARSEOP_PINGROUPFUNCTION:

        Rnode = RsDoPinGroupFunctionDescriptor (Info);
        break;

    case PARSEOP_PINGROUPCONFIG:

        Rnode = RsDoPinGroupConfigDescriptor (Info);
        break;

    case PARSEOP_DEFAULT_ARG:

        /* Just ignore any of these, they are used as fillers/placeholders */
        break;

    default:

        printf ("Unknown resource descriptor type [%s]\n",
            Info->DescriptorTypeOp->Asl.ParseOpName);
        break;
    }

    /*
     * Mark original node as unused, but head of a resource descriptor.
     * This allows the resource to be installed in the namespace so that
     * references to the descriptor can be resolved.
     */
    Info->DescriptorTypeOp->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
    Info->DescriptorTypeOp->Asl.CompileFlags = OP_IS_RESOURCE_DESC;
    Info->DescriptorTypeOp->Asl.Value.Integer = Info->CurrentByteOffset;

    if (Rnode)
    {
        Info->DescriptorTypeOp->Asl.FinalAmlLength = Rnode->BufferLength;
        Info->DescriptorTypeOp->Asl.Extra =
            ((AML_RESOURCE *) Rnode->Buffer)->DescriptorType;
    }

    return (Rnode);
}


/*******************************************************************************
 *
 * FUNCTION:    RsLinkDescriptorChain
 *
 * PARAMETERS:  PreviousRnode       - Pointer to the node that will be previous
 *                                    to the linked node,  At exit, set to the
 *                                    last node in the new chain.
 *              Rnode               - Resource node to link into the list
 *
 * RETURN:      Cumulative buffer byte offset of the new segment of chain
 *
 * DESCRIPTION: Link a descriptor chain at the end of an existing chain.
 *
 ******************************************************************************/

UINT32
RsLinkDescriptorChain (
    ASL_RESOURCE_NODE       **PreviousRnode,
    ASL_RESOURCE_NODE       *Rnode)
{
    ASL_RESOURCE_NODE       *LastRnode;
    UINT32                  CurrentByteOffset;


    /* Anything to do? */

    if (!Rnode)
    {
        return (0);
    }

    /* Point the previous node to the new node */

    (*PreviousRnode)->Next = Rnode;
    CurrentByteOffset = Rnode->BufferLength;

    /* Walk to the end of the chain headed by Rnode */

    LastRnode = Rnode;
    while (LastRnode->Next)
    {
        LastRnode = LastRnode->Next;
        CurrentByteOffset += LastRnode->BufferLength;
    }

    /* Previous node becomes the last node in the chain */

    *PreviousRnode = LastRnode;
    return (CurrentByteOffset);
}


/*******************************************************************************
 *
 * FUNCTION:    RsDoResourceTemplate
 *
 * PARAMETERS:  Op        - Parent of a resource template list
 *
 * RETURN:      None. Sets input node to point to a list of AML code
 *
 * DESCRIPTION: Merge a list of resource descriptors into a single AML buffer,
 *              in preparation for output to the AML output file.
 *
 ******************************************************************************/

void
RsDoResourceTemplate (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *BufferLengthOp;
    ACPI_PARSE_OBJECT       *BufferOp;
    ACPI_PARSE_OBJECT       *DescriptorTypeOp;
    ACPI_PARSE_OBJECT       *LastOp = NULL;
    UINT32                  CurrentByteOffset = 0;
    ASL_RESOURCE_NODE       HeadRnode;
    ASL_RESOURCE_NODE       *PreviousRnode;
    ASL_RESOURCE_NODE       *Rnode;
    ASL_RESOURCE_INFO       Info;
    UINT8                   State;


    /* Mark parent as containing a resource template */

    if (Op->Asl.Parent)
    {
        Op->Asl.Parent->Asl.CompileFlags |= OP_IS_RESOURCE_DESC;
    }

    /* ResourceTemplate Opcode is first (Op) */
    /* Buffer Length node is first child */

    BufferLengthOp = ASL_GET_CHILD_NODE (Op);

    /* Buffer Op is first peer */

    BufferOp = ASL_GET_PEER_NODE (BufferLengthOp);

    /* First Descriptor type is next */

    DescriptorTypeOp = ASL_GET_PEER_NODE (BufferOp);

    /* DEFAULT_ARG indicates null template - ResourceTemplate(){} */

    if (DescriptorTypeOp->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
    {
        AslError (ASL_WARNING, ASL_MSG_NULL_RESOURCE_TEMPLATE,
            DescriptorTypeOp, DescriptorTypeOp->Asl.Value.String);
    }

    /*
     * Process all resource descriptors in the list
     * Note: It is assumed that the EndTag node has been automatically
     * inserted at the end of the template by the parser.
     */
    State = ACPI_RSTATE_NORMAL;
    PreviousRnode = &HeadRnode;
    while (DescriptorTypeOp)
    {
        /* Save information for optional mapfile */

        if (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CONNECTION)
        {
            Info.MappingOp = Op->Asl.Parent;
        }
        else
        {
            Info.MappingOp = DescriptorTypeOp;
        }

        Info.DescriptorTypeOp = DescriptorTypeOp;
        Info.CurrentByteOffset = CurrentByteOffset;

        DescriptorTypeOp->Asl.CompileFlags |= OP_IS_RESOURCE_DESC;
        Rnode = RsDoOneResourceDescriptor (&Info, &State);

        /*
         * Update current byte offset to indicate the number of bytes from the
         * start of the buffer. Buffer can include multiple descriptors, we
         * must keep track of the offset of not only each descriptor, but each
         * element (field) within each descriptor as well.
         */
        CurrentByteOffset += RsLinkDescriptorChain (&PreviousRnode, Rnode);

        /* Get the next descriptor in the list */

        LastOp = DescriptorTypeOp;
        DescriptorTypeOp = ASL_GET_PEER_NODE (DescriptorTypeOp);
    }

    if (State == ACPI_RSTATE_DEPENDENT_LIST)
    {
        if (LastOp)
        {
            LastOp = LastOp->Asl.Parent;
        }
        AslError (ASL_ERROR, ASL_MSG_MISSING_ENDDEPENDENT, LastOp, NULL);
    }

    /*
     * Transform the nodes into the following
     *
     * Op           -> AML_BUFFER_OP
     * First Child  -> BufferLength
     * Second Child -> Descriptor Buffer (raw byte data)
     */
    Op->Asl.ParseOpcode               = PARSEOP_BUFFER;
    Op->Asl.AmlOpcode                 = AML_BUFFER_OP;
    Op->Asl.CompileFlags              = OP_AML_PACKAGE | OP_IS_RESOURCE_DESC;
    UtSetParseOpName (Op);

    BufferLengthOp->Asl.ParseOpcode   = PARSEOP_INTEGER;
    BufferLengthOp->Asl.Value.Integer = CurrentByteOffset;
    (void) OpcSetOptimalIntegerSize (BufferLengthOp);
    UtSetParseOpName (BufferLengthOp);

    BufferOp->Asl.ParseOpcode         = PARSEOP_RAW_DATA;
    BufferOp->Asl.AmlOpcode           = AML_RAW_DATA_CHAIN;
    BufferOp->Asl.AmlOpcodeLength     = 0;
    BufferOp->Asl.AmlLength           = CurrentByteOffset;
    BufferOp->Asl.Value.Buffer        = (UINT8 *) HeadRnode.Next;
    BufferOp->Asl.CompileFlags       |= OP_IS_RESOURCE_DATA;
    UtSetParseOpName (BufferOp);

    return;
}
