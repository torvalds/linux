/******************************************************************************
 *
 * Module Name: dsopcode - Dispatcher support for regions and fields
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acevents.h>
#include <contrib/dev/acpica/include/actables.h>

#define _COMPONENT          ACPI_DISPATCHER
        ACPI_MODULE_NAME    ("dsopcode")

/* Local prototypes */

static ACPI_STATUS
AcpiDsInitBufferField (
    UINT16                  AmlOpcode,
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     *BufferDesc,
    ACPI_OPERAND_OBJECT     *OffsetDesc,
    ACPI_OPERAND_OBJECT     *LengthDesc,
    ACPI_OPERAND_OBJECT     *ResultDesc);


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsInitializeRegion
 *
 * PARAMETERS:  ObjHandle       - Region namespace node
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Front end to EvInitializeRegion
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsInitializeRegion (
    ACPI_HANDLE             ObjHandle)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_STATUS             Status;


    ObjDesc = AcpiNsGetAttachedObject (ObjHandle);

    /* Namespace is NOT locked */

    Status = AcpiEvInitializeRegion (ObjDesc);
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsInitBufferField
 *
 * PARAMETERS:  AmlOpcode       - CreateXxxField
 *              ObjDesc         - BufferField object
 *              BufferDesc      - Host Buffer
 *              OffsetDesc      - Offset into buffer
 *              LengthDesc      - Length of field (CREATE_FIELD_OP only)
 *              ResultDesc      - Where to store the result
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform actual initialization of a buffer field
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDsInitBufferField (
    UINT16                  AmlOpcode,
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     *BufferDesc,
    ACPI_OPERAND_OBJECT     *OffsetDesc,
    ACPI_OPERAND_OBJECT     *LengthDesc,
    ACPI_OPERAND_OBJECT     *ResultDesc)
{
    UINT32                  Offset;
    UINT32                  BitOffset;
    UINT32                  BitCount;
    UINT8                   FieldFlags;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE_PTR (DsInitBufferField, ObjDesc);


    /* Host object must be a Buffer */

    if (BufferDesc->Common.Type != ACPI_TYPE_BUFFER)
    {
        ACPI_ERROR ((AE_INFO,
            "Target of Create Field is not a Buffer object - %s",
            AcpiUtGetObjectTypeName (BufferDesc)));

        Status = AE_AML_OPERAND_TYPE;
        goto Cleanup;
    }

    /*
     * The last parameter to all of these opcodes (ResultDesc) started
     * out as a NameString, and should therefore now be a NS node
     * after resolution in AcpiExResolveOperands().
     */
    if (ACPI_GET_DESCRIPTOR_TYPE (ResultDesc) != ACPI_DESC_TYPE_NAMED)
    {
        ACPI_ERROR ((AE_INFO,
            "(%s) destination not a NS Node [%s]",
            AcpiPsGetOpcodeName (AmlOpcode),
            AcpiUtGetDescriptorName (ResultDesc)));

        Status = AE_AML_OPERAND_TYPE;
        goto Cleanup;
    }

    Offset = (UINT32) OffsetDesc->Integer.Value;

    /*
     * Setup the Bit offsets and counts, according to the opcode
     */
    switch (AmlOpcode)
    {
    case AML_CREATE_FIELD_OP:

        /* Offset is in bits, count is in bits */

        FieldFlags = AML_FIELD_ACCESS_BYTE;
        BitOffset  = Offset;
        BitCount   = (UINT32) LengthDesc->Integer.Value;

        /* Must have a valid (>0) bit count */

        if (BitCount == 0)
        {
            ACPI_BIOS_ERROR ((AE_INFO,
                "Attempt to CreateField of length zero"));
            Status = AE_AML_OPERAND_VALUE;
            goto Cleanup;
        }
        break;

    case AML_CREATE_BIT_FIELD_OP:

        /* Offset is in bits, Field is one bit */

        BitOffset  = Offset;
        BitCount   = 1;
        FieldFlags = AML_FIELD_ACCESS_BYTE;
        break;

    case AML_CREATE_BYTE_FIELD_OP:

        /* Offset is in bytes, field is one byte */

        BitOffset  = 8 * Offset;
        BitCount   = 8;
        FieldFlags = AML_FIELD_ACCESS_BYTE;
        break;

    case AML_CREATE_WORD_FIELD_OP:

        /* Offset is in bytes, field is one word */

        BitOffset  = 8 * Offset;
        BitCount   = 16;
        FieldFlags = AML_FIELD_ACCESS_WORD;
        break;

    case AML_CREATE_DWORD_FIELD_OP:

        /* Offset is in bytes, field is one dword */

        BitOffset  = 8 * Offset;
        BitCount   = 32;
        FieldFlags = AML_FIELD_ACCESS_DWORD;
        break;

    case AML_CREATE_QWORD_FIELD_OP:

        /* Offset is in bytes, field is one qword */

        BitOffset  = 8 * Offset;
        BitCount   = 64;
        FieldFlags = AML_FIELD_ACCESS_QWORD;
        break;

    default:

        ACPI_ERROR ((AE_INFO,
            "Unknown field creation opcode 0x%02X",
            AmlOpcode));
        Status = AE_AML_BAD_OPCODE;
        goto Cleanup;
    }

    /* Entire field must fit within the current length of the buffer */

    if ((BitOffset + BitCount) >
        (8 * (UINT32) BufferDesc->Buffer.Length))
    {
        Status = AE_AML_BUFFER_LIMIT;
        ACPI_BIOS_EXCEPTION ((AE_INFO, Status,
            "Field [%4.4s] at bit offset/length %u/%u "
            "exceeds size of target Buffer (%u bits)",
            AcpiUtGetNodeName (ResultDesc), BitOffset, BitCount,
            8 * (UINT32) BufferDesc->Buffer.Length));
        goto Cleanup;
    }

    /*
     * Initialize areas of the field object that are common to all fields
     * For FieldFlags, use LOCK_RULE = 0 (NO_LOCK),
     * UPDATE_RULE = 0 (UPDATE_PRESERVE)
     */
    Status = AcpiExPrepCommonFieldObject (
        ObjDesc, FieldFlags, 0, BitOffset, BitCount);
    if (ACPI_FAILURE (Status))
    {
        goto Cleanup;
    }

    ObjDesc->BufferField.BufferObj = BufferDesc;

    /* Reference count for BufferDesc inherits ObjDesc count */

    BufferDesc->Common.ReferenceCount = (UINT16)
        (BufferDesc->Common.ReferenceCount + ObjDesc->Common.ReferenceCount);


Cleanup:

    /* Always delete the operands */

    AcpiUtRemoveReference (OffsetDesc);
    AcpiUtRemoveReference (BufferDesc);

    if (AmlOpcode == AML_CREATE_FIELD_OP)
    {
        AcpiUtRemoveReference (LengthDesc);
    }

    /* On failure, delete the result descriptor */

    if (ACPI_FAILURE (Status))
    {
        AcpiUtRemoveReference (ResultDesc);     /* Result descriptor */
    }
    else
    {
        /* Now the address and length are valid for this BufferField */

        ObjDesc->BufferField.Flags |= AOPOBJ_DATA_VALID;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsEvalBufferFieldOperands
 *
 * PARAMETERS:  WalkState       - Current walk
 *              Op              - A valid BufferField Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get BufferField Buffer and Index
 *              Called from AcpiDsExecEndOp during BufferField parse tree walk
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsEvalBufferFieldOperands (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_PARSE_OBJECT       *NextOp;


    ACPI_FUNCTION_TRACE_PTR (DsEvalBufferFieldOperands, Op);


    /*
     * This is where we evaluate the address and length fields of the
     * CreateXxxField declaration
     */
    Node =  Op->Common.Node;

    /* NextOp points to the op that holds the Buffer */

    NextOp = Op->Common.Value.Arg;

    /* Evaluate/create the address and length operands */

    Status = AcpiDsCreateOperands (WalkState, NextOp);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    ObjDesc = AcpiNsGetAttachedObject (Node);
    if (!ObjDesc)
    {
        return_ACPI_STATUS (AE_NOT_EXIST);
    }

    /* Resolve the operands */

    Status = AcpiExResolveOperands (
        Op->Common.AmlOpcode, ACPI_WALK_OPERANDS, WalkState);
    if (ACPI_FAILURE (Status))
    {
        ACPI_ERROR ((AE_INFO, "(%s) bad operand(s), status 0x%X",
            AcpiPsGetOpcodeName (Op->Common.AmlOpcode), Status));

        return_ACPI_STATUS (Status);
    }

    /* Initialize the Buffer Field */

    if (Op->Common.AmlOpcode == AML_CREATE_FIELD_OP)
    {
        /* NOTE: Slightly different operands for this opcode */

        Status = AcpiDsInitBufferField (Op->Common.AmlOpcode, ObjDesc,
            WalkState->Operands[0], WalkState->Operands[1],
            WalkState->Operands[2], WalkState->Operands[3]);
    }
    else
    {
        /* All other, CreateXxxField opcodes */

        Status = AcpiDsInitBufferField (Op->Common.AmlOpcode, ObjDesc,
            WalkState->Operands[0], WalkState->Operands[1],
            NULL, WalkState->Operands[2]);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsEvalRegionOperands
 *
 * PARAMETERS:  WalkState       - Current walk
 *              Op              - A valid region Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get region address and length
 *              Called from AcpiDsExecEndOp during OpRegion parse tree walk
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsEvalRegionOperands (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     *OperandDesc;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_PARSE_OBJECT       *NextOp;
    ACPI_ADR_SPACE_TYPE     SpaceId;


    ACPI_FUNCTION_TRACE_PTR (DsEvalRegionOperands, Op);


    /*
     * This is where we evaluate the address and length fields of the
     * OpRegion declaration
     */
    Node = Op->Common.Node;

    /* NextOp points to the op that holds the SpaceID */

    NextOp = Op->Common.Value.Arg;
    SpaceId = (ACPI_ADR_SPACE_TYPE) NextOp->Common.Value.Integer;

    /* NextOp points to address op */

    NextOp = NextOp->Common.Next;

    /* Evaluate/create the address and length operands */

    Status = AcpiDsCreateOperands (WalkState, NextOp);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Resolve the length and address operands to numbers */

    Status = AcpiExResolveOperands (
        Op->Common.AmlOpcode, ACPI_WALK_OPERANDS, WalkState);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    ObjDesc = AcpiNsGetAttachedObject (Node);
    if (!ObjDesc)
    {
        return_ACPI_STATUS (AE_NOT_EXIST);
    }

    /*
     * Get the length operand and save it
     * (at Top of stack)
     */
    OperandDesc = WalkState->Operands[WalkState->NumOperands - 1];

    ObjDesc->Region.Length = (UINT32) OperandDesc->Integer.Value;
    AcpiUtRemoveReference (OperandDesc);

    /* A zero-length operation region is unusable. Just warn */

    if (!ObjDesc->Region.Length && (SpaceId < ACPI_NUM_PREDEFINED_REGIONS))
    {
        ACPI_WARNING ((AE_INFO,
            "Operation Region [%4.4s] has zero length (SpaceId %X)",
            Node->Name.Ascii, SpaceId));
    }

    /*
     * Get the address and save it
     * (at top of stack - 1)
     */
    OperandDesc = WalkState->Operands[WalkState->NumOperands - 2];

    ObjDesc->Region.Address = (ACPI_PHYSICAL_ADDRESS)
        OperandDesc->Integer.Value;
    AcpiUtRemoveReference (OperandDesc);

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "RgnObj %p Addr %8.8X%8.8X Len %X\n",
        ObjDesc, ACPI_FORMAT_UINT64 (ObjDesc->Region.Address),
        ObjDesc->Region.Length));

    Status = AcpiUtAddAddressRange (ObjDesc->Region.SpaceId,
        ObjDesc->Region.Address, ObjDesc->Region.Length, Node);

    /* Now the address and length are valid for this opregion */

    ObjDesc->Region.Flags |= AOPOBJ_DATA_VALID;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsEvalTableRegionOperands
 *
 * PARAMETERS:  WalkState       - Current walk
 *              Op              - A valid region Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get region address and length.
 *              Called from AcpiDsExecEndOp during DataTableRegion parse
 *              tree walk.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsEvalTableRegionOperands (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     **Operand;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_PARSE_OBJECT       *NextOp;
    ACPI_TABLE_HEADER       *Table;
    UINT32                  TableIndex;


    ACPI_FUNCTION_TRACE_PTR (DsEvalTableRegionOperands, Op);


    /*
     * This is where we evaluate the Signature string, OemId string,
     * and OemTableId string of the Data Table Region declaration
     */
    Node =  Op->Common.Node;

    /* NextOp points to Signature string op */

    NextOp = Op->Common.Value.Arg;

    /*
     * Evaluate/create the Signature string, OemId string,
     * and OemTableId string operands
     */
    Status = AcpiDsCreateOperands (WalkState, NextOp);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    Operand = &WalkState->Operands[0];

    /*
     * Resolve the Signature string, OemId string,
     * and OemTableId string operands
     */
    Status = AcpiExResolveOperands (
        Op->Common.AmlOpcode, ACPI_WALK_OPERANDS, WalkState);
    if (ACPI_FAILURE (Status))
    {
        goto Cleanup;
    }

    /* Find the ACPI table */

    Status = AcpiTbFindTable (
        Operand[0]->String.Pointer,
        Operand[1]->String.Pointer,
        Operand[2]->String.Pointer, &TableIndex);
    if (ACPI_FAILURE (Status))
    {
        if (Status == AE_NOT_FOUND)
        {
            ACPI_ERROR ((AE_INFO,
                "ACPI Table [%4.4s] OEM:(%s, %s) not found in RSDT/XSDT",
                Operand[0]->String.Pointer,
                Operand[1]->String.Pointer,
                Operand[2]->String.Pointer));
        }
        goto Cleanup;
    }

    Status = AcpiGetTableByIndex (TableIndex, &Table);
    if (ACPI_FAILURE (Status))
    {
        goto Cleanup;
    }

    ObjDesc = AcpiNsGetAttachedObject (Node);
    if (!ObjDesc)
    {
        Status = AE_NOT_EXIST;
        goto Cleanup;
    }

    ObjDesc->Region.Address = ACPI_PTR_TO_PHYSADDR (Table);
    ObjDesc->Region.Length = Table->Length;

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "RgnObj %p Addr %8.8X%8.8X Len %X\n",
        ObjDesc, ACPI_FORMAT_UINT64 (ObjDesc->Region.Address),
        ObjDesc->Region.Length));

    /* Now the address and length are valid for this opregion */

    ObjDesc->Region.Flags |= AOPOBJ_DATA_VALID;

Cleanup:
    AcpiUtRemoveReference (Operand[0]);
    AcpiUtRemoveReference (Operand[1]);
    AcpiUtRemoveReference (Operand[2]);

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsEvalDataObjectOperands
 *
 * PARAMETERS:  WalkState       - Current walk
 *              Op              - A valid DataObject Op object
 *              ObjDesc         - DataObject
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the operands and complete the following data object types:
 *              Buffer, Package.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsEvalDataObjectOperands (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op,
    ACPI_OPERAND_OBJECT     *ObjDesc)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ArgDesc;
    UINT32                  Length;


    ACPI_FUNCTION_TRACE (DsEvalDataObjectOperands);


    /* The first operand (for all of these data objects) is the length */

    /*
     * Set proper index into operand stack for AcpiDsObjStackPush
     * invoked inside AcpiDsCreateOperand.
     */
    WalkState->OperandIndex = WalkState->NumOperands;

    /* Ignore if child is not valid */

    if (!Op->Common.Value.Arg)
    {
        ACPI_ERROR ((AE_INFO,
            "Missing child while evaluating opcode %4.4X, Op %p",
            Op->Common.AmlOpcode, Op));
        return_ACPI_STATUS (AE_OK);
    }

    Status = AcpiDsCreateOperand (WalkState, Op->Common.Value.Arg, 1);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    Status = AcpiExResolveOperands (WalkState->Opcode,
        &(WalkState->Operands [WalkState->NumOperands -1]),
        WalkState);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Extract length operand */

    ArgDesc = WalkState->Operands [WalkState->NumOperands - 1];
    Length = (UINT32) ArgDesc->Integer.Value;

    /* Cleanup for length operand */

    Status = AcpiDsObjStackPop (1, WalkState);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    AcpiUtRemoveReference (ArgDesc);

    /*
     * Create the actual data object
     */
    switch (Op->Common.AmlOpcode)
    {
    case AML_BUFFER_OP:

        Status = AcpiDsBuildInternalBufferObj (
            WalkState, Op, Length, &ObjDesc);
        break;

    case AML_PACKAGE_OP:
    case AML_VARIABLE_PACKAGE_OP:

        Status = AcpiDsBuildInternalPackageObj (
            WalkState, Op, Length, &ObjDesc);
        break;

    default:

        return_ACPI_STATUS (AE_AML_BAD_OPCODE);
    }

    if (ACPI_SUCCESS (Status))
    {
        /*
         * Return the object in the WalkState, unless the parent is a package -
         * in this case, the return object will be stored in the parse tree
         * for the package.
         */
        if ((!Op->Common.Parent) ||
            ((Op->Common.Parent->Common.AmlOpcode != AML_PACKAGE_OP) &&
             (Op->Common.Parent->Common.AmlOpcode != AML_VARIABLE_PACKAGE_OP) &&
             (Op->Common.Parent->Common.AmlOpcode != AML_NAME_OP)))
        {
            WalkState->ResultObj = ObjDesc;
        }
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDsEvalBankFieldOperands
 *
 * PARAMETERS:  WalkState       - Current walk
 *              Op              - A valid BankField Op object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get BankField BankValue
 *              Called from AcpiDsExecEndOp during BankField parse tree walk
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDsEvalBankFieldOperands (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     *OperandDesc;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_PARSE_OBJECT       *NextOp;
    ACPI_PARSE_OBJECT       *Arg;


    ACPI_FUNCTION_TRACE_PTR (DsEvalBankFieldOperands, Op);


    /*
     * This is where we evaluate the BankValue field of the
     * BankField declaration
     */

    /* NextOp points to the op that holds the Region */

    NextOp = Op->Common.Value.Arg;

    /* NextOp points to the op that holds the Bank Register */

    NextOp = NextOp->Common.Next;

    /* NextOp points to the op that holds the Bank Value */

    NextOp = NextOp->Common.Next;

    /*
     * Set proper index into operand stack for AcpiDsObjStackPush
     * invoked inside AcpiDsCreateOperand.
     *
     * We use WalkState->Operands[0] to store the evaluated BankValue
     */
    WalkState->OperandIndex = 0;

    Status = AcpiDsCreateOperand (WalkState, NextOp, 0);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    Status = AcpiExResolveToValue (&WalkState->Operands[0], WalkState);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    ACPI_DUMP_OPERANDS (ACPI_WALK_OPERANDS,
        AcpiPsGetOpcodeName (Op->Common.AmlOpcode), 1);
    /*
     * Get the BankValue operand and save it
     * (at Top of stack)
     */
    OperandDesc = WalkState->Operands[0];

    /* Arg points to the start Bank Field */

    Arg = AcpiPsGetArg (Op, 4);
    while (Arg)
    {
        /* Ignore OFFSET and ACCESSAS terms here */

        if (Arg->Common.AmlOpcode == AML_INT_NAMEDFIELD_OP)
        {
            Node = Arg->Common.Node;

            ObjDesc = AcpiNsGetAttachedObject (Node);
            if (!ObjDesc)
            {
                return_ACPI_STATUS (AE_NOT_EXIST);
            }

            ObjDesc->BankField.Value = (UINT32) OperandDesc->Integer.Value;
        }

        /* Move to next field in the list */

        Arg = Arg->Common.Next;
    }

    AcpiUtRemoveReference (OperandDesc);
    return_ACPI_STATUS (Status);
}
