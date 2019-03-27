/******************************************************************************
 *
 * Module Name: dtexpress.c - Support for integer expressions and labels
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
#include "dtparser.y.h"

#define _COMPONENT          DT_COMPILER
        ACPI_MODULE_NAME    ("dtexpress")


/* Local prototypes */

static void
DtInsertLabelField (
    DT_FIELD                *Field);

static DT_FIELD *
DtLookupLabel (
    char                    *Name);

/* Global used for errors during parse and related functions */

DT_FIELD                *AslGbl_CurrentField;


/******************************************************************************
 *
 * FUNCTION:    DtResolveIntegerExpression
 *
 * PARAMETERS:  Field               - Field object with Integer expression
 *              ReturnValue         - Where the integer is returned
 *
 * RETURN:      Status, and the resolved 64-bit integer value
 *
 * DESCRIPTION: Resolve an integer expression to a single value. Supports
 *              both integer constants and labels.
 *
 *****************************************************************************/

ACPI_STATUS
DtResolveIntegerExpression (
    DT_FIELD                *Field,
    UINT64                  *ReturnValue)
{
    UINT64                  Result;


    DbgPrint (ASL_DEBUG_OUTPUT, "Full Integer expression: %s\n",
        Field->Value);

    AslGbl_CurrentField = Field;

    Result = DtEvaluateExpression (Field->Value);
    *ReturnValue = Result;
    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtDoOperator
 *
 * PARAMETERS:  LeftValue           - First 64-bit operand
 *              Operator            - Parse token for the operator (OP_EXP_*)
 *              RightValue          - Second 64-bit operand
 *
 * RETURN:      64-bit result of the requested operation
 *
 * DESCRIPTION: Perform the various 64-bit integer math functions
 *
 *****************************************************************************/

UINT64
DtDoOperator (
    UINT64                  LeftValue,
    UINT32                  Operator,
    UINT64                  RightValue)
{
    UINT64                  Result;


    /* Perform the requested operation */

    switch (Operator)
    {
    case OP_EXP_ONES_COMPLIMENT:

        Result = ~RightValue;
        break;

    case OP_EXP_LOGICAL_NOT:

        Result = !RightValue;
        break;

    case OP_EXP_MULTIPLY:

        Result = LeftValue * RightValue;
        break;

    case OP_EXP_DIVIDE:

        if (!RightValue)
        {
            DtError (ASL_ERROR, ASL_MSG_DIVIDE_BY_ZERO,
                AslGbl_CurrentField, NULL);
            return (0);
        }

        Result = LeftValue / RightValue;
        break;

    case OP_EXP_MODULO:

        if (!RightValue)
        {
            DtError (ASL_ERROR, ASL_MSG_DIVIDE_BY_ZERO,
                AslGbl_CurrentField, NULL);
            return (0);
        }

        Result = LeftValue % RightValue;
        break;

    case OP_EXP_ADD:
        Result = LeftValue + RightValue;
        break;

    case OP_EXP_SUBTRACT:

        Result = LeftValue - RightValue;
        break;

    case OP_EXP_SHIFT_RIGHT:

        Result = LeftValue >> RightValue;
        break;

    case OP_EXP_SHIFT_LEFT:

        Result = LeftValue << RightValue;
        break;

    case OP_EXP_LESS:

        Result = LeftValue < RightValue;
        break;

    case OP_EXP_GREATER:

        Result = LeftValue > RightValue;
        break;

    case OP_EXP_LESS_EQUAL:

        Result = LeftValue <= RightValue;
        break;

    case OP_EXP_GREATER_EQUAL:

        Result = LeftValue >= RightValue;
        break;

    case OP_EXP_EQUAL:

        Result = LeftValue == RightValue;
        break;

    case OP_EXP_NOT_EQUAL:

        Result = LeftValue != RightValue;
        break;

    case OP_EXP_AND:

        Result = LeftValue & RightValue;
        break;

    case OP_EXP_XOR:

        Result = LeftValue ^ RightValue;
        break;

    case OP_EXP_OR:

        Result = LeftValue | RightValue;
        break;

    case OP_EXP_LOGICAL_AND:

        Result = LeftValue && RightValue;
        break;

    case OP_EXP_LOGICAL_OR:

        Result = LeftValue || RightValue;
        break;

   default:

        /* Unknown operator */

        DtFatal (ASL_MSG_INVALID_EXPRESSION,
            AslGbl_CurrentField, NULL);
        return (0);
    }

    DbgPrint (ASL_DEBUG_OUTPUT,
        "IntegerEval: (%8.8X%8.8X %s %8.8X%8.8X) = %8.8X%8.8X\n",
        ACPI_FORMAT_UINT64 (LeftValue),
        DtGetOpName (Operator),
        ACPI_FORMAT_UINT64 (RightValue),
        ACPI_FORMAT_UINT64 (Result));

    return (Result);
}


/******************************************************************************
 *
 * FUNCTION:    DtResolveLabel
 *
 * PARAMETERS:  LabelString         - Contains the label
 *
 * RETURN:      Table offset associated with the label
 *
 * DESCRIPTION: Lookup a label and return its value.
 *
 *****************************************************************************/

UINT64
DtResolveLabel (
    char                    *LabelString)
{
    DT_FIELD                *LabelField;


    DbgPrint (ASL_DEBUG_OUTPUT, "Resolve Label: %s\n", LabelString);

    /* Resolve a label reference to an integer (table offset) */

    if (*LabelString != '$')
    {
        return (0);
    }

    LabelField = DtLookupLabel (LabelString);
    if (!LabelField)
    {
        DtError (ASL_ERROR, ASL_MSG_UNKNOWN_LABEL,
            AslGbl_CurrentField, LabelString);
        return (0);
    }

    /* All we need from the label is the offset in the table */

    DbgPrint (ASL_DEBUG_OUTPUT, "Resolved Label: 0x%8.8X\n",
        LabelField->TableOffset);

    return (LabelField->TableOffset);
}


/******************************************************************************
 *
 * FUNCTION:    DtDetectAllLabels
 *
 * PARAMETERS:  FieldList           - Field object at start of generic list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Detect all labels in a list of "generic" opcodes (such as
 *              a UEFI table.) and insert them into the global label list.
 *
 *****************************************************************************/

void
DtDetectAllLabels (
    DT_FIELD                *FieldList)
{
    ACPI_DMTABLE_INFO       *Info;
    DT_FIELD                *GenericField;
    UINT32                  TableOffset;


    TableOffset = AslGbl_CurrentTableOffset;
    GenericField = FieldList;

    /*
     * Process all "Label:" fields within the parse tree. We need
     * to know the offsets for all labels before we can compile
     * the parse tree in order to handle forward references. Traverse
     * tree and get/set all field lengths of all operators in order to
     * determine the label offsets.
     */
    while (GenericField)
    {
        Info = DtGetGenericTableInfo (GenericField->Name);
        if (Info)
        {
            /* Maintain table offsets */

            GenericField->TableOffset = TableOffset;
            TableOffset += DtGetFieldLength (GenericField, Info);

            /* Insert all labels in the global label list */

            if (Info->Opcode == ACPI_DMT_LABEL)
            {
                DtInsertLabelField (GenericField);
            }
        }

        GenericField = GenericField->Next;
    }
}


/******************************************************************************
 *
 * FUNCTION:    DtInsertLabelField
 *
 * PARAMETERS:  Field               - Field object with Label to be inserted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Insert a label field into the global label list
 *
 *****************************************************************************/

static void
DtInsertLabelField (
    DT_FIELD                *Field)
{

    DbgPrint (ASL_DEBUG_OUTPUT,
        "DtInsertLabelField: Found Label : %s at output table offset %X\n",
        Field->Value, Field->TableOffset);

    Field->NextLabel = AslGbl_LabelList;
    AslGbl_LabelList = Field;
}


/******************************************************************************
 *
 * FUNCTION:    DtLookupLabel
 *
 * PARAMETERS:  Name                - Label to be resolved
 *
 * RETURN:      Field object associated with the label
 *
 * DESCRIPTION: Lookup a label in the global label list. Used during the
 *              resolution of integer expressions.
 *
 *****************************************************************************/

static DT_FIELD *
DtLookupLabel (
    char                    *Name)
{
    DT_FIELD                *LabelField;


    /* Skip a leading $ */

    if (*Name == '$')
    {
        Name++;
    }

    /* Search global list */

    LabelField = AslGbl_LabelList;
    while (LabelField)
    {
        if (!strcmp (Name, LabelField->Value))
        {
            return (LabelField);
        }

        LabelField = LabelField->NextLabel;
    }

    return (NULL);
}
