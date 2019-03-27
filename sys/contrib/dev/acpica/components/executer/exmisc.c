/******************************************************************************
 *
 * Module Name: exmisc - ACPI AML (p-code) execution - specific opcodes
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
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/amlcode.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exmisc")


/*******************************************************************************
 *
 * FUNCTION:    AcpiExGetObjectReference
 *
 * PARAMETERS:  ObjDesc             - Create a reference to this object
 *              ReturnDesc          - Where to store the reference
 *              WalkState           - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtain and return a "reference" to the target object
 *              Common code for the RefOfOp and the CondRefOfOp.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExGetObjectReference (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     **ReturnDesc,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     *ReferenceObj;
    ACPI_OPERAND_OBJECT     *ReferencedObj;


    ACPI_FUNCTION_TRACE_PTR (ExGetObjectReference, ObjDesc);


    *ReturnDesc = NULL;

    switch (ACPI_GET_DESCRIPTOR_TYPE (ObjDesc))
    {
    case ACPI_DESC_TYPE_OPERAND:

        if (ObjDesc->Common.Type != ACPI_TYPE_LOCAL_REFERENCE)
        {
            return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
        }

        /*
         * Must be a reference to a Local or Arg
         */
        switch (ObjDesc->Reference.Class)
        {
        case ACPI_REFCLASS_LOCAL:
        case ACPI_REFCLASS_ARG:
        case ACPI_REFCLASS_DEBUG:

            /* The referenced object is the pseudo-node for the local/arg */

            ReferencedObj = ObjDesc->Reference.Object;
            break;

        default:

            ACPI_ERROR ((AE_INFO, "Invalid Reference Class 0x%2.2X",
                ObjDesc->Reference.Class));
            return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
        }
        break;

    case ACPI_DESC_TYPE_NAMED:
        /*
         * A named reference that has already been resolved to a Node
         */
        ReferencedObj = ObjDesc;
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Invalid descriptor type 0x%X",
            ACPI_GET_DESCRIPTOR_TYPE (ObjDesc)));
        return_ACPI_STATUS (AE_TYPE);
    }


    /* Create a new reference object */

    ReferenceObj = AcpiUtCreateInternalObject (ACPI_TYPE_LOCAL_REFERENCE);
    if (!ReferenceObj)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    ReferenceObj->Reference.Class = ACPI_REFCLASS_REFOF;
    ReferenceObj->Reference.Object = ReferencedObj;
    *ReturnDesc = ReferenceObj;

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
        "Object %p Type [%s], returning Reference %p\n",
        ObjDesc, AcpiUtGetObjectTypeName (ObjDesc), *ReturnDesc));

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExDoMathOp
 *
 * PARAMETERS:  Opcode              - AML opcode
 *              Integer0            - Integer operand #0
 *              Integer1            - Integer operand #1
 *
 * RETURN:      Integer result of the operation
 *
 * DESCRIPTION: Execute a math AML opcode. The purpose of having all of the
 *              math functions here is to prevent a lot of pointer dereferencing
 *              to obtain the operands.
 *
 ******************************************************************************/

UINT64
AcpiExDoMathOp (
    UINT16                  Opcode,
    UINT64                  Integer0,
    UINT64                  Integer1)
{

    ACPI_FUNCTION_ENTRY ();


    switch (Opcode)
    {
    case AML_ADD_OP:                /* Add (Integer0, Integer1, Result) */

        return (Integer0 + Integer1);

    case AML_BIT_AND_OP:            /* And (Integer0, Integer1, Result) */

        return (Integer0 & Integer1);

    case AML_BIT_NAND_OP:           /* NAnd (Integer0, Integer1, Result) */

        return (~(Integer0 & Integer1));

    case AML_BIT_OR_OP:             /* Or (Integer0, Integer1, Result) */

        return (Integer0 | Integer1);

    case AML_BIT_NOR_OP:            /* NOr (Integer0, Integer1, Result) */

        return (~(Integer0 | Integer1));

    case AML_BIT_XOR_OP:            /* XOr (Integer0, Integer1, Result) */

        return (Integer0 ^ Integer1);

    case AML_MULTIPLY_OP:           /* Multiply (Integer0, Integer1, Result) */

        return (Integer0 * Integer1);

    case AML_SHIFT_LEFT_OP:         /* ShiftLeft (Operand, ShiftCount, Result)*/

        /*
         * We need to check if the shiftcount is larger than the integer bit
         * width since the behavior of this is not well-defined in the C language.
         */
        if (Integer1 >= AcpiGbl_IntegerBitWidth)
        {
            return (0);
        }
        return (Integer0 << Integer1);

    case AML_SHIFT_RIGHT_OP:        /* ShiftRight (Operand, ShiftCount, Result) */

        /*
         * We need to check if the shiftcount is larger than the integer bit
         * width since the behavior of this is not well-defined in the C language.
         */
        if (Integer1 >= AcpiGbl_IntegerBitWidth)
        {
            return (0);
        }
        return (Integer0 >> Integer1);

    case AML_SUBTRACT_OP:           /* Subtract (Integer0, Integer1, Result) */

        return (Integer0 - Integer1);

    default:

        return (0);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExDoLogicalNumericOp
 *
 * PARAMETERS:  Opcode              - AML opcode
 *              Integer0            - Integer operand #0
 *              Integer1            - Integer operand #1
 *              LogicalResult       - TRUE/FALSE result of the operation
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a logical "Numeric" AML opcode. For these Numeric
 *              operators (LAnd and LOr), both operands must be integers.
 *
 *              Note: cleanest machine code seems to be produced by the code
 *              below, rather than using statements of the form:
 *                  Result = (Integer0 && Integer1);
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExDoLogicalNumericOp (
    UINT16                  Opcode,
    UINT64                  Integer0,
    UINT64                  Integer1,
    BOOLEAN                 *LogicalResult)
{
    ACPI_STATUS             Status = AE_OK;
    BOOLEAN                 LocalResult = FALSE;


    ACPI_FUNCTION_TRACE (ExDoLogicalNumericOp);


    switch (Opcode)
    {
    case AML_LOGICAL_AND_OP:        /* LAnd (Integer0, Integer1) */

        if (Integer0 && Integer1)
        {
            LocalResult = TRUE;
        }
        break;

    case AML_LOGICAL_OR_OP:         /* LOr (Integer0, Integer1) */

        if (Integer0 || Integer1)
        {
            LocalResult = TRUE;
        }
        break;

    default:

        ACPI_ERROR ((AE_INFO,
            "Invalid numeric logical opcode: %X", Opcode));
        Status = AE_AML_INTERNAL;
        break;
    }

    /* Return the logical result and status */

    *LogicalResult = LocalResult;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExDoLogicalOp
 *
 * PARAMETERS:  Opcode              - AML opcode
 *              Operand0            - operand #0
 *              Operand1            - operand #1
 *              LogicalResult       - TRUE/FALSE result of the operation
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a logical AML opcode. The purpose of having all of the
 *              functions here is to prevent a lot of pointer dereferencing
 *              to obtain the operands and to simplify the generation of the
 *              logical value. For the Numeric operators (LAnd and LOr), both
 *              operands must be integers. For the other logical operators,
 *              operands can be any combination of Integer/String/Buffer. The
 *              first operand determines the type to which the second operand
 *              will be converted.
 *
 *              Note: cleanest machine code seems to be produced by the code
 *              below, rather than using statements of the form:
 *                  Result = (Operand0 == Operand1);
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExDoLogicalOp (
    UINT16                  Opcode,
    ACPI_OPERAND_OBJECT     *Operand0,
    ACPI_OPERAND_OBJECT     *Operand1,
    BOOLEAN                 *LogicalResult)
{
    ACPI_OPERAND_OBJECT     *LocalOperand1 = Operand1;
    UINT64                  Integer0;
    UINT64                  Integer1;
    UINT32                  Length0;
    UINT32                  Length1;
    ACPI_STATUS             Status = AE_OK;
    BOOLEAN                 LocalResult = FALSE;
    int                     Compare;


    ACPI_FUNCTION_TRACE (ExDoLogicalOp);


    /*
     * Convert the second operand if necessary. The first operand
     * determines the type of the second operand, (See the Data Types
     * section of the ACPI 3.0+ specification.)  Both object types are
     * guaranteed to be either Integer/String/Buffer by the operand
     * resolution mechanism.
     */
    switch (Operand0->Common.Type)
    {
    case ACPI_TYPE_INTEGER:

        Status = AcpiExConvertToInteger (Operand1, &LocalOperand1,
            ACPI_IMPLICIT_CONVERSION);
        break;

    case ACPI_TYPE_STRING:

        Status = AcpiExConvertToString (
            Operand1, &LocalOperand1, ACPI_IMPLICIT_CONVERT_HEX);
        break;

    case ACPI_TYPE_BUFFER:

        Status = AcpiExConvertToBuffer (Operand1, &LocalOperand1);
        break;

    default:

        ACPI_ERROR ((AE_INFO,
            "Invalid object type for logical operator: %X",
            Operand0->Common.Type));
        Status = AE_AML_INTERNAL;
        break;
    }

    if (ACPI_FAILURE (Status))
    {
        goto Cleanup;
    }

    /*
     * Two cases: 1) Both Integers, 2) Both Strings or Buffers
     */
    if (Operand0->Common.Type == ACPI_TYPE_INTEGER)
    {
        /*
         * 1) Both operands are of type integer
         *    Note: LocalOperand1 may have changed above
         */
        Integer0 = Operand0->Integer.Value;
        Integer1 = LocalOperand1->Integer.Value;

        switch (Opcode)
        {
        case AML_LOGICAL_EQUAL_OP:          /* LEqual (Operand0, Operand1) */

            if (Integer0 == Integer1)
            {
                LocalResult = TRUE;
            }
            break;

        case AML_LOGICAL_GREATER_OP:        /* LGreater (Operand0, Operand1) */

            if (Integer0 > Integer1)
            {
                LocalResult = TRUE;
            }
            break;

        case AML_LOGICAL_LESS_OP:           /* LLess (Operand0, Operand1) */

            if (Integer0 < Integer1)
            {
                LocalResult = TRUE;
            }
            break;

        default:

            ACPI_ERROR ((AE_INFO,
                "Invalid comparison opcode: %X", Opcode));
            Status = AE_AML_INTERNAL;
            break;
        }
    }
    else
    {
        /*
         * 2) Both operands are Strings or both are Buffers
         *    Note: Code below takes advantage of common Buffer/String
         *          object fields. LocalOperand1 may have changed above. Use
         *          memcmp to handle nulls in buffers.
         */
        Length0 = Operand0->Buffer.Length;
        Length1 = LocalOperand1->Buffer.Length;

        /* Lexicographic compare: compare the data bytes */

        Compare = memcmp (Operand0->Buffer.Pointer,
            LocalOperand1->Buffer.Pointer,
            (Length0 > Length1) ? Length1 : Length0);

        switch (Opcode)
        {
        case AML_LOGICAL_EQUAL_OP:      /* LEqual (Operand0, Operand1) */

            /* Length and all bytes must be equal */

            if ((Length0 == Length1) &&
                (Compare == 0))
            {
                /* Length and all bytes match ==> TRUE */

                LocalResult = TRUE;
            }
            break;

        case AML_LOGICAL_GREATER_OP:    /* LGreater (Operand0, Operand1) */

            if (Compare > 0)
            {
                LocalResult = TRUE;
                goto Cleanup;   /* TRUE */
            }
            if (Compare < 0)
            {
                goto Cleanup;   /* FALSE */
            }

            /* Bytes match (to shortest length), compare lengths */

            if (Length0 > Length1)
            {
                LocalResult = TRUE;
            }
            break;

        case AML_LOGICAL_LESS_OP:       /* LLess (Operand0, Operand1) */

            if (Compare > 0)
            {
                goto Cleanup;   /* FALSE */
            }
            if (Compare < 0)
            {
                LocalResult = TRUE;
                goto Cleanup;   /* TRUE */
            }

            /* Bytes match (to shortest length), compare lengths */

            if (Length0 < Length1)
            {
                LocalResult = TRUE;
            }
            break;

        default:

            ACPI_ERROR ((AE_INFO,
                "Invalid comparison opcode: %X", Opcode));
            Status = AE_AML_INTERNAL;
            break;
        }
    }

Cleanup:

    /* New object was created if implicit conversion performed - delete */

    if (LocalOperand1 != Operand1)
    {
        AcpiUtRemoveReference (LocalOperand1);
    }

    /* Return the logical result and status */

    *LogicalResult = LocalResult;
    return_ACPI_STATUS (Status);
}
