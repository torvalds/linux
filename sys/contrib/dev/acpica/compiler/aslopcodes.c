/******************************************************************************
 *
 * Module Name: aslopcode - AML opcode generation
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
        ACPI_MODULE_NAME    ("aslopcodes")


/* Local prototypes */

static void
OpcDoAccessAs (
    ACPI_PARSE_OBJECT       *Op);

static void
OpcDoConnection (
    ACPI_PARSE_OBJECT       *Op);

static void
OpcDoUnicode (
    ACPI_PARSE_OBJECT       *Op);

static void
OpcDoEisaId (
    ACPI_PARSE_OBJECT       *Op);

static void
OpcDoUuId (
    ACPI_PARSE_OBJECT       *Op);


/*******************************************************************************
 *
 * FUNCTION:    OpcAmlOpcodeUpdateWalk
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Opcode update walk, ascending callback
 *
 ******************************************************************************/

ACPI_STATUS
OpcAmlOpcodeUpdateWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{

    /*
     * Handle the Package() case where the actual opcode cannot be determined
     * until the PackageLength operand has been folded and minimized.
     * (PackageOp versus VarPackageOp)
     *
     * This is (as of ACPI 3.0) the only case where the AML opcode can change
     * based upon the value of a parameter.
     *
     * The parser always inserts a VarPackage opcode, which can possibly be
     * optimized to a Package opcode.
     */
    if (Op->Asl.ParseOpcode == PARSEOP_VAR_PACKAGE)
    {
        OpnDoPackage (Op);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    OpcAmlOpcodeWalk
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse tree walk to generate both the AML opcodes and the AML
 *              operands.
 *
 ******************************************************************************/

ACPI_STATUS
OpcAmlOpcodeWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{

    AslGbl_TotalParseNodes++;

    OpcGenerateAmlOpcode (Op);
    OpnGenerateAmlOperands (Op);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    OpcGetIntegerWidth
 *
 * PARAMETERS:  Op          - DEFINITION BLOCK op
 *
 * RETURN:      none
 *
 * DESCRIPTION: Extract integer width from the table revision
 *
 ******************************************************************************/

void
OpcGetIntegerWidth (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Child;


    if (!Op)
    {
        return;
    }

    if (AslGbl_RevisionOverride)
    {
        AcpiUtSetIntegerWidth (AslGbl_RevisionOverride);
    }
    else
    {
        Child = Op->Asl.Child;
        Child = Child->Asl.Next;
        Child = Child->Asl.Next;

        /* Use the revision to set the integer width */

        AcpiUtSetIntegerWidth ((UINT8) Child->Asl.Value.Integer);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    OpcSetOptimalIntegerSize
 *
 * PARAMETERS:  Op        - A parse tree node
 *
 * RETURN:      Integer width, in bytes. Also sets the node AML opcode to the
 *              optimal integer AML prefix opcode.
 *
 * DESCRIPTION: Determine the optimal AML encoding of an integer. All leading
 *              zeros can be truncated to squeeze the integer into the
 *              minimal number of AML bytes.
 *
 ******************************************************************************/

UINT32
OpcSetOptimalIntegerSize (
    ACPI_PARSE_OBJECT       *Op)
{

#if 0
    /*
     * TBD: - we don't want to optimize integers in the block header, but the
     * code below does not work correctly.
     */
    if (Op->Asl.Parent &&
        Op->Asl.Parent->Asl.Parent &&
       (Op->Asl.Parent->Asl.Parent->Asl.ParseOpcode == PARSEOP_DEFINITION_BLOCK))
    {
        return (0);
    }
#endif

    /*
     * Check for the special AML integers first - Zero, One, Ones.
     * These are single-byte opcodes that are the smallest possible
     * representation of an integer.
     *
     * This optimization is optional.
     */
    if (AslGbl_IntegerOptimizationFlag)
    {
        switch (Op->Asl.Value.Integer)
        {
        case 0:

            Op->Asl.AmlOpcode = AML_ZERO_OP;
            AslError (ASL_OPTIMIZATION, ASL_MSG_INTEGER_OPTIMIZATION,
                Op, "Zero");
            return (1);

        case 1:

            Op->Asl.AmlOpcode = AML_ONE_OP;
            AslError (ASL_OPTIMIZATION, ASL_MSG_INTEGER_OPTIMIZATION,
                Op, "One");
            return (1);

        case ACPI_UINT32_MAX:

            /* Check for table integer width (32 or 64) */

            if (AcpiGbl_IntegerByteWidth == 4)
            {
                Op->Asl.AmlOpcode = AML_ONES_OP;
                AslError (ASL_OPTIMIZATION, ASL_MSG_INTEGER_OPTIMIZATION,
                    Op, "Ones");
                return (1);
            }
            break;

        case ACPI_UINT64_MAX:

            /* Check for table integer width (32 or 64) */

            if (AcpiGbl_IntegerByteWidth == 8)
            {
                Op->Asl.AmlOpcode = AML_ONES_OP;
                AslError (ASL_OPTIMIZATION, ASL_MSG_INTEGER_OPTIMIZATION,
                    Op, "Ones");
                return (1);
            }
            break;

        default:

            break;
        }
    }

    /* Find the best fit using the various AML integer prefixes */

    if (Op->Asl.Value.Integer <= ACPI_UINT8_MAX)
    {
        Op->Asl.AmlOpcode = AML_BYTE_OP;
        return (1);
    }

    if (Op->Asl.Value.Integer <= ACPI_UINT16_MAX)
    {
        Op->Asl.AmlOpcode = AML_WORD_OP;
        return (2);
    }

    if (Op->Asl.Value.Integer <= ACPI_UINT32_MAX)
    {
        Op->Asl.AmlOpcode = AML_DWORD_OP;
        return (4);
    }
    else /* 64-bit integer */
    {
        if (AcpiGbl_IntegerByteWidth == 4)
        {
            AslError (ASL_WARNING, ASL_MSG_INTEGER_LENGTH,
                Op, NULL);

            if (!AslGbl_IgnoreErrors)
            {
                /* Truncate the integer to 32-bit */

                Op->Asl.Value.Integer &= ACPI_UINT32_MAX;

                /* Now set the optimal integer size */

                return (OpcSetOptimalIntegerSize (Op));
            }
        }

        Op->Asl.AmlOpcode = AML_QWORD_OP;
        return (8);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    OpcDoAccessAs
 *
 * PARAMETERS:  Op        - Parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Implement the ACCESS_AS ASL keyword.
 *
 ******************************************************************************/

static void
OpcDoAccessAs (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *TypeOp;
    ACPI_PARSE_OBJECT       *AttribOp;
    ACPI_PARSE_OBJECT       *LengthOp;
    UINT8                   Attribute;


    Op->Asl.AmlOpcodeLength = 1;
    TypeOp = Op->Asl.Child;

    /* First child is the access type */

    TypeOp->Asl.AmlOpcode = AML_RAW_DATA_BYTE;
    TypeOp->Asl.ParseOpcode = PARSEOP_RAW_DATA;

    /* Second child is the optional access attribute */

    AttribOp = TypeOp->Asl.Next;
    if (AttribOp->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
    {
        AttribOp->Asl.Value.Integer = 0;
    }

    AttribOp->Asl.AmlOpcode = AML_RAW_DATA_BYTE;
    AttribOp->Asl.ParseOpcode = PARSEOP_RAW_DATA;

    /* Only a few AccessAttributes support AccessLength */

    Attribute = (UINT8) AttribOp->Asl.Value.Integer;
    if ((Attribute != AML_FIELD_ATTRIB_BYTES) &&
        (Attribute != AML_FIELD_ATTRIB_RAW_BYTES) &&
        (Attribute != AML_FIELD_ATTRIB_RAW_PROCESS_BYTES))
    {
        return;
    }

    Op->Asl.AmlOpcode = AML_FIELD_EXT_ACCESS_OP;

    /*
     * Child of Attributes is the AccessLength (required for Multibyte,
     * RawBytes, RawProcess.)
     */
    LengthOp = AttribOp->Asl.Child;
    if (!LengthOp)
    {
        return;
    }

    /* TBD: probably can remove */

    if (LengthOp->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
    {
        LengthOp->Asl.Value.Integer = 16;
    }

    LengthOp->Asl.AmlOpcode = AML_RAW_DATA_BYTE;
    LengthOp->Asl.ParseOpcode = PARSEOP_RAW_DATA;
}


/*******************************************************************************
 *
 * FUNCTION:    OpcDoConnection
 *
 * PARAMETERS:  Op        - Parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Implement the Connection ASL keyword.
 *
 ******************************************************************************/

static void
OpcDoConnection (
    ACPI_PARSE_OBJECT       *Op)
{
    ASL_RESOURCE_NODE       *Rnode;
    ACPI_PARSE_OBJECT       *BufferOp;
    ACPI_PARSE_OBJECT       *BufferLengthOp;
    ACPI_PARSE_OBJECT       *BufferDataOp;
    ASL_RESOURCE_INFO       Info;
    UINT8                   State;


    Op->Asl.AmlOpcodeLength = 1;

    if (Op->Asl.Child->Asl.AmlOpcode == AML_INT_NAMEPATH_OP)
    {
        return;
    }

    BufferOp = Op->Asl.Child;
    BufferLengthOp = BufferOp->Asl.Child;
    BufferDataOp = BufferLengthOp->Asl.Next;

    Info.DescriptorTypeOp = BufferDataOp->Asl.Next;
    Info.CurrentByteOffset = 0;
    State = ACPI_RSTATE_NORMAL;
    Rnode = RsDoOneResourceDescriptor (&Info, &State);
    if (!Rnode)
    {
        return; /* error */
    }

    /*
     * Transform the nodes into the following
     *
     * Op           -> AML_BUFFER_OP
     * First Child  -> BufferLength
     * Second Child -> Descriptor Buffer (raw byte data)
     */
    BufferOp->Asl.ParseOpcode = PARSEOP_BUFFER;
    BufferOp->Asl.AmlOpcode = AML_BUFFER_OP;
    BufferOp->Asl.CompileFlags = OP_AML_PACKAGE | OP_IS_RESOURCE_DESC;
    UtSetParseOpName (BufferOp);

    BufferLengthOp->Asl.ParseOpcode = PARSEOP_INTEGER;
    BufferLengthOp->Asl.Value.Integer = Rnode->BufferLength;
    (void) OpcSetOptimalIntegerSize (BufferLengthOp);
    UtSetParseOpName (BufferLengthOp);

    BufferDataOp->Asl.ParseOpcode = PARSEOP_RAW_DATA;
    BufferDataOp->Asl.AmlOpcode = AML_RAW_DATA_CHAIN;
    BufferDataOp->Asl.AmlOpcodeLength = 0;
    BufferDataOp->Asl.AmlLength = Rnode->BufferLength;
    BufferDataOp->Asl.Value.Buffer = (UINT8 *) Rnode;
    UtSetParseOpName (BufferDataOp);
}


/*******************************************************************************
 *
 * FUNCTION:    OpcDoUnicode
 *
 * PARAMETERS:  Op        - Parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Implement the UNICODE ASL "macro".  Convert the input string
 *              to a unicode buffer. There is no Unicode AML opcode.
 *
 * Note:  The Unicode string is 16 bits per character, no leading signature,
 *        with a 16-bit terminating NULL.
 *
 ******************************************************************************/

static void
OpcDoUnicode (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *InitializerOp;
    UINT32                  Length;
    UINT32                  Count;
    UINT32                  i;
    UINT8                   *AsciiString;
    UINT16                  *UnicodeString;
    ACPI_PARSE_OBJECT       *BufferLengthOp;


    /* Change op into a buffer object */

    Op->Asl.CompileFlags &= ~OP_COMPILE_TIME_CONST;
    Op->Asl.ParseOpcode = PARSEOP_BUFFER;
    UtSetParseOpName (Op);

    /* Buffer Length is first, followed by the string */

    BufferLengthOp = Op->Asl.Child;
    InitializerOp = BufferLengthOp->Asl.Next;

    AsciiString = (UINT8 *) InitializerOp->Asl.Value.String;

    /* Create a new buffer for the Unicode string */

    Count = strlen (InitializerOp->Asl.Value.String) + 1;
    Length = Count * sizeof (UINT16);
    UnicodeString = UtLocalCalloc (Length);

    /* Convert to Unicode string (including null terminator) */

    for (i = 0; i < Count; i++)
    {
        UnicodeString[i] = (UINT16) AsciiString[i];
    }

    /*
     * Just set the buffer size node to be the buffer length, regardless
     * of whether it was previously an integer or a default_arg placeholder
     */
    BufferLengthOp->Asl.ParseOpcode = PARSEOP_INTEGER;
    BufferLengthOp->Asl.AmlOpcode = AML_DWORD_OP;
    BufferLengthOp->Asl.Value.Integer = Length;
    UtSetParseOpName (BufferLengthOp);

    (void) OpcSetOptimalIntegerSize (BufferLengthOp);

    /* The Unicode string is a raw data buffer */

    InitializerOp->Asl.Value.Buffer = (UINT8 *) UnicodeString;
    InitializerOp->Asl.AmlOpcode = AML_RAW_DATA_BUFFER;
    InitializerOp->Asl.AmlLength = Length;
    InitializerOp->Asl.ParseOpcode = PARSEOP_RAW_DATA;
    InitializerOp->Asl.Child = NULL;
    UtSetParseOpName (InitializerOp);
}


/*******************************************************************************
 *
 * FUNCTION:    OpcDoEisaId
 *
 * PARAMETERS:  Op        - Parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert a string EISA ID to numeric representation. See the
 *              Pnp BIOS Specification for details. Here is an excerpt:
 *
 *              A seven character ASCII representation of the product
 *              identifier compressed into a 32-bit identifier. The seven
 *              character ID consists of a three character manufacturer code,
 *              a three character hexadecimal product identifier, and a one
 *              character hexadecimal revision number. The manufacturer code
 *              is a 3 uppercase character code that is compressed into 3 5-bit
 *              values as follows:
 *                  1) Find hex ASCII value for each letter
 *                  2) Subtract 40h from each ASCII value
 *                  3) Retain 5 least significant bits for each letter by
 *                     discarding upper 3 bits because they are always 0.
 *                  4) Compressed code = concatenate 0 and the 3 5-bit values
 *
 *              The format of the compressed product identifier is as follows:
 *              Byte 0: Bit 7       - Reserved (0)
 *                      Bits 6-2:   - 1st character of compressed mfg code
 *                      Bits 1-0    - Upper 2 bits of 2nd character of mfg code
 *              Byte 1: Bits 7-5    - Lower 3 bits of 2nd character of mfg code
 *                      Bits 4-0    - 3rd character of mfg code
 *              Byte 2: Bits 7-4    - 1st hex digit of product number
 *                      Bits 3-0    - 2nd hex digit of product number
 *              Byte 3: Bits 7-4    - 3st hex digit of product number
 *                      Bits 3-0    - Hex digit of the revision number
 *
 ******************************************************************************/

static void
OpcDoEisaId (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT32                  EisaId = 0;
    UINT32                  BigEndianId;
    char                    *InString;
    ACPI_STATUS             Status = AE_OK;
    UINT32                  i;


    InString = (char *) Op->Asl.Value.String;

    /*
     * The EISAID string must be exactly 7 characters and of the form
     * "UUUXXXX" -- 3 uppercase letters and 4 hex digits (e.g., "PNP0001")
     */
    if (strlen (InString) != 7)
    {
        Status = AE_BAD_PARAMETER;
    }
    else
    {
        /* Check all 7 characters for correct format */

        for (i = 0; i < 7; i++)
        {
            /* First 3 characters must be uppercase letters */

            if (i < 3)
            {
                if (!isupper ((int) InString[i]))
                {
                    Status = AE_BAD_PARAMETER;
                }
            }

            /* Last 4 characters must be hex digits */

            else if (!isxdigit ((int) InString[i]))
            {
                Status = AE_BAD_PARAMETER;
            }
        }
    }

    if (ACPI_FAILURE (Status))
    {
        AslError (ASL_ERROR, ASL_MSG_INVALID_EISAID, Op, Op->Asl.Value.String);
    }
    else
    {
        /* Create ID big-endian first (bits are contiguous) */

        BigEndianId =
            (UINT32) ((UINT8) (InString[0] - 0x40)) << 26 |
            (UINT32) ((UINT8) (InString[1] - 0x40)) << 21 |
            (UINT32) ((UINT8) (InString[2] - 0x40)) << 16 |

            (AcpiUtAsciiCharToHex (InString[3])) << 12 |
            (AcpiUtAsciiCharToHex (InString[4])) << 8  |
            (AcpiUtAsciiCharToHex (InString[5])) << 4  |
             AcpiUtAsciiCharToHex (InString[6]);

        /* Swap to little-endian to get final ID (see function header) */

        EisaId = AcpiUtDwordByteSwap (BigEndianId);
    }

    /*
     * Morph the Op into an integer, regardless of whether there
     * was an error in the EISAID string
     */
    Op->Asl.Value.Integer = EisaId;

    Op->Asl.CompileFlags &= ~OP_COMPILE_TIME_CONST;
    Op->Asl.ParseOpcode = PARSEOP_INTEGER;
    (void) OpcSetOptimalIntegerSize (Op);

    /* Op is now an integer */

    UtSetParseOpName (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    OpcDoUuId
 *
 * PARAMETERS:  Op                  - Parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert UUID string to 16-byte buffer
 *
 ******************************************************************************/

static void
OpcDoUuId (
    ACPI_PARSE_OBJECT       *Op)
{
    char                    *InString;
    UINT8                   *Buffer;
    ACPI_STATUS             Status = AE_OK;
    ACPI_PARSE_OBJECT       *NewOp;


    InString = ACPI_CAST_PTR (char, Op->Asl.Value.String);
    Buffer = UtLocalCalloc (16);

    Status = AuValidateUuid (InString);
    if (ACPI_FAILURE (Status))
    {
        AslError (ASL_ERROR, ASL_MSG_INVALID_UUID, Op, Op->Asl.Value.String);
    }
    else
    {
        AcpiUtConvertStringToUuid (InString, Buffer);
    }

    /* Change Op to a Buffer */

    Op->Asl.ParseOpcode = PARSEOP_BUFFER;
    Op->Common.AmlOpcode = AML_BUFFER_OP;

    /* Disable further optimization */

    Op->Asl.CompileFlags &= ~OP_COMPILE_TIME_CONST;
    UtSetParseOpName (Op);

    /* Child node is the buffer length */

    NewOp = TrAllocateOp (PARSEOP_INTEGER);

    NewOp->Asl.AmlOpcode = AML_BYTE_OP;
    NewOp->Asl.Value.Integer = 16;
    NewOp->Asl.Parent = Op;

    Op->Asl.Child = NewOp;
    Op = NewOp;

    /* Peer to the child is the raw buffer data */

    NewOp = TrAllocateOp (PARSEOP_RAW_DATA);
    NewOp->Asl.AmlOpcode = AML_RAW_DATA_BUFFER;
    NewOp->Asl.AmlLength = 16;
    NewOp->Asl.Value.String = ACPI_CAST_PTR (char, Buffer);
    NewOp->Asl.Parent = Op->Asl.Parent;

    Op->Asl.Next = NewOp;
}


/*******************************************************************************
 *
 * FUNCTION:    OpcGenerateAmlOpcode
 *
 * PARAMETERS:  Op                  - Parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generate the AML opcode associated with the node and its
 *              parse (lex/flex) keyword opcode. Essentially implements
 *              a mapping between the parse opcodes and the actual AML opcodes.
 *
 ******************************************************************************/

void
OpcGenerateAmlOpcode (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT16                  Index;


    Index = (UINT16) (Op->Asl.ParseOpcode - ASL_PARSE_OPCODE_BASE);

    Op->Asl.AmlOpcode     = AslKeywordMapping[Index].AmlOpcode;
    Op->Asl.AcpiBtype     = AslKeywordMapping[Index].AcpiBtype;
    Op->Asl.CompileFlags |= AslKeywordMapping[Index].Flags;

    if (!Op->Asl.Value.Integer)
    {
        Op->Asl.Value.Integer = AslKeywordMapping[Index].Value;
    }

    /* Special handling for some opcodes */

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_INTEGER:
        /*
         * Set the opcode based on the size of the integer
         */
        (void) OpcSetOptimalIntegerSize (Op);
        break;

    case PARSEOP_OFFSET:

        Op->Asl.AmlOpcodeLength = 1;
        break;

    case PARSEOP_ACCESSAS:

        OpcDoAccessAs (Op);
        break;

    case PARSEOP_CONNECTION:

        OpcDoConnection (Op);
        break;

    case PARSEOP_EISAID:

        OpcDoEisaId (Op);
        break;

    case PARSEOP_PRINTF:

        OpcDoPrintf (Op);
        break;

    case PARSEOP_FPRINTF:

        OpcDoFprintf (Op);
        break;

    case PARSEOP_TOPLD:

        OpcDoPld (Op);
        break;

    case PARSEOP_TOUUID:

        OpcDoUuId (Op);
        break;

    case PARSEOP_UNICODE:

        OpcDoUnicode (Op);
        break;

    case PARSEOP_INCLUDE:

        AslGbl_HasIncludeFiles = TRUE;
        break;

    case PARSEOP_TIMER:

        if (AcpiGbl_IntegerBitWidth == 32)
        {
            AslError (ASL_REMARK, ASL_MSG_TRUNCATION, Op, NULL);
        }
        break;

    default:

        /* Nothing to do for other opcodes */

        break;
    }

    return;
}
