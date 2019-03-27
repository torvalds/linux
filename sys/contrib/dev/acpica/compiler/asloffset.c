/******************************************************************************
 *
 * Module Name: asloffset - Generate a C "offset table" for BIOS use.
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
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("asloffset")


/* Local prototypes */

static void
LsEmitOffsetTableEntry (
    UINT32                  FileId,
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  NamepathOffset,
    UINT32                  Offset,
    char                    *OpName,
    UINT64                  Value,
    UINT8                   AmlOpcode,
    UINT16                  ParentOpcode);


/*******************************************************************************
 *
 * FUNCTION:    LsAmlOffsetWalk
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Process one node during a offset table file generation.
 *
 * Three types of objects are currently emitted to the offset table:
 *   1) Tagged (named) resource descriptors
 *   2) Named integer objects with constant integer values
 *   3) Named package objects
 *   4) Operation Regions that have constant Offset (address) parameters
 *   5) Control methods
 *
 * The offset table allows the BIOS to dynamically update the values of these
 * objects at boot time.
 *
 ******************************************************************************/

ACPI_STATUS
LsAmlOffsetWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    UINT32                  FileId = (UINT32) ACPI_TO_INTEGER (Context);
    ACPI_NAMESPACE_NODE     *Node;
    UINT32                  Length;
    UINT32                  NamepathOffset;
    UINT32                  DataOffset;
    ACPI_PARSE_OBJECT       *NextOp;


    /* Ignore actual data blocks for resource descriptors */

    if (Op->Asl.CompileFlags & OP_IS_RESOURCE_DATA)
    {
        return (AE_OK); /* Do NOT update the global AML offset */
    }

    /* We are only interested in named objects (have a namespace node) */

    Node = Op->Asl.Node;
    if (!Node)
    {
        AslGbl_CurrentAmlOffset += Op->Asl.FinalAmlLength;
        return (AE_OK);
    }

    /* Named resource descriptor (has a descriptor tag) */

    if ((Node->Type == ACPI_TYPE_LOCAL_RESOURCE) &&
        (Op->Asl.CompileFlags & OP_IS_RESOURCE_DESC))
    {
        LsEmitOffsetTableEntry (FileId, Node, 0, AslGbl_CurrentAmlOffset,
            Op->Asl.ParseOpName, 0, Op->Asl.Extra, AML_BUFFER_OP);

        AslGbl_CurrentAmlOffset += Op->Asl.FinalAmlLength;
        return (AE_OK);
    }

    switch (Op->Asl.AmlOpcode)
    {
    case AML_NAME_OP:

        /* Named object -- Name (NameString, DataRefObject) */

        if (!Op->Asl.Child)
        {
            FlPrintFile (FileId, "%s NO CHILD!\n", AslGbl_MsgBuffer);
            return (AE_OK);
        }

        Length = Op->Asl.FinalAmlLength;

        /* Get to the NameSeg/NamePath Op (and length of the name) */

        Op = Op->Asl.Child;

        /* Get offset of last nameseg and the actual data */

        NamepathOffset = AslGbl_CurrentAmlOffset + Length +
            (Op->Asl.FinalAmlLength - ACPI_NAME_SIZE);

        DataOffset = AslGbl_CurrentAmlOffset + Length +
            Op->Asl.FinalAmlLength;

        /* Get actual value associated with the name */

        Op = Op->Asl.Next;
        switch (Op->Asl.AmlOpcode)
        {
        case AML_BYTE_OP:
        case AML_WORD_OP:
        case AML_DWORD_OP:
        case AML_QWORD_OP:

            /* The +1 is to handle the integer size prefix (opcode) */

            LsEmitOffsetTableEntry (FileId, Node, NamepathOffset, (DataOffset + 1),
                Op->Asl.ParseOpName, Op->Asl.Value.Integer,
                (UINT8) Op->Asl.AmlOpcode, AML_NAME_OP);
            break;

        case AML_ONE_OP:
        case AML_ONES_OP:
        case AML_ZERO_OP:

            /* For these, offset will point to the opcode */

            LsEmitOffsetTableEntry (FileId, Node, NamepathOffset, DataOffset,
                Op->Asl.ParseOpName, Op->Asl.Value.Integer,
                (UINT8) Op->Asl.AmlOpcode, AML_NAME_OP);
            break;

        case AML_PACKAGE_OP:
        case AML_VARIABLE_PACKAGE_OP:

            /* Get the package element count */

            NextOp = Op->Asl.Child;

            LsEmitOffsetTableEntry (FileId, Node, NamepathOffset, DataOffset,
                Op->Asl.ParseOpName, NextOp->Asl.Value.Integer,
                (UINT8) Op->Asl.AmlOpcode, AML_NAME_OP);
            break;

         default:
             break;
        }

        AslGbl_CurrentAmlOffset += Length;
        return (AE_OK);

    case AML_REGION_OP:

        /* OperationRegion (NameString, RegionSpace, RegionOffset, RegionLength) */

        Length = Op->Asl.FinalAmlLength;

        /* Get the name/namepath node */

        NextOp = Op->Asl.Child;

        /* Get offset of last nameseg and the actual data */

        NamepathOffset = AslGbl_CurrentAmlOffset + Length +
            (NextOp->Asl.FinalAmlLength - ACPI_NAME_SIZE);

        DataOffset = AslGbl_CurrentAmlOffset + Length +
            (NextOp->Asl.FinalAmlLength + 1);

        /* Get the SpaceId node, then the Offset (address) node */

        NextOp = NextOp->Asl.Next;
        NextOp = NextOp->Asl.Next;

        switch (NextOp->Asl.AmlOpcode)
        {
        /*
         * We are only interested in integer constants that can be changed
         * at boot time. Note, the One/Ones/Zero opcodes are considered
         * non-changeable, so we ignore them here.
         */
        case AML_BYTE_OP:
        case AML_WORD_OP:
        case AML_DWORD_OP:
        case AML_QWORD_OP:

            LsEmitOffsetTableEntry (FileId, Node, NamepathOffset, (DataOffset + 1),
                Op->Asl.ParseOpName, NextOp->Asl.Value.Integer,
                (UINT8) NextOp->Asl.AmlOpcode, AML_REGION_OP);

            AslGbl_CurrentAmlOffset += Length;
            return (AE_OK);

        default:
            break;
        }
        break;

    case AML_METHOD_OP:

        /* Method (Namepath, ...) */

        Length = Op->Asl.FinalAmlLength;

        /* Get the NameSeg/NamePath Op */

        NextOp = Op->Asl.Child;

        /* Get offset of last nameseg and the actual data (flags byte) */

        NamepathOffset = AslGbl_CurrentAmlOffset + Length +
            (NextOp->Asl.FinalAmlLength - ACPI_NAME_SIZE);

        DataOffset = AslGbl_CurrentAmlOffset + Length +
            NextOp->Asl.FinalAmlLength;

        /* Get the flags byte Op */

        NextOp = NextOp->Asl.Next;

        LsEmitOffsetTableEntry (FileId, Node, NamepathOffset, DataOffset,
            Op->Asl.ParseOpName, NextOp->Asl.Value.Integer,
            (UINT8) Op->Asl.AmlOpcode, AML_METHOD_OP);
        break;

    case AML_PROCESSOR_OP:

        /* Processor (Namepath, ProcessorId, Address, Length) */

        Length = Op->Asl.FinalAmlLength;
        NextOp = Op->Asl.Child;     /* Get Namepath */

        /* Get offset of last nameseg and the actual data (PBlock address) */

        NamepathOffset = AslGbl_CurrentAmlOffset + Length +
            (NextOp->Asl.FinalAmlLength - ACPI_NAME_SIZE);

        DataOffset = AslGbl_CurrentAmlOffset + Length +
            (NextOp->Asl.FinalAmlLength + 1);

        NextOp = NextOp->Asl.Next;  /* Get ProcessorID (BYTE) */
        NextOp = NextOp->Asl.Next;  /* Get Address (DWORD) */

        LsEmitOffsetTableEntry (FileId, Node, NamepathOffset, DataOffset,
            Op->Asl.ParseOpName, NextOp->Asl.Value.Integer,
            (UINT8) AML_DWORD_OP, AML_PROCESSOR_OP);
        break;

    case AML_DEVICE_OP:
    case AML_SCOPE_OP:
    case AML_THERMAL_ZONE_OP:

        /* Device/Scope/ThermalZone (Namepath) */

        Length = Op->Asl.FinalAmlLength;
        NextOp = Op->Asl.Child;     /* Get Namepath */

        /* Get offset of last nameseg */

        NamepathOffset = AslGbl_CurrentAmlOffset + Length +
            (NextOp->Asl.FinalAmlLength - ACPI_NAME_SIZE);

        LsEmitOffsetTableEntry (FileId, Node, NamepathOffset, 0,
            Op->Asl.ParseOpName, 0, (UINT8) 0, Op->Asl.AmlOpcode);
        break;

    default:
        break;
    }

    AslGbl_CurrentAmlOffset += Op->Asl.FinalAmlLength;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    LsEmitOffsetTableEntry
 *
 * PARAMETERS:  FileId          - ID of current listing file
 *              Node            - Namespace node associated with the name
 *              Offset          - Offset of the value within the AML table
 *              OpName          - Name of the AML opcode
 *              Value           - Current value of the AML field
 *              AmlOpcode       - Opcode associated with the field
 *              ObjectType      - ACPI object type
 *
 * RETURN:      None
 *
 * DESCRIPTION: Emit a line of the offset table (-so option)
 *
 ******************************************************************************/

static void
LsEmitOffsetTableEntry (
    UINT32                  FileId,
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  NamepathOffset,
    UINT32                  Offset,
    char                    *OpName,
    UINT64                  Value,
    UINT8                   AmlOpcode,
    UINT16                  ParentOpcode)
{
    ACPI_BUFFER             TargetPath;
    ACPI_STATUS             Status;


    /* Get the full pathname to the namespace node */

    TargetPath.Length = ACPI_ALLOCATE_LOCAL_BUFFER;
    Status = AcpiNsHandleToPathname (Node, &TargetPath, FALSE);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* [1] - Skip the opening backslash for the path */

    strcpy (AslGbl_MsgBuffer, "\"");
    strcat (AslGbl_MsgBuffer, &((char *) TargetPath.Pointer)[1]);
    strcat (AslGbl_MsgBuffer, "\",");
    ACPI_FREE (TargetPath.Pointer);

    /*
     * Max offset is 4G, constrained by 32-bit ACPI table length.
     * Max Length for Integers is 8 bytes.
     */
    FlPrintFile (FileId,
        "    {%-29s 0x%4.4X, 0x%8.8X, 0x%2.2X, 0x%8.8X, 0x%8.8X%8.8X}, /* %s */\n",
        AslGbl_MsgBuffer, ParentOpcode, NamepathOffset, AmlOpcode,
        Offset, ACPI_FORMAT_UINT64 (Value), OpName);
}


/*******************************************************************************
 *
 * FUNCTION:    LsDoOffsetTableHeader, LsDoOffsetTableFooter
 *
 * PARAMETERS:  FileId          - ID of current listing file
 *
 * RETURN:      None
 *
 * DESCRIPTION: Header and footer for the offset table file.
 *
 ******************************************************************************/

void
LsDoOffsetTableHeader (
    UINT32                  FileId)
{

    FlPrintFile (FileId,
        "#ifndef __AML_OFFSET_TABLE_H\n"
        "#define __AML_OFFSET_TABLE_H\n\n");

    FlPrintFile (FileId, "typedef struct {\n"
        "    char                   *Pathname;      /* Full pathname (from root) to the object */\n"
        "    unsigned short         ParentOpcode;   /* AML opcode for the parent object */\n"
        "    unsigned long          NamesegOffset;  /* Offset of last nameseg in the parent namepath */\n"
        "    unsigned char          Opcode;         /* AML opcode for the data */\n"
        "    unsigned long          Offset;         /* Offset for the data */\n"
        "    unsigned long long     Value;          /* Original value of the data (as applicable) */\n"
        "} AML_OFFSET_TABLE_ENTRY;\n\n");

    FlPrintFile (FileId,
        "#endif /* __AML_OFFSET_TABLE_H */\n\n");

    FlPrintFile (FileId,
        "/*\n"
        " * Information specific to the supported object types:\n"
        " *\n"
        " * Integers:\n"
        " *    Opcode is the integer prefix, indicates length of the data\n"
        " *        (One of: BYTE, WORD, DWORD, QWORD, ZERO, ONE, ONES)\n"
        " *    Offset points to the actual integer data\n"
        " *    Value is the existing value in the AML\n"
        " *\n"
        " * Packages:\n"
        " *    Opcode is the package or var_package opcode\n"
        " *    Offset points to the package opcode\n"
        " *    Value is the package element count\n"
        " *\n"
        " * Operation Regions:\n"
        " *    Opcode is the address integer prefix, indicates length of the data\n"
        " *    Offset points to the region address\n"
        " *    Value is the existing address value in the AML\n"
        " *\n"
        " * Control Methods:\n"
        " *    Offset points to the method flags byte\n"
        " *    Value is the existing flags value in the AML\n"
        " *\n"
        " * Processors:\n"
        " *    Offset points to the first byte of the PBlock Address\n"
        " *\n"
        " * Resource Descriptors:\n"
        " *    Opcode is the descriptor type\n"
        " *    Offset points to the start of the descriptor\n"
        " *\n"
        " * Scopes/Devices/ThermalZones:\n"
        " *    Nameseg offset only\n"
        " */\n");

    FlPrintFile (FileId,
        "AML_OFFSET_TABLE_ENTRY   %s_%s_OffsetTable[] =\n{\n",
        AslGbl_TableSignature, AslGbl_TableId);
}


void
LsDoOffsetTableFooter (
    UINT32                  FileId)
{

    FlPrintFile (FileId,
        "    {NULL,0,0,0,0,0} /* Table terminator */\n};\n\n");
    AslGbl_CurrentAmlOffset = 0;
}
