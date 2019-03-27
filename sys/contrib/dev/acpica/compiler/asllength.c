/******************************************************************************
 *
 * Module Name: asllength - Tree walk to determine package and opcode lengths
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
#include <contrib/dev/acpica/include/acconvert.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("asllength")

/* Local prototypes */

static UINT8
CgGetPackageLenByteCount (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  PackageLength);

static void
CgGenerateAmlOpcodeLength (
    ACPI_PARSE_OBJECT       *Op);


#ifdef ACPI_OBSOLETE_FUNCTIONS
void
LnAdjustLengthToRoot (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  LengthDelta);
#endif


/*******************************************************************************
 *
 * FUNCTION:    LnInitLengthsWalk
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk callback to initialize (and re-initialize) the node
 *              subtree length(s) to zero. The Subtree lengths are bubbled
 *              up to the root node in order to get a total AML length.
 *
 ******************************************************************************/

ACPI_STATUS
LnInitLengthsWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{

    Op->Asl.AmlSubtreeLength = 0;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    LnPackageLengthWalk
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk callback to calculate the total AML length.
 *              1) Calculate the AML lengths (opcode, package length, etc.) for
 *                 THIS node.
 *              2) Bubbble up all of these lengths to the parent node by summing
 *                 them all into the parent subtree length.
 *
 * Note:  The SubtreeLength represents the total AML length of all child nodes
 *        in all subtrees under a given node. Therefore, once this walk is
 *        complete, the Root Node subtree length is the AML length of the entire
 *        tree (and thus, the entire ACPI table)
 *
 ******************************************************************************/

ACPI_STATUS
LnPackageLengthWalk (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{

    /* Generate the AML lengths for this node */

    CgGenerateAmlLengths (Op);

    /* Bubble up all lengths (this node and all below it) to the parent */

    if ((Op->Asl.Parent) &&
        (Op->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG))
    {
        Op->Asl.Parent->Asl.AmlSubtreeLength += (
            Op->Asl.AmlLength +
            Op->Asl.AmlOpcodeLength +
            Op->Asl.AmlPkgLenBytes +
            Op->Asl.AmlSubtreeLength +
            CvCalculateCommentLengths (Op)
        );
    }
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    CgGetPackageLenByteCount
 *
 * PARAMETERS:  Op              - Parse node
 *              PackageLength   - Length to be encoded
 *
 * RETURN:      Required length of the package length encoding
 *
 * DESCRIPTION: Calculate the number of bytes required to encode the given
 *              package length.
 *
 ******************************************************************************/

static UINT8
CgGetPackageLenByteCount (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  PackageLength)
{

    /*
     * Determine the number of bytes required to encode the package length
     * Note: the package length includes the number of bytes used to encode
     * the package length, so we must account for this also.
     */
    if (PackageLength <= (0x0000003F - 1))
    {
        return (1);
    }
    else if (PackageLength <= (0x00000FFF - 2))
    {
        return (2);
    }
    else if (PackageLength <= (0x000FFFFF - 3))
    {
        return (3);
    }
    else if (PackageLength <= (0x0FFFFFFF - 4))
    {
        return (4);
    }
    else
    {
        /* Fatal error - the package length is too large to encode */

        AslError (ASL_ERROR, ASL_MSG_ENCODING_LENGTH, Op, NULL);
    }

    return (0);
}


/*******************************************************************************
 *
 * FUNCTION:    CgGenerateAmlOpcodeLength
 *
 * PARAMETERS:  Op          - Parse node whose AML opcode lengths will be
 *                            calculated
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Calculate the AmlOpcodeLength, AmlPkgLenBytes, and AmlLength
 *              fields for this node.
 *
 ******************************************************************************/

static void
CgGenerateAmlOpcodeLength (
    ACPI_PARSE_OBJECT       *Op)
{

    /* Check for two-byte opcode */

    if (Op->Asl.AmlOpcode > 0x00FF)
    {
        Op->Asl.AmlOpcodeLength = 2;
    }
    else
    {
        Op->Asl.AmlOpcodeLength = 1;
    }

    /* Does this opcode have an associated "PackageLength" field? */

    Op->Asl.AmlPkgLenBytes = 0;
    if (Op->Asl.CompileFlags & OP_AML_PACKAGE)
    {
        Op->Asl.AmlPkgLenBytes = CgGetPackageLenByteCount (
            Op, Op->Asl.AmlSubtreeLength);
    }

    /* Data opcode lengths are easy */

    switch (Op->Asl.AmlOpcode)
    {
    case AML_BYTE_OP:

        Op->Asl.AmlLength = 1;
        break;

    case AML_WORD_OP:

        Op->Asl.AmlLength = 2;
        break;

    case AML_DWORD_OP:

        Op->Asl.AmlLength = 4;
        break;

    case AML_QWORD_OP:

        Op->Asl.AmlLength = 8;
        break;

    default:

        /* All data opcodes must be above */
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CgGenerateAmlLengths
 *
 * PARAMETERS:  Op        - Parse node
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Generate internal length fields based on the AML opcode or
 *              parse opcode.
 *
 ******************************************************************************/

void
CgGenerateAmlLengths (
    ACPI_PARSE_OBJECT       *Op)
{
    char                    *Buffer;
    ACPI_STATUS             Status;


    switch (Op->Asl.AmlOpcode)
    {
    case AML_RAW_DATA_BYTE:

        Op->Asl.AmlOpcodeLength = 0;
        Op->Asl.AmlLength = 1;
        return;

    case AML_RAW_DATA_WORD:

        Op->Asl.AmlOpcodeLength = 0;
        Op->Asl.AmlLength = 2;
        return;

    case AML_RAW_DATA_DWORD:

        Op->Asl.AmlOpcodeLength = 0;
        Op->Asl.AmlLength = 4;
        return;

    case AML_RAW_DATA_QWORD:

        Op->Asl.AmlOpcodeLength = 0;
        Op->Asl.AmlLength = 8;
        return;

    case AML_RAW_DATA_BUFFER:

        /* Aml length is/was set by creator */

        Op->Asl.AmlOpcodeLength = 0;
        return;

    case AML_RAW_DATA_CHAIN:

        /* Aml length is/was set by creator */

        Op->Asl.AmlOpcodeLength = 0;
        return;

    default:

        break;
    }

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_DEFINITION_BLOCK:

        AslGbl_TableLength = sizeof (ACPI_TABLE_HEADER) + Op->Asl.AmlSubtreeLength;
        break;

    case PARSEOP_NAMESEG:

        Op->Asl.AmlOpcodeLength = 0;
        Op->Asl.AmlLength = 4;
        Op->Asl.ExternalName = Op->Asl.Value.String;
        break;

    case PARSEOP_NAMESTRING:
    case PARSEOP_METHODCALL:

        if (Op->Asl.CompileFlags & OP_NAME_INTERNALIZED)
        {
            break;
        }

        Op->Asl.AmlOpcodeLength = 0;
        Status = UtInternalizeName (Op->Asl.Value.String, &Buffer);
        if (ACPI_FAILURE (Status))
        {
            DbgPrint (ASL_DEBUG_OUTPUT,
                "Failure from internalize name %X\n", Status);
            break;
        }

        Op->Asl.ExternalName = Op->Asl.Value.String;
        Op->Asl.Value.String = Buffer;
        Op->Asl.CompileFlags |= OP_NAME_INTERNALIZED;
        Op->Asl.AmlLength = strlen (Buffer);

        /*
         * Check for single backslash reference to root,
         * make it a null terminated string in the AML
         */
        if (Op->Asl.AmlLength == 1)
        {
            Op->Asl.AmlLength = 2;
        }
        break;

    case PARSEOP_STRING_LITERAL:

        Op->Asl.AmlOpcodeLength = 1;

        /* Get null terminator */

        Op->Asl.AmlLength = strlen (Op->Asl.Value.String) + 1;
        break;

    case PARSEOP_PACKAGE_LENGTH:

        Op->Asl.AmlOpcodeLength = 0;
        Op->Asl.AmlPkgLenBytes = CgGetPackageLenByteCount (Op,
            (UINT32) Op->Asl.Value.Integer);
        break;

    case PARSEOP_RAW_DATA:

        Op->Asl.AmlOpcodeLength = 0;
        break;

    case PARSEOP_DEFAULT_ARG:
    case PARSEOP_INCLUDE:
    case PARSEOP_INCLUDE_END:

        /* Ignore the "default arg" nodes, they are extraneous at this point */

        break;

    case PARSEOP_EXTERNAL:

        CgGenerateAmlOpcodeLength (Op);
        break;

    default:

        CgGenerateAmlOpcodeLength (Op);
        break;
    }
}


#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    LnAdjustLengthToRoot
 *
 * PARAMETERS:  Op      - Node whose Length was changed
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Change the Subtree length of the given node, and bubble the
 *              change all the way up to the root node. This allows for
 *              last second changes to a package length (for example, if the
 *              package length encoding gets shorter or longer.)
 *
 ******************************************************************************/

void
LnAdjustLengthToRoot (
    ACPI_PARSE_OBJECT       *SubtreeOp,
    UINT32                  LengthDelta)
{
    ACPI_PARSE_OBJECT       *Op;


    /* Adjust all subtree lengths up to the root */

    Op = SubtreeOp->Asl.Parent;
    while (Op)
    {
        Op->Asl.AmlSubtreeLength -= LengthDelta;
        Op = Op->Asl.Parent;
    }

    /* Adjust the global table length */

    AslGbl_TableLength -= LengthDelta;
}
#endif
