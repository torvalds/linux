/*******************************************************************************
 *
 * Module Name: dmopcode - AML disassembler, specific AML opcodes
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acdebug.h>
#include <contrib/dev/acpica/include/acconvert.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dmopcode")


/* Local prototypes */

static void
AcpiDmMatchKeyword (
    ACPI_PARSE_OBJECT       *Op);

static void
AcpiDmConvertToElseIf (
    ACPI_PARSE_OBJECT       *Op);

static void
AcpiDmPromoteSubtree (
    ACPI_PARSE_OBJECT       *StartOp);

/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDisplayTargetPathname
 *
 * PARAMETERS:  Op              - Parse object
 *
 * RETURN:      None
 *
 * DESCRIPTION: For AML opcodes that have a target operand, display the full
 *              pathname for the target, in a comment field. Handles Return()
 *              statements also.
 *
 ******************************************************************************/

void
AcpiDmDisplayTargetPathname (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *NextOp;
    ACPI_PARSE_OBJECT       *PrevOp = NULL;
    char                    *Pathname;
    const ACPI_OPCODE_INFO  *OpInfo;


    if (Op->Common.AmlOpcode == AML_RETURN_OP)
    {
        PrevOp = Op->Asl.Value.Arg;
    }
    else
    {
        OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);
        if (!(OpInfo->Flags & AML_HAS_TARGET))
        {
            return;
        }

        /* Target is the last Op in the arg list */

        NextOp = Op->Asl.Value.Arg;
        while (NextOp)
        {
            PrevOp = NextOp;
            NextOp = PrevOp->Asl.Next;
        }
    }

    if (!PrevOp)
    {
        return;
    }

    /* We must have a namepath AML opcode */

    if (PrevOp->Asl.AmlOpcode != AML_INT_NAMEPATH_OP)
    {
        return;
    }

    /* A null string is the "no target specified" case */

    if (!PrevOp->Asl.Value.String)
    {
        return;
    }

    /* No node means "unresolved external reference" */

    if (!PrevOp->Asl.Node)
    {
        AcpiOsPrintf (" /* External reference */");
        return;
    }

    /* Ignore if path is already from the root */

    if (*PrevOp->Asl.Value.String == '\\')
    {
        return;
    }

    /* Now: we can get the full pathname */

    Pathname = AcpiNsGetExternalPathname (PrevOp->Asl.Node);
    if (!Pathname)
    {
        return;
    }

    AcpiOsPrintf (" /* %s */", Pathname);
    ACPI_FREE (Pathname);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmNotifyDescription
 *
 * PARAMETERS:  Op              - Name() parse object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Emit a description comment for the value associated with a
 *              Notify() operator.
 *
 ******************************************************************************/

void
AcpiDmNotifyDescription (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *NextOp;
    ACPI_NAMESPACE_NODE     *Node;
    UINT8                   NotifyValue;
    UINT8                   Type = ACPI_TYPE_ANY;


    /* The notify value is the second argument */

    NextOp = Op->Asl.Value.Arg;
    NextOp = NextOp->Asl.Next;

    switch (NextOp->Common.AmlOpcode)
    {
    case AML_ZERO_OP:
    case AML_ONE_OP:

        NotifyValue = (UINT8) NextOp->Common.AmlOpcode;
        break;

    case AML_BYTE_OP:

        NotifyValue = (UINT8) NextOp->Asl.Value.Integer;
        break;

    default:
        return;
    }

    /*
     * Attempt to get the namespace node so we can determine the object type.
     * Some notify values are dependent on the object type (Device, Thermal,
     * or Processor).
     */
    Node = Op->Asl.Node;
    if (Node)
    {
        Type = Node->Type;
    }

    AcpiOsPrintf (" // %s", AcpiUtGetNotifyName (NotifyValue, Type));
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPredefinedDescription
 *
 * PARAMETERS:  Op              - Name() parse object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Emit a description comment for a predefined ACPI name.
 *              Used for iASL compiler only.
 *
 ******************************************************************************/

void
AcpiDmPredefinedDescription (
    ACPI_PARSE_OBJECT       *Op)
{
#ifdef ACPI_ASL_COMPILER
    const AH_PREDEFINED_NAME    *Info;
    char                        *NameString;
    int                         LastCharIsDigit;
    int                         LastCharsAreHex;


    if (!Op)
    {
        return;
    }

    /* Ensure that the comment field is emitted only once */

    if (Op->Common.DisasmFlags & ACPI_PARSEOP_PREDEFINED_CHECKED)
    {
        return;
    }
    Op->Common.DisasmFlags |= ACPI_PARSEOP_PREDEFINED_CHECKED;

    /* Predefined name must start with an underscore */

    NameString = ACPI_CAST_PTR (char, &Op->Named.Name);
    if (NameString[0] != '_')
    {
        return;
    }

    /*
     * Check for the special ACPI names:
     * _ACd, _ALd, _EJd, _Exx, _Lxx, _Qxx, _Wxx, _T_a
     * (where d=decimal_digit, x=hex_digit, a=anything)
     *
     * Convert these to the generic name for table lookup.
     * Note: NameString is guaranteed to be upper case here.
     */
    LastCharIsDigit =
        (isdigit ((int) NameString[3]));    /* d */
    LastCharsAreHex =
        (isxdigit ((int) NameString[2]) &&  /* xx */
         isxdigit ((int) NameString[3]));

    switch (NameString[1])
    {
    case 'A':

        if ((NameString[2] == 'C') && (LastCharIsDigit))
        {
            NameString = "_ACx";
        }
        else if ((NameString[2] == 'L') && (LastCharIsDigit))
        {
            NameString = "_ALx";
        }
        break;

    case 'E':

        if ((NameString[2] == 'J') && (LastCharIsDigit))
        {
            NameString = "_EJx";
        }
        else if (LastCharsAreHex)
        {
            NameString = "_Exx";
        }
        break;

    case 'L':

        if (LastCharsAreHex)
        {
            NameString = "_Lxx";
        }
        break;

    case 'Q':

        if (LastCharsAreHex)
        {
            NameString = "_Qxx";
        }
        break;

    case 'T':

        if (NameString[2] == '_')
        {
            NameString = "_T_x";
        }
        break;

    case 'W':

        if (LastCharsAreHex)
        {
            NameString = "_Wxx";
        }
        break;

    default:

        break;
    }

    /* Match the name in the info table */

    Info = AcpiAhMatchPredefinedName (NameString);
    if (Info)
    {
        AcpiOsPrintf ("  // %4.4s: %s",
            NameString, ACPI_CAST_PTR (char, Info->Description));
    }

#endif
    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmFieldPredefinedDescription
 *
 * PARAMETERS:  Op              - Parse object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Emit a description comment for a resource descriptor tag
 *              (which is a predefined ACPI name.) Used for iASL compiler only.
 *
 ******************************************************************************/

void
AcpiDmFieldPredefinedDescription (
    ACPI_PARSE_OBJECT       *Op)
{
#ifdef ACPI_ASL_COMPILER
    ACPI_PARSE_OBJECT       *IndexOp;
    char                    *Tag;
    const ACPI_OPCODE_INFO  *OpInfo;
    const AH_PREDEFINED_NAME *Info;


    if (!Op)
    {
        return;
    }

    /* Ensure that the comment field is emitted only once */

    if (Op->Common.DisasmFlags & ACPI_PARSEOP_PREDEFINED_CHECKED)
    {
        return;
    }
    Op->Common.DisasmFlags |= ACPI_PARSEOP_PREDEFINED_CHECKED;

    /*
     * Op must be one of the Create* operators: CreateField, CreateBitField,
     * CreateByteField, CreateWordField, CreateDwordField, CreateQwordField
     */
    OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);
    if (!(OpInfo->Flags & AML_CREATE))
    {
        return;
    }

    /* Second argument is the Index argument */

    IndexOp = Op->Common.Value.Arg;
    IndexOp = IndexOp->Common.Next;

    /* Index argument must be a namepath */

    if (IndexOp->Common.AmlOpcode != AML_INT_NAMEPATH_OP)
    {
        return;
    }

    /* Major cheat: We previously put the Tag ptr in the Node field */

    Tag = ACPI_CAST_PTR (char, IndexOp->Common.Node);
    if (!Tag || (*Tag == 0))
    {
        return;
    }

    /* Is the tag a predefined name? */

    Info = AcpiAhMatchPredefinedName (Tag);
    if (!Info)
    {
        /* Not a predefined name (does not start with underscore) */

        return;
    }

    AcpiOsPrintf ("  // %4.4s: %s", Tag,
        ACPI_CAST_PTR (char, Info->Description));

    /* String contains the prefix path, free it */

    ACPI_FREE (IndexOp->Common.Value.String);
    IndexOp->Common.Value.String = NULL;
#endif

    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmMethodFlags
 *
 * PARAMETERS:  Op              - Method Object to be examined
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode control method flags
 *
 ******************************************************************************/

void
AcpiDmMethodFlags (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT32                  Flags;
    UINT32                  Args;


    /* The next Op contains the flags */

    Op = AcpiPsGetDepthNext (NULL, Op);
    Flags = (UINT8) Op->Common.Value.Integer;
    Args = Flags & 0x07;

    /* Mark the Op as completed */

    Op->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;

    /* 1) Method argument count */

    AcpiOsPrintf (", %u, ", Args);

    /* 2) Serialize rule */

    if (!(Flags & 0x08))
    {
        AcpiOsPrintf ("Not");
    }

    AcpiOsPrintf ("Serialized");

    /* 3) SyncLevel */

    if (Flags & 0xF0)
    {
        AcpiOsPrintf (", %u", Flags >> 4);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmFieldFlags
 *
 * PARAMETERS:  Op              - Field Object to be examined
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode Field definition flags
 *
 ******************************************************************************/

void
AcpiDmFieldFlags (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT32                  Flags;


    Op = Op->Common.Next;
    Flags = (UINT8) Op->Common.Value.Integer;

    /* Mark the Op as completed */

    Op->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;

    AcpiOsPrintf ("%s, ", AcpiGbl_AccessTypes [Flags & 0x07]);
    AcpiOsPrintf ("%s, ", AcpiGbl_LockRule [(Flags & 0x10) >> 4]);
    AcpiOsPrintf ("%s)",  AcpiGbl_UpdateRules [(Flags & 0x60) >> 5]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmAddressSpace
 *
 * PARAMETERS:  SpaceId         - ID to be translated
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a SpaceId to an AddressSpaceKeyword
 *
 ******************************************************************************/

void
AcpiDmAddressSpace (
    UINT8                   SpaceId)
{

    if (SpaceId >= ACPI_NUM_PREDEFINED_REGIONS)
    {
        if (SpaceId == 0x7F)
        {
            AcpiOsPrintf ("FFixedHW, ");
        }
        else
        {
            AcpiOsPrintf ("0x%.2X, ", SpaceId);
        }
    }
    else
    {
        AcpiOsPrintf ("%s, ", AcpiGbl_RegionTypes [SpaceId]);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmRegionFlags
 *
 * PARAMETERS:  Op              - Object to be examined
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode OperationRegion flags
 *
 ******************************************************************************/

void
AcpiDmRegionFlags (
    ACPI_PARSE_OBJECT       *Op)
{

    /* The next Op contains the SpaceId */

    Op = AcpiPsGetDepthNext (NULL, Op);

    /* Mark the Op as completed */

    Op->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;

    AcpiOsPrintf (", ");
    AcpiDmAddressSpace ((UINT8) Op->Common.Value.Integer);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmMatchOp
 *
 * PARAMETERS:  Op              - Match Object to be examined
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode Match opcode operands
 *
 ******************************************************************************/

void
AcpiDmMatchOp (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *NextOp;


    NextOp = AcpiPsGetDepthNext (NULL, Op);
    NextOp = NextOp->Common.Next;

    if (!NextOp)
    {
        /* Handle partial tree during single-step */

        return;
    }

    /* Mark the two nodes that contain the encoding for the match keywords */

    NextOp->Common.DisasmOpcode = ACPI_DASM_MATCHOP;

    NextOp = NextOp->Common.Next;
    NextOp = NextOp->Common.Next;
    NextOp->Common.DisasmOpcode = ACPI_DASM_MATCHOP;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmMatchKeyword
 *
 * PARAMETERS:  Op              - Match Object to be examined
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode Match opcode operands
 *
 ******************************************************************************/

static void
AcpiDmMatchKeyword (
    ACPI_PARSE_OBJECT       *Op)
{

    if (((UINT32) Op->Common.Value.Integer) > ACPI_MAX_MATCH_OPCODE)
    {
        AcpiOsPrintf ("/* Unknown Match Keyword encoding */");
    }
    else
    {
        AcpiOsPrintf ("%s",
            AcpiGbl_MatchOps[(ACPI_SIZE) Op->Common.Value.Integer]);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDisassembleOneOp
 *
 * PARAMETERS:  WalkState           - Current walk info
 *              Info                - Parse tree walk info
 *              Op                  - Op that is to be printed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Disassemble a single AML opcode
 *
 ******************************************************************************/

void
AcpiDmDisassembleOneOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_OP_WALK_INFO       *Info,
    ACPI_PARSE_OBJECT       *Op)
{
    const ACPI_OPCODE_INFO  *OpInfo = NULL;
    UINT32                  Offset;
    UINT32                  Length;
    ACPI_PARSE_OBJECT       *Child;
    ACPI_STATUS             Status;
    UINT8                   *Aml;
    const AH_DEVICE_ID      *IdInfo;


    if (!Op)
    {
        AcpiOsPrintf ("<NULL OP PTR>");
        return;
    }

    if (Op->Common.DisasmFlags & ACPI_PARSEOP_ELSEIF)
    {
        return; /* ElseIf macro was already emitted */
    }

    switch (Op->Common.DisasmOpcode)
    {
    case ACPI_DASM_MATCHOP:

        AcpiDmMatchKeyword (Op);
        return;

    case ACPI_DASM_LNOT_SUFFIX:

        if (!AcpiGbl_CstyleDisassembly)
        {
            switch (Op->Common.AmlOpcode)
            {
            case AML_LOGICAL_EQUAL_OP:
                AcpiOsPrintf ("LNotEqual");
                break;

            case AML_LOGICAL_GREATER_OP:
                AcpiOsPrintf ("LLessEqual");
                break;

            case AML_LOGICAL_LESS_OP:
                AcpiOsPrintf ("LGreaterEqual");
                break;

            default:
                break;
            }
        }

        Op->Common.DisasmOpcode = 0;
        Op->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
        return;

    default:
        break;
    }

    OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);

    /* The op and arguments */

    switch (Op->Common.AmlOpcode)
    {
    case AML_LOGICAL_NOT_OP:

        Child = Op->Common.Value.Arg;
        if ((Child->Common.AmlOpcode == AML_LOGICAL_EQUAL_OP) ||
            (Child->Common.AmlOpcode == AML_LOGICAL_GREATER_OP) ||
            (Child->Common.AmlOpcode == AML_LOGICAL_LESS_OP))
        {
            Child->Common.DisasmOpcode = ACPI_DASM_LNOT_SUFFIX;
            Op->Common.DisasmOpcode = ACPI_DASM_LNOT_PREFIX;
        }
        else
        {
            AcpiOsPrintf ("%s", OpInfo->Name);
        }
        break;

    case AML_BYTE_OP:

        AcpiOsPrintf ("0x%2.2X", (UINT32) Op->Common.Value.Integer);
        break;

    case AML_WORD_OP:

        if (Op->Common.DisasmOpcode == ACPI_DASM_EISAID)
        {
            AcpiDmDecompressEisaId ((UINT32) Op->Common.Value.Integer);
        }
        else
        {
            AcpiOsPrintf ("0x%4.4X", (UINT32) Op->Common.Value.Integer);
        }
        break;

    case AML_DWORD_OP:

        if (Op->Common.DisasmOpcode == ACPI_DASM_EISAID)
        {
            AcpiDmDecompressEisaId ((UINT32) Op->Common.Value.Integer);
        }
        else
        {
            AcpiOsPrintf ("0x%8.8X", (UINT32) Op->Common.Value.Integer);
        }
        break;

    case AML_QWORD_OP:

        AcpiOsPrintf ("0x%8.8X%8.8X",
            ACPI_FORMAT_UINT64 (Op->Common.Value.Integer));
        break;

    case AML_STRING_OP:

        AcpiUtPrintString (Op->Common.Value.String, ACPI_UINT16_MAX);

        /* For _HID/_CID strings, attempt to output a descriptive comment */

        if (Op->Common.DisasmOpcode == ACPI_DASM_HID_STRING)
        {
            /* If we know about the ID, emit the description */

            IdInfo = AcpiAhMatchHardwareId (Op->Common.Value.String);
            if (IdInfo)
            {
                AcpiOsPrintf (" /* %s */", IdInfo->Description);
            }
        }
        break;

    case AML_BUFFER_OP:
        /*
         * Determine the type of buffer. We can have one of the following:
         *
         * 1) ResourceTemplate containing Resource Descriptors.
         * 2) Unicode String buffer
         * 3) ASCII String buffer
         * 4) Raw data buffer (if none of the above)
         *
         * Since there are no special AML opcodes to differentiate these
         * types of buffers, we have to closely look at the data in the
         * buffer to determine the type.
         */
        if (!AcpiGbl_NoResourceDisassembly)
        {
            Status = AcpiDmIsResourceTemplate (WalkState, Op);
            if (ACPI_SUCCESS (Status))
            {
                Op->Common.DisasmOpcode = ACPI_DASM_RESOURCE;
                AcpiOsPrintf ("ResourceTemplate");
                break;
            }
            else if (Status == AE_AML_NO_RESOURCE_END_TAG)
            {
                AcpiOsPrintf (
                    "/**** Is ResourceTemplate, "
                    "but EndTag not at buffer end ****/ ");
            }
        }

        if (AcpiDmIsUuidBuffer (Op))
        {
            Op->Common.DisasmOpcode = ACPI_DASM_UUID;
            AcpiOsPrintf ("ToUUID (");
        }
        else if (AcpiDmIsUnicodeBuffer (Op))
        {
            Op->Common.DisasmOpcode = ACPI_DASM_UNICODE;
            AcpiOsPrintf ("Unicode (");
        }
        else if (AcpiDmIsStringBuffer (Op))
        {
            Op->Common.DisasmOpcode = ACPI_DASM_STRING;
            AcpiOsPrintf ("Buffer");
        }
        else if (AcpiDmIsPldBuffer (Op))
        {
            Op->Common.DisasmOpcode = ACPI_DASM_PLD_METHOD;
            AcpiOsPrintf ("ToPLD (");
        }
        else
        {
            Op->Common.DisasmOpcode = ACPI_DASM_BUFFER;
            AcpiOsPrintf ("Buffer");
        }
        break;

    case AML_INT_NAMEPATH_OP:

        AcpiDmNamestring (Op->Common.Value.Name);
        break;

    case AML_INT_NAMEDFIELD_OP:

        Length = AcpiDmDumpName (Op->Named.Name);

        AcpiOsPrintf (",");
        ASL_CV_PRINT_ONE_COMMENT (Op, AML_NAMECOMMENT, NULL, 0);
        AcpiOsPrintf ("%*.s  %u", (unsigned) (5 - Length), " ",
            (UINT32) Op->Common.Value.Integer);

        AcpiDmCommaIfFieldMember (Op);

        Info->BitOffset += (UINT32) Op->Common.Value.Integer;
        break;

    case AML_INT_RESERVEDFIELD_OP:

        /* Offset() -- Must account for previous offsets */

        Offset = (UINT32) Op->Common.Value.Integer;
        Info->BitOffset += Offset;

        if (Info->BitOffset % 8 == 0)
        {
            AcpiOsPrintf ("Offset (0x%.2X)", ACPI_DIV_8 (Info->BitOffset));
        }
        else
        {
            AcpiOsPrintf ("    ,   %u", Offset);
        }

        AcpiDmCommaIfFieldMember (Op);
        break;

    case AML_INT_ACCESSFIELD_OP:
    case AML_INT_EXTACCESSFIELD_OP:

        AcpiOsPrintf ("AccessAs (%s, ",
            AcpiGbl_AccessTypes [(UINT32) (Op->Common.Value.Integer & 0x7)]);

        AcpiDmDecodeAttribute ((UINT8) (Op->Common.Value.Integer >> 8));

        if (Op->Common.AmlOpcode == AML_INT_EXTACCESSFIELD_OP)
        {
            AcpiOsPrintf (" (0x%2.2X)", (unsigned)
                ((Op->Common.Value.Integer >> 16) & 0xFF));
        }

        AcpiOsPrintf (")");
        AcpiDmCommaIfFieldMember (Op);
        ASL_CV_PRINT_ONE_COMMENT (Op, AML_COMMENT_END_NODE, NULL, 0);
        break;

    case AML_INT_CONNECTION_OP:
        /*
         * Two types of Connection() - one with a buffer object, the
         * other with a namestring that points to a buffer object.
         */
        AcpiOsPrintf ("Connection (");
        Child = Op->Common.Value.Arg;

        if (Child->Common.AmlOpcode == AML_INT_BYTELIST_OP)
        {
            AcpiOsPrintf ("\n");

            Aml = Child->Named.Data;
            Length = (UINT32) Child->Common.Value.Integer;

            Info->Level += 1;
            Info->MappingOp = Op;
            Op->Common.DisasmOpcode = ACPI_DASM_RESOURCE;

            AcpiDmResourceTemplate (Info, Op->Common.Parent, Aml, Length);

            Info->Level -= 1;
            AcpiDmIndent (Info->Level);
        }
        else
        {
            AcpiDmNamestring (Child->Common.Value.Name);
        }

        AcpiOsPrintf (")");
        AcpiDmCommaIfFieldMember (Op);
        ASL_CV_PRINT_ONE_COMMENT (Op, AML_COMMENT_END_NODE, NULL, 0);
        ASL_CV_PRINT_ONE_COMMENT (Op, AMLCOMMENT_INLINE, NULL, 0);
        AcpiOsPrintf ("\n");

        Op->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE; /* for now, ignore in AcpiDmAscendingOp */
        Child->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
        break;

    case AML_INT_BYTELIST_OP:

        AcpiDmByteList (Info, Op);
        break;

    case AML_INT_METHODCALL_OP:

        Op = AcpiPsGetDepthNext (NULL, Op);
        Op->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;

        AcpiDmNamestring (Op->Common.Value.Name);
        break;

    case AML_WHILE_OP:

        if (Op->Common.DisasmOpcode == ACPI_DASM_SWITCH)
        {
            AcpiOsPrintf ("%s", "Switch");
            break;
        }

        AcpiOsPrintf ("%s", OpInfo->Name);
        break;

    case AML_IF_OP:

        if (Op->Common.DisasmOpcode == ACPI_DASM_CASE)
        {
            AcpiOsPrintf ("%s", "Case");
            break;
        }

        AcpiOsPrintf ("%s", OpInfo->Name);
        break;

    case AML_ELSE_OP:

        AcpiDmConvertToElseIf (Op);
        break;

    case AML_EXTERNAL_OP:

        if (AcpiGbl_DmEmitExternalOpcodes)
        {
            AcpiDmEmitExternal (Op, AcpiPsGetArg(Op, 0));
        }

        break;

    default:

        /* Just get the opcode name and print it */

        AcpiOsPrintf ("%s", OpInfo->Name);


#ifdef ACPI_DEBUGGER

        if ((Op->Common.AmlOpcode == AML_INT_RETURN_VALUE_OP) &&
            (WalkState) &&
            (WalkState->Results) &&
            (WalkState->ResultCount))
        {
            AcpiDbDecodeInternalObject (
                WalkState->Results->Results.ObjDesc [
                    (WalkState->ResultCount - 1) %
                        ACPI_RESULTS_FRAME_OBJ_NUM]);
        }
#endif

        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmConvertToElseIf
 *
 * PARAMETERS:  OriginalElseOp          - ELSE Object to be examined
 *
 * RETURN:      None. Emits either an "Else" or an "ElseIf" ASL operator.
 *
 * DESCRIPTION: Detect and convert an If..Else..If sequence to If..ElseIf
 *
 * EXAMPLE:
 *
 * This If..Else..If nested sequence:
 *
 *        If (Arg0 == 1)
 *        {
 *            Local0 = 4
 *        }
 *        Else
 *        {
 *            If (Arg0 == 2)
 *            {
 *                Local0 = 5
 *            }
 *        }
 *
 * Is converted to this simpler If..ElseIf sequence:
 *
 *        If (Arg0 == 1)
 *        {
 *            Local0 = 4
 *        }
 *        ElseIf (Arg0 == 2)
 *        {
 *            Local0 = 5
 *        }
 *
 * NOTE: There is no actual ElseIf AML opcode. ElseIf is essentially an ASL
 * macro that emits an Else opcode followed by an If opcode. This function
 * reverses these AML sequences back to an ElseIf macro where possible. This
 * can make the disassembled ASL code simpler and more like the original code.
 *
 ******************************************************************************/

static void
AcpiDmConvertToElseIf (
    ACPI_PARSE_OBJECT       *OriginalElseOp)
{
    ACPI_PARSE_OBJECT       *IfOp;
    ACPI_PARSE_OBJECT       *ElseOp;


    /*
     * To be able to perform the conversion, two conditions must be satisfied:
     * 1) The first child of the Else must be an If statement.
     * 2) The If block can only be followed by an Else block and these must
     *    be the only blocks under the original Else.
     */
    IfOp = OriginalElseOp->Common.Value.Arg;

    if (!IfOp ||
        (IfOp->Common.AmlOpcode != AML_IF_OP) ||
        (IfOp->Asl.Next && (IfOp->Asl.Next->Common.AmlOpcode != AML_ELSE_OP)))
    {
        /* Not a proper Else..If sequence, cannot convert to ElseIf */

        if (OriginalElseOp->Common.DisasmOpcode == ACPI_DASM_DEFAULT)
        {
            AcpiOsPrintf ("%s", "Default");
            return;
        }

        AcpiOsPrintf ("%s", "Else");
        return;
    }

    /* Cannot have anything following the If...Else block */

    ElseOp = IfOp->Common.Next;
    if (ElseOp && ElseOp->Common.Next)
    {
        if (OriginalElseOp->Common.DisasmOpcode == ACPI_DASM_DEFAULT)
        {
            AcpiOsPrintf ("%s", "Default");
            return;
        }

        AcpiOsPrintf ("%s", "Else");
        return;
    }

    if (OriginalElseOp->Common.DisasmOpcode == ACPI_DASM_DEFAULT)
    {
        /*
         * There is an ElseIf but in this case the Else is actually
         * a Default block for a Switch/Case statement. No conversion.
         */
        AcpiOsPrintf ("%s", "Default");
        return;
    }

    if (OriginalElseOp->Common.DisasmOpcode == ACPI_DASM_CASE)
    {
        /*
         * This ElseIf is actually a Case block for a Switch/Case
         * statement. Print Case but do not return so that we can
         * promote the subtree and keep the indentation level.
         */
        AcpiOsPrintf ("%s", "Case");
    }
    else
    {
       /* Emit ElseIf, mark the IF as now an ELSEIF */

        AcpiOsPrintf ("%s", "ElseIf");
    }

    IfOp->Common.DisasmFlags |= ACPI_PARSEOP_ELSEIF;

    /* The IF parent will now be the same as the original ELSE parent */

    IfOp->Common.Parent = OriginalElseOp->Common.Parent;

    /*
     * Update the NEXT pointers to restructure the parse tree, essentially
     * promoting an If..Else block up to the same level as the original
     * Else.
     *
     * Check if the IF has a corresponding ELSE peer
     */
    ElseOp = IfOp->Common.Next;
    if (ElseOp &&
        (ElseOp->Common.AmlOpcode == AML_ELSE_OP))
    {
        /* If an ELSE matches the IF, promote it also */

        ElseOp->Common.Parent = OriginalElseOp->Common.Parent;

        /* Promote the entire block under the ElseIf (All Next OPs) */

        AcpiDmPromoteSubtree (OriginalElseOp);
    }
    else
    {
        /* Otherwise, set the IF NEXT to the original ELSE NEXT */

        IfOp->Common.Next = OriginalElseOp->Common.Next;
    }

    /* Detach the child IF block from the original ELSE */

    OriginalElseOp->Common.Value.Arg = NULL;

    /* Ignore the original ELSE from now on */

    OriginalElseOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
    OriginalElseOp->Common.DisasmOpcode = ACPI_DASM_LNOT_PREFIX;

    /* Insert IF (now ELSEIF) as next peer of the original ELSE */

    OriginalElseOp->Common.Next = IfOp;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPromoteSubtree
 *
 * PARAMETERS:  StartOpOp           - Original parent of the entire subtree
 *
 * RETURN:      None
 *
 * DESCRIPTION: Promote an entire parse subtree up one level.
 *
 ******************************************************************************/

static void
AcpiDmPromoteSubtree (
    ACPI_PARSE_OBJECT       *StartOp)
{
    ACPI_PARSE_OBJECT       *Op;
    ACPI_PARSE_OBJECT       *ParentOp;


    /* New parent for subtree elements */

    ParentOp = StartOp->Common.Parent;

    /* First child starts the subtree */

    Op = StartOp->Common.Value.Arg;

    /* Walk the top-level elements of the subtree */

    while (Op)
    {
        Op->Common.Parent = ParentOp;
        if (!Op->Common.Next)
        {
            /* Last Op in list, update its next field */

            Op->Common.Next = StartOp->Common.Next;
            break;
        }
        Op = Op->Common.Next;
    }
}
