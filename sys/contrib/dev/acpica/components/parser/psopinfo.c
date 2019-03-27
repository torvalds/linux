/******************************************************************************
 *
 * Module Name: psopinfo - AML opcode information functions and dispatch tables
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
#include <contrib/dev/acpica/include/acopcode.h>
#include <contrib/dev/acpica/include/amlcode.h>


#define _COMPONENT          ACPI_PARSER
        ACPI_MODULE_NAME    ("psopinfo")


static const UINT8      AcpiGbl_ArgumentCount[] = {0,1,1,1,1,2,2,2,2,3,3,6};


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsGetOpcodeInfo
 *
 * PARAMETERS:  Opcode              - The AML opcode
 *
 * RETURN:      A pointer to the info about the opcode.
 *
 * DESCRIPTION: Find AML opcode description based on the opcode.
 *              NOTE: This procedure must ALWAYS return a valid pointer!
 *
 ******************************************************************************/

const ACPI_OPCODE_INFO *
AcpiPsGetOpcodeInfo (
    UINT16                  Opcode)
{
#ifdef ACPI_DEBUG_OUTPUT
    const char              *OpcodeName = "Unknown AML opcode";
#endif

    ACPI_FUNCTION_NAME (PsGetOpcodeInfo);


    /*
     * Detect normal 8-bit opcode or extended 16-bit opcode
     */
    if (!(Opcode & 0xFF00))
    {
        /* Simple (8-bit) opcode: 0-255, can't index beyond table  */

        return (&AcpiGbl_AmlOpInfo [AcpiGbl_ShortOpIndex [(UINT8) Opcode]]);
    }

    if (((Opcode & 0xFF00) == AML_EXTENDED_OPCODE) &&
        (((UINT8) Opcode) <= MAX_EXTENDED_OPCODE))
    {
        /* Valid extended (16-bit) opcode */

        return (&AcpiGbl_AmlOpInfo [AcpiGbl_LongOpIndex [(UINT8) Opcode]]);
    }

#if defined ACPI_ASL_COMPILER && defined ACPI_DEBUG_OUTPUT
#include <contrib/dev/acpica/compiler/asldefine.h>

    switch (Opcode)
    {
    case AML_RAW_DATA_BYTE:
        OpcodeName = "-Raw Data Byte-";
        break;

    case AML_RAW_DATA_WORD:
        OpcodeName = "-Raw Data Word-";
        break;

    case AML_RAW_DATA_DWORD:
        OpcodeName = "-Raw Data Dword-";
        break;

    case AML_RAW_DATA_QWORD:
        OpcodeName = "-Raw Data Qword-";
        break;

    case AML_RAW_DATA_BUFFER:
        OpcodeName = "-Raw Data Buffer-";
        break;

    case AML_RAW_DATA_CHAIN:
        OpcodeName = "-Raw Data Buffer Chain-";
        break;

    case AML_PACKAGE_LENGTH:
        OpcodeName = "-Package Length-";
        break;

    case AML_UNASSIGNED_OPCODE:
        OpcodeName = "-Unassigned Opcode-";
        break;

    case AML_DEFAULT_ARG_OP:
        OpcodeName = "-Default Arg-";
        break;

    default:
        break;
    }
#endif

    /* Unknown AML opcode */

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
        "%s [%4.4X]\n", OpcodeName, Opcode));

    return (&AcpiGbl_AmlOpInfo [_UNK]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsGetOpcodeName
 *
 * PARAMETERS:  Opcode              - The AML opcode
 *
 * RETURN:      A pointer to the name of the opcode (ASCII String)
 *              Note: Never returns NULL.
 *
 * DESCRIPTION: Translate an opcode into a human-readable string
 *
 ******************************************************************************/

const char *
AcpiPsGetOpcodeName (
    UINT16                  Opcode)
{
#if defined(ACPI_DISASSEMBLER) || defined (ACPI_DEBUG_OUTPUT)

    const ACPI_OPCODE_INFO  *Op;


    Op = AcpiPsGetOpcodeInfo (Opcode);

    /* Always guaranteed to return a valid pointer */

    return (Op->Name);

#else
    return ("OpcodeName unavailable");

#endif
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsGetArgumentCount
 *
 * PARAMETERS:  OpType              - Type associated with the AML opcode
 *
 * RETURN:      Argument count
 *
 * DESCRIPTION: Obtain the number of expected arguments for an AML opcode
 *
 ******************************************************************************/

UINT8
AcpiPsGetArgumentCount (
    UINT32                  OpType)
{

    if (OpType <= AML_TYPE_EXEC_6A_0T_1R)
    {
        return (AcpiGbl_ArgumentCount[OpType]);
    }

    return (0);
}


/*
 * This table is directly indexed by the opcodes It returns
 * an index into the opcode table (AcpiGbl_AmlOpInfo)
 */
const UINT8 AcpiGbl_ShortOpIndex[256] =
{
/*              0     1     2     3     4     5     6     7  */
/*              8     9     A     B     C     D     E     F  */
/* 0x00 */    0x00, 0x01, _UNK, _UNK, _UNK, _UNK, 0x02, _UNK,
/* 0x08 */    0x03, _UNK, 0x04, 0x05, 0x06, 0x07, 0x6E, _UNK,
/* 0x10 */    0x08, 0x09, 0x0a, 0x6F, 0x0b, 0x81, _UNK, _UNK,
/* 0x18 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x20 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x28 */    _UNK, _UNK, _UNK, _UNK, _UNK, 0x63, _PFX, _PFX,
/* 0x30 */    0x67, 0x66, 0x68, 0x65, 0x69, 0x64, 0x6A, 0x7D,
/* 0x38 */    0x7F, 0x80, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x40 */    _UNK, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC,
/* 0x48 */    _ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC,
/* 0x50 */    _ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC, _ASC,
/* 0x58 */    _ASC, _ASC, _ASC, _UNK, _PFX, _UNK, _PFX, _ASC,
/* 0x60 */    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
/* 0x68 */    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, _UNK,
/* 0x70 */    0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22,
/* 0x78 */    0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
/* 0x80 */    0x2b, 0x2c, 0x2d, 0x2e, 0x70, 0x71, 0x2f, 0x30,
/* 0x88 */    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x72,
/* 0x90 */    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x73, 0x74,
/* 0x98 */    0x75, 0x76, _UNK, _UNK, 0x77, 0x78, 0x79, 0x7A,
/* 0xA0 */    0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x60, 0x61,
/* 0xA8 */    0x62, 0x82, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xB0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xB8 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xC0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xC8 */    _UNK, _UNK, _UNK, _UNK, 0x44, _UNK, _UNK, _UNK,
/* 0xD0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xD8 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xE0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xE8 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xF0 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0xF8 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, 0x45,
};

/*
 * This table is indexed by the second opcode of the extended opcode
 * pair. It returns an index into the opcode table (AcpiGbl_AmlOpInfo)
 */
const UINT8 AcpiGbl_LongOpIndex[NUM_EXTENDED_OPCODE] =
{
/*              0     1     2     3     4     5     6     7  */
/*              8     9     A     B     C     D     E     F  */
/* 0x00 */    _UNK, 0x46, 0x47, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x08 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x10 */    _UNK, _UNK, 0x48, 0x49, _UNK, _UNK, _UNK, _UNK,
/* 0x18 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, 0x7B,
/* 0x20 */    0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51,
/* 0x28 */    0x52, 0x53, 0x54, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x30 */    0x55, 0x56, 0x57, 0x7e, _UNK, _UNK, _UNK, _UNK,
/* 0x38 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x40 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x48 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x50 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x58 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x60 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x68 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x70 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x78 */    _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK, _UNK,
/* 0x80 */    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
/* 0x88 */    0x7C,
};
