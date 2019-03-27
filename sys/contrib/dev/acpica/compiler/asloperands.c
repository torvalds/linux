/******************************************************************************
 *
 * Module Name: asloperands - AML operand processing
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
        ACPI_MODULE_NAME    ("asloperands")

/* Local prototypes */

static void
OpnDoField (
    ACPI_PARSE_OBJECT       *Op);

static void
OpnDoBankField (
    ACPI_PARSE_OBJECT       *Op);

static void
OpnDoBuffer (
    ACPI_PARSE_OBJECT       *Op);

static void
OpnDoDefinitionBlock (
    ACPI_PARSE_OBJECT       *Op);

static void
OpnDoFieldCommon (
    ACPI_PARSE_OBJECT       *FieldOp,
    ACPI_PARSE_OBJECT       *Op);

static void
OpnDoIndexField (
    ACPI_PARSE_OBJECT       *Op);

static void
OpnDoLoadTable (
    ACPI_PARSE_OBJECT       *Op);

static void
OpnDoMethod (
    ACPI_PARSE_OBJECT       *Op);

static void
OpnDoMutex (
    ACPI_PARSE_OBJECT       *Op);

static void
OpnDoRegion (
    ACPI_PARSE_OBJECT       *Op);

static void
OpnAttachNameToNode (
    ACPI_PARSE_OBJECT       *Op);


/*******************************************************************************
 *
 * FUNCTION:    OpnDoMutex
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the operands for the MUTEX ASL keyword.
 *
 ******************************************************************************/

static void
OpnDoMutex (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;


    Next = Op->Asl.Child;
    Next = Next->Asl.Next;

    if (Next->Asl.Value.Integer > 15)
    {
        AslError (ASL_ERROR, ASL_MSG_SYNC_LEVEL, Next, NULL);
    }
    return;
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoMethod
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the operands for the METHOD ASL keyword.
 *
 ******************************************************************************/

static void
OpnDoMethod (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;

    /* Optional arguments for this opcode with defaults */

    UINT8                   NumArgs = 0;
    UINT8                   Serialized = 0;
    UINT8                   Concurrency = 0;
    UINT8                   MethodFlags;


    /* Opcode and package length first */
    /* Method name */

    Next = Op->Asl.Child;

    /* Num args */

    Next = Next->Asl.Next;
    if (Next->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
    {
        NumArgs = (UINT8) Next->Asl.Value.Integer;
        Next->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
    }

    /* Serialized Flag */

    Next = Next->Asl.Next;
    if (Next->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
    {
        Serialized = (UINT8) Next->Asl.Value.Integer;
        Next->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
    }

    /* Concurrency value (valid values are 0-15) */

    Next = Next->Asl.Next;
    if (Next->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
    {
        /* This is a ByteConstExpr, so eval the constant now */

        OpcAmlConstantWalk (Next, 0, NULL);

        if (Next->Asl.Value.Integer > 15)
        {
            AslError (ASL_ERROR, ASL_MSG_SYNC_LEVEL, Next, NULL);
        }

        Concurrency = (UINT8) Next->Asl.Value.Integer;
    }

    /* Put the bits in their proper places */

    MethodFlags = (UINT8)
        ((NumArgs & 0x7) |
        ((Serialized & 0x1) << 3) |
        ((Concurrency & 0xF) << 4));

    /* Use the last node for the combined flags byte */

    Next->Asl.Value.Integer = MethodFlags;
    Next->Asl.AmlOpcode = AML_RAW_DATA_BYTE;
    Next->Asl.AmlLength = 1;
    Next->Asl.ParseOpcode = PARSEOP_RAW_DATA;

    /* Save the arg count in the first node */

    Op->Asl.Extra = NumArgs;
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoFieldCommon
 *
 * PARAMETERS:  FieldOp       - Node for an ASL field
 *              Op            - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the AML operands for the various field keywords,
 *              FIELD, BANKFIELD, INDEXFIELD
 *
 ******************************************************************************/

static void
OpnDoFieldCommon (
    ACPI_PARSE_OBJECT       *FieldOp,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;
    ACPI_PARSE_OBJECT       *PkgLengthNode;
    UINT32                  CurrentBitOffset;
    UINT32                  NewBitOffset;
    UINT8                   AccessType;
    UINT8                   LockRule;
    UINT8                   UpdateRule;
    UINT8                   FieldFlags;
    UINT32                  MinimumLength;


    /* AccessType -- not optional, so no need to check for DEFAULT_ARG */

    AccessType = (UINT8) Op->Asl.Value.Integer;
    Op->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;

    /* Set the access type in the parent (field) node for use later */

    FieldOp->Asl.Value.Integer = AccessType;

    /* LockRule -- not optional, so no need to check for DEFAULT_ARG */

    Next = Op->Asl.Next;
    LockRule = (UINT8) Next->Asl.Value.Integer;
    Next->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;

    /* UpdateRule -- not optional, so no need to check for DEFAULT_ARG */

    Next = Next->Asl.Next;
    UpdateRule = (UINT8) Next->Asl.Value.Integer;

    /*
     * Generate the flags byte. The various fields are already
     * in the right bit position via translation from the
     * keywords by the parser.
     */
    FieldFlags = (UINT8) (AccessType | LockRule | UpdateRule);

    /* Use the previous node to be the FieldFlags node */

    /* Set the node to RAW_DATA */

    Next->Asl.Value.Integer = FieldFlags;
    Next->Asl.AmlOpcode = AML_RAW_DATA_BYTE;
    Next->Asl.AmlLength = 1;
    Next->Asl.ParseOpcode = PARSEOP_RAW_DATA;

    /* Process the FieldUnitList */

    Next = Next->Asl.Next;
    CurrentBitOffset = 0;

    while (Next)
    {
        /* Save the offset of this field unit */

        Next->Asl.ExtraValue = CurrentBitOffset;

        switch (Next->Asl.ParseOpcode)
        {
        case PARSEOP_ACCESSAS:

            PkgLengthNode = Next->Asl.Child;
            AccessType = (UINT8) PkgLengthNode->Asl.Value.Integer;

            /* Nothing additional to do */
            break;

        case PARSEOP_OFFSET:

            /* New offset into the field */

            PkgLengthNode = Next->Asl.Child;
            NewBitOffset = ((UINT32) PkgLengthNode->Asl.Value.Integer) * 8;

            /*
             * Examine the specified offset in relation to the
             * current offset counter.
             */
            if (NewBitOffset < CurrentBitOffset)
            {
                /*
                 * Not allowed to specify a backwards offset!
                 * Issue error and ignore this node.
                 */
                AslError (ASL_ERROR, ASL_MSG_BACKWARDS_OFFSET, PkgLengthNode,
                    NULL);
                Next->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
                PkgLengthNode->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
            }
            else if (NewBitOffset == CurrentBitOffset)
            {
                /*
                 * This Offset() operator is redundant and not needed,
                 * because the offset value is the same as the current
                 * offset.
                 */
                AslError (ASL_REMARK, ASL_MSG_OFFSET, PkgLengthNode, NULL);

                if (AslGbl_OptimizeTrivialParseNodes)
                {
                    /*
                     * Optimize this Offset() operator by removing/ignoring
                     * it. Set the related nodes to default.
                     */
                    Next->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
                    PkgLengthNode->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;

                    AslError (ASL_OPTIMIZATION, ASL_MSG_OFFSET, PkgLengthNode,
                        "Optimizer has removed statement");
                }
                else
                {
                    /* Optimization is disabled, treat as a valid Offset */

                    PkgLengthNode->Asl.Value.Integer =
                        NewBitOffset - CurrentBitOffset;
                    CurrentBitOffset = NewBitOffset;
                }
            }
            else
            {
                /*
                 * Valid new offset - set the value to be inserted into the AML
                 * and update the offset counter.
                 */
                PkgLengthNode->Asl.Value.Integer =
                    NewBitOffset - CurrentBitOffset;
                CurrentBitOffset = NewBitOffset;
            }
            break;

        case PARSEOP_NAMESEG:
        case PARSEOP_RESERVED_BYTES:

            /* Named or reserved field entry */

            PkgLengthNode = Next->Asl.Child;
            NewBitOffset = (UINT32) PkgLengthNode->Asl.Value.Integer;
            CurrentBitOffset += NewBitOffset;

            if ((NewBitOffset == 0) &&
                (Next->Asl.ParseOpcode == PARSEOP_RESERVED_BYTES) &&
                AslGbl_OptimizeTrivialParseNodes)
            {
                /*
                 * Unnamed field with a bit length of zero. We can
                 * safely just ignore this. However, we will not ignore
                 * a named field of zero length, we don't want to just
                 * toss out a name.
                 */
                Next->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
                PkgLengthNode->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
                break;
            }

            /* Save the current AccessAs value for error checking later */

            switch (AccessType)
            {
                case AML_FIELD_ACCESS_ANY:
                case AML_FIELD_ACCESS_BYTE:
                case AML_FIELD_ACCESS_BUFFER:
                default:

                    MinimumLength = 8;
                    break;

                case AML_FIELD_ACCESS_WORD:
                    MinimumLength = 16;
                    break;

                case AML_FIELD_ACCESS_DWORD:
                    MinimumLength = 32;
                    break;

                case AML_FIELD_ACCESS_QWORD:
                    MinimumLength = 64;
                    break;
            }

            PkgLengthNode->Asl.ExtraValue = MinimumLength;
            break;

        default:

            /* All supported field opcodes must appear above */

            break;
        }

        /* Move on to next entry in the field list */

        Next = Next->Asl.Next;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoField
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the AML operands for the FIELD ASL keyword
 *
 ******************************************************************************/

static void
OpnDoField (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;


    /* Opcode is parent node */
    /* First child is field name */

    Next = Op->Asl.Child;

    /* Second child is the AccessType */

    OpnDoFieldCommon (Op, Next->Asl.Next);
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoIndexField
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the AML operands for the INDEXFIELD ASL keyword
 *
 ******************************************************************************/

static void
OpnDoIndexField (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;


    /* Opcode is parent node */
    /* First child is the index name */

    Next = Op->Asl.Child;

    /* Second child is the data name */

    Next = Next->Asl.Next;

    /* Third child is the AccessType */

    OpnDoFieldCommon (Op, Next->Asl.Next);
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoBankField
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the AML operands for the BANKFIELD ASL keyword
 *
 ******************************************************************************/

static void
OpnDoBankField (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;


    /* Opcode is parent node */
    /* First child is the region name */

    Next = Op->Asl.Child;

    /* Second child is the bank name */

    Next = Next->Asl.Next;

    /* Third child is the bank value */

    Next = Next->Asl.Next;

    /* Fourth child is the AccessType */

    OpnDoFieldCommon (Op, Next->Asl.Next);
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoRegion
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Tries to get the length of the region. Can only do this at
 *              compile time if the length is a constant.
 *
 ******************************************************************************/

static void
OpnDoRegion (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;
    ACPI_ADR_SPACE_TYPE     SpaceId;


    /* Opcode is parent node */
    /* First child is the region name */

    Next = Op->Asl.Child;

    /* Second child is the space ID */

    Next = Next->Asl.Next;
    SpaceId = (ACPI_ADR_SPACE_TYPE) Next->Common.Value.Integer;

    /* Third child is the region offset */

    Next = Next->Asl.Next;

    /* Fourth child is the region length */

    Next = Next->Asl.Next;
    if (Next->Asl.ParseOpcode == PARSEOP_INTEGER)
    {
        /* Check for zero length */

        Op->Asl.Value.Integer = Next->Asl.Value.Integer;
        if (!Op->Asl.Value.Integer && (SpaceId < ACPI_NUM_PREDEFINED_REGIONS))
        {
            AslError (ASL_ERROR, ASL_MSG_REGION_LENGTH, Op, NULL);
        }
    }
    else
    {
        Op->Asl.Value.Integer = ACPI_UINT64_MAX;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoBuffer
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the AML operands for the BUFFER ASL keyword. We
 *              build a single raw byte buffer from the initialization nodes,
 *              each parse node contains a buffer byte.
 *
 ******************************************************************************/

static void
OpnDoBuffer (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *InitializerOp;
    ACPI_PARSE_OBJECT       *BufferLengthOp;

    /* Optional arguments for this opcode with defaults */

    UINT32                  BufferLength = 0;


    /* Opcode and package length first */
    /* Buffer Length is next, followed by the initializer list */

    BufferLengthOp = Op->Asl.Child;
    InitializerOp = BufferLengthOp->Asl.Next;

    /*
     * If the BufferLength is not an INTEGER or was not specified in the ASL
     * (DEFAULT_ARG), it is a TermArg that is
     * evaluated at run-time, and we are therefore finished.
     */
    if ((BufferLengthOp->Asl.ParseOpcode != PARSEOP_INTEGER) &&
        (BufferLengthOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG))
    {
        return;
    }

    /*
     * We want to count the number of items in the initializer list, because if
     * it is larger than the buffer length, we will define the buffer size
     * to be the size of the initializer list (as per the ACPI Specification)
     */
    switch (InitializerOp->Asl.ParseOpcode)
    {
    case PARSEOP_INTEGER:
    case PARSEOP_BYTECONST:
    case PARSEOP_WORDCONST:
    case PARSEOP_DWORDCONST:

        /* The peer list contains the byte list (if any...) */

        while (InitializerOp)
        {
            /* For buffers, this is a list of raw bytes */

            InitializerOp->Asl.AmlOpcode = AML_RAW_DATA_BYTE;
            InitializerOp->Asl.AmlLength = 1;
            InitializerOp->Asl.ParseOpcode = PARSEOP_RAW_DATA;

            BufferLength++;
            InitializerOp = ASL_GET_PEER_NODE (InitializerOp);
        }
        break;

    case PARSEOP_STRING_LITERAL:

        /*
         * Only one initializer, the string. Buffer must be big enough to hold
         * the string plus the null termination byte
         */
        BufferLength = strlen (InitializerOp->Asl.Value.String) + 1;

        InitializerOp->Asl.AmlOpcode = AML_RAW_DATA_BUFFER;
        InitializerOp->Asl.AmlLength = BufferLength;
        InitializerOp->Asl.ParseOpcode = PARSEOP_RAW_DATA;
        break;

    case PARSEOP_RAW_DATA:

        /* Buffer nodes are already initialized (e.g. Unicode operator) */
        return;

    case PARSEOP_DEFAULT_ARG:
        break;

    default:

        AslError (ASL_ERROR, ASL_MSG_INVALID_OPERAND, InitializerOp,
            "Unknown buffer initializer opcode");
        printf ("Unknown buffer initializer opcode [%s]\n",
            UtGetOpName (InitializerOp->Asl.ParseOpcode));
        return;
    }

    /* Check if initializer list is longer than the buffer length */

    if (BufferLengthOp->Asl.Value.Integer > BufferLength)
    {
        BufferLength = (UINT32) BufferLengthOp->Asl.Value.Integer;
    }

    if (!BufferLength)
    {
        /* No length AND no items -- issue notice */

        AslError (ASL_REMARK, ASL_MSG_BUFFER_LENGTH, BufferLengthOp, NULL);

        /* But go ahead and put the buffer length of zero into the AML */
    }

    /*
     * Just set the buffer size node to be the buffer length, regardless
     * of whether it was previously an integer or a default_arg placeholder
     */
    BufferLengthOp->Asl.ParseOpcode = PARSEOP_INTEGER;
    BufferLengthOp->Asl.AmlOpcode = AML_DWORD_OP;
    BufferLengthOp->Asl.Value.Integer = BufferLength;

    (void) OpcSetOptimalIntegerSize (BufferLengthOp);

    /* Remaining nodes are handled via the tree walk */
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoPackage
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the AML operands for the PACKAGE ASL keyword. NOTE:
 *              can only be called after constants have been folded, to ensure
 *              that the PackageLength operand has been fully reduced.
 *
 ******************************************************************************/

void
OpnDoPackage (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *InitializerOp;
    ACPI_PARSE_OBJECT       *PackageLengthOp;
    UINT32                  PackageLength = 0;


    /* Opcode and package length first, followed by the initializer list */

    PackageLengthOp = Op->Asl.Child;
    InitializerOp = PackageLengthOp->Asl.Next;

    /* Count the number of items in the initializer list */

    if (InitializerOp->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG)
    {
        /* The peer list contains the byte list (if any...) */

        while (InitializerOp)
        {
            PackageLength++;
            InitializerOp = InitializerOp->Asl.Next;
        }
    }

    /* If package length is a constant, compare to the initializer list */

    if ((PackageLengthOp->Asl.ParseOpcode == PARSEOP_INTEGER)      ||
        (PackageLengthOp->Asl.ParseOpcode == PARSEOP_QWORDCONST))
    {
        if (PackageLengthOp->Asl.Value.Integer > PackageLength)
        {
            /*
             * Allow package length to be longer than the initializer
             * list -- but if the length of initializer list is nonzero,
             * issue a message since this is probably a coding error,
             * even though technically legal.
             */
            if (PackageLength > 0)
            {
                AslError (ASL_REMARK, ASL_MSG_LIST_LENGTH_SHORT,
                    PackageLengthOp, NULL);
            }

            PackageLength = (UINT32) PackageLengthOp->Asl.Value.Integer;
        }
        else if (PackageLengthOp->Asl.Value.Integer < PackageLength)
        {
            /*
             * The package length is smaller than the length of the
             * initializer list. This is an error as per the ACPI spec.
             */
            AslError (ASL_ERROR, ASL_MSG_LIST_LENGTH_LONG,
                PackageLengthOp, NULL);
        }
    }

    if (PackageLengthOp->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
    {
        /*
         * This is the case if the PackageLength was left empty - Package()
         * The package length becomes the length of the initializer list
         */
        Op->Asl.Child->Asl.ParseOpcode = PARSEOP_INTEGER;
        Op->Asl.Child->Asl.Value.Integer = PackageLength;

        /* Set the AML opcode */

        (void) OpcSetOptimalIntegerSize (Op->Asl.Child);
    }

    /* If not a variable-length package, check for a zero package length */

    if ((PackageLengthOp->Asl.ParseOpcode == PARSEOP_INTEGER)      ||
        (PackageLengthOp->Asl.ParseOpcode == PARSEOP_QWORDCONST)   ||
        (PackageLengthOp->Asl.ParseOpcode == PARSEOP_ZERO)         ||
        (PackageLengthOp->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG))
    {
        if (!PackageLength)
        {
            /* No length AND no initializer list -- issue a remark */

            AslError (ASL_REMARK, ASL_MSG_PACKAGE_LENGTH,
                PackageLengthOp, NULL);

            /* But go ahead and put the buffer length of zero into the AML */
        }
    }

    /*
     * If the PackageLength is a constant <= 255, we can change the
     * AML opcode from VarPackage to a simple (ACPI 1.0) Package opcode.
     */
    if (((Op->Asl.Child->Asl.ParseOpcode == PARSEOP_INTEGER) &&
            (Op->Asl.Child->Asl.Value.Integer <= 255))  ||
        (Op->Asl.Child->Asl.ParseOpcode == PARSEOP_ONE) ||
        (Op->Asl.Child->Asl.ParseOpcode == PARSEOP_ONES)||
        (Op->Asl.Child->Asl.ParseOpcode == PARSEOP_ZERO))
    {
        Op->Asl.AmlOpcode = AML_PACKAGE_OP;
        Op->Asl.ParseOpcode = PARSEOP_PACKAGE;

        /*
         * Just set the package size node to be the package length, regardless
         * of whether it was previously an integer or a default_arg placeholder
         */
        PackageLengthOp->Asl.AmlOpcode = AML_RAW_DATA_BYTE;
        PackageLengthOp->Asl.AmlLength = 1;
        PackageLengthOp->Asl.ParseOpcode = PARSEOP_RAW_DATA;
        PackageLengthOp->Asl.Value.Integer = PackageLength;
    }

    /* Remaining nodes are handled via the tree walk */
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoLoadTable
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the AML operands for the LOADTABLE ASL keyword.
 *
 ******************************************************************************/

static void
OpnDoLoadTable (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;


    /* Opcode is parent node */
    /* First child is the table signature */

    Next = Op->Asl.Child;

    /* Second child is the OEM ID*/

    Next = Next->Asl.Next;

    /* Third child is the OEM table ID */

    Next = Next->Asl.Next;

    /* Fourth child is the RootPath string */

    Next = Next->Asl.Next;
    if (Next->Asl.ParseOpcode == PARSEOP_ZERO)
    {
        Next->Asl.ParseOpcode = PARSEOP_STRING_LITERAL;
        Next->Asl.Value.String = "\\";
        Next->Asl.AmlLength = 2;
        OpcGenerateAmlOpcode (Next);
    }

#ifdef ASL_FUTURE_IMPLEMENTATION

    /* TBD: NOT IMPLEMENTED */
    /* Fifth child is the [optional] ParameterPathString */
    /* Sixth child is the [optional] ParameterData */

    Next = Next->Asl.Next;
    if (Next->Asl.ParseOpcode == DEFAULT_ARG)
    {
        Next->Asl.AmlLength = 1;
        Next->Asl.ParseOpcode = ZERO;
        OpcGenerateAmlOpcode (Next);
    }


    Next = Next->Asl.Next;
    if (Next->Asl.ParseOpcode == DEFAULT_ARG)
    {
        Next->Asl.AmlLength = 1;
        Next->Asl.ParseOpcode = ZERO;
        OpcGenerateAmlOpcode (Next);
    }
#endif
}


/*******************************************************************************
 *
 * FUNCTION:    OpnDoDefinitionBlock
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Construct the AML operands for the DEFINITIONBLOCK ASL keyword
 *
 ******************************************************************************/

static void
OpnDoDefinitionBlock (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Child;
    ACPI_SIZE               Length;
    UINT32                  i;
    char                    *Filename;


    /*
     * These nodes get stuffed into the table header. They are special
     * cased when the table is written to the output file.
     *
     * Mark all of these nodes as non-usable so they won't get output
     * as AML opcodes!
     */

    /* Get AML filename. Use it if non-null */

    Child = Op->Asl.Child;
    if (Child->Asl.Value.Buffer  &&
        *Child->Asl.Value.Buffer &&
        (AslGbl_UseDefaultAmlFilename))
    {
        /*
         * We will use the AML filename that is embedded in the source file
         * for the output filename.
         */
        Filename = UtLocalCacheCalloc (strlen (AslGbl_DirectoryPath) +
            strlen ((char *) Child->Asl.Value.Buffer) + 1);

        /* Prepend the current directory path */

        strcpy (Filename, AslGbl_DirectoryPath);
        strcat (Filename, (char *) Child->Asl.Value.Buffer);

        AslGbl_OutputFilenamePrefix = Filename;
        UtConvertBackslashes (AslGbl_OutputFilenamePrefix);
    }

    Child->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;

    /* Signature */

    Child = Child->Asl.Next;
    Child->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
    if (Child->Asl.Value.String)
    {
        AslGbl_TableSignature = Child->Asl.Value.String;
        if (strlen (AslGbl_TableSignature) != ACPI_NAME_SIZE)
        {
            AslError (ASL_ERROR, ASL_MSG_TABLE_SIGNATURE, Child,
                "Length must be exactly 4 characters");
        }

        for (i = 0; i < ACPI_NAME_SIZE; i++)
        {
            if (!isalnum ((int) AslGbl_TableSignature[i]))
            {
                AslError (ASL_ERROR, ASL_MSG_TABLE_SIGNATURE, Child,
                    "Contains non-alphanumeric characters");
            }
        }
    }

    /* Revision */

    Child = Child->Asl.Next;
    Child->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;

    /*
     * We used the revision to set the integer width earlier
     */

    /* OEMID */

    Child = Child->Asl.Next;
    Child->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
    if (Child->Asl.Value.String &&
        strlen (Child->Asl.Value.String) > ACPI_OEM_ID_SIZE)
    {
        AslError (ASL_ERROR, ASL_MSG_OEM_ID, Child,
            "Length cannot exceed 6 characters");
    }

    /* OEM TableID */

    Child = Child->Asl.Next;
    Child->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
    if (Child->Asl.Value.String)
    {
        Length = strlen (Child->Asl.Value.String);
        if (Length > ACPI_OEM_TABLE_ID_SIZE)
        {
            AslError (ASL_ERROR, ASL_MSG_OEM_TABLE_ID, Child,
                "Length cannot exceed 8 characters");
        }

        AslGbl_TableId = UtLocalCacheCalloc (Length + 1);
        strcpy (AslGbl_TableId, Child->Asl.Value.String);

        /*
         * Convert anything non-alphanumeric to an underscore. This
         * allows us to use the TableID to generate unique C symbols.
         */
        for (i = 0; i < Length; i++)
        {
            if (!isalnum ((int) AslGbl_TableId[i]))
            {
                AslGbl_TableId[i] = '_';
            }
        }
    }

    /* OEM Revision */

    Child = Child->Asl.Next;
    Child->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
}


/*******************************************************************************
 *
 * FUNCTION:    UtGetArg
 *
 * PARAMETERS:  Op              - Get an argument for this op
 *              Argn            - Nth argument to get
 *
 * RETURN:      The argument (as an Op object). NULL if argument does not exist
 *
 * DESCRIPTION: Get the specified op's argument (peer)
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
UtGetArg (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Argn)
{
    ACPI_PARSE_OBJECT       *Arg = NULL;


    /* Get the requested argument object */

    Arg = Op->Asl.Child;
    while (Arg && Argn)
    {
        Argn--;
        Arg = Arg->Asl.Next;
    }

    return (Arg);
}


/*******************************************************************************
 *
 * FUNCTION:    OpnAttachNameToNode
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: For the named ASL/AML operators, get the actual name from the
 *              argument list and attach it to the parent node so that we
 *              can get to it quickly later.
 *
 ******************************************************************************/

static void
OpnAttachNameToNode (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Child = NULL;


    switch (Op->Asl.AmlOpcode)
    {
    case AML_DATA_REGION_OP:
    case AML_DEVICE_OP:
    case AML_EVENT_OP:
    case AML_EXTERNAL_OP:
    case AML_METHOD_OP:
    case AML_MUTEX_OP:
    case AML_REGION_OP:
    case AML_POWER_RESOURCE_OP:
    case AML_PROCESSOR_OP:
    case AML_THERMAL_ZONE_OP:
    case AML_NAME_OP:
    case AML_SCOPE_OP:

        Child = UtGetArg (Op, 0);
        break;

    case AML_ALIAS_OP:

        Child = UtGetArg (Op, 1);
        break;

    case AML_CREATE_BIT_FIELD_OP:
    case AML_CREATE_BYTE_FIELD_OP:
    case AML_CREATE_WORD_FIELD_OP:
    case AML_CREATE_DWORD_FIELD_OP:
    case AML_CREATE_QWORD_FIELD_OP:

        Child = UtGetArg (Op, 2);
        break;

    case AML_CREATE_FIELD_OP:

        Child = UtGetArg (Op, 3);
        break;

    case AML_BANK_FIELD_OP:
    case AML_INDEX_FIELD_OP:
    case AML_FIELD_OP:

        return;

    default:

        return;
    }

    if (Child)
    {
        UtAttachNamepathToOwner (Op, Child);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    OpnGenerateAmlOperands
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prepare nodes to be output as AML data and operands. The more
 *              complex AML opcodes require processing of the child nodes
 *              (arguments/operands).
 *
 ******************************************************************************/

void
OpnGenerateAmlOperands (
    ACPI_PARSE_OBJECT       *Op)
{


    if (Op->Asl.AmlOpcode == AML_RAW_DATA_BYTE)
    {
        return;
    }

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_DEFINITION_BLOCK:

        OpnDoDefinitionBlock (Op);
        break;

    case PARSEOP_METHOD:

        OpnDoMethod (Op);
        break;

    case PARSEOP_MUTEX:

        OpnDoMutex (Op);
        break;

    case PARSEOP_FIELD:

        OpnDoField (Op);
        break;

    case PARSEOP_INDEXFIELD:

        OpnDoIndexField (Op);
        break;

    case PARSEOP_BANKFIELD:

        OpnDoBankField (Op);
        break;

    case PARSEOP_BUFFER:

        OpnDoBuffer (Op);
        break;

    case PARSEOP_LOADTABLE:

        OpnDoLoadTable (Op);
        break;

    case PARSEOP_OPERATIONREGION:

        OpnDoRegion (Op);
        break;

    case PARSEOP_RESOURCETEMPLATE:

        RsDoResourceTemplate (Op);
        break;

    case PARSEOP_NAMESEG:
    case PARSEOP_NAMESTRING:
    case PARSEOP_METHODCALL:
    case PARSEOP_STRING_LITERAL:
    default:

        break;
    }

    /* TBD: move */

    OpnAttachNameToNode (Op);
}
