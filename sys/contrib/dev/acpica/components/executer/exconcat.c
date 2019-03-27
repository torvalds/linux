/******************************************************************************
 *
 * Module Name: exconcat - Concatenate-type AML operators
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
#include <contrib/dev/acpica/include/amlresrc.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exconcat")

/* Local Prototypes */

static ACPI_STATUS
AcpiExConvertToObjectTypeString (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     **ResultDesc);


/*******************************************************************************
 *
 * FUNCTION:    AcpiExDoConcatenate
 *
 * PARAMETERS:  Operand0            - First source object
 *              Operand1            - Second source object
 *              ActualReturnDesc    - Where to place the return object
 *              WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Concatenate two objects with the ACPI-defined conversion
 *              rules as necessary.
 * NOTE:
 * Per the ACPI spec (up to 6.1), Concatenate only supports Integer,
 * String, and Buffer objects. However, we support all objects here
 * as an extension. This improves the usefulness of both Concatenate
 * and the Printf/Fprintf macros. The extension returns a string
 * describing the object type for the other objects.
 * 02/2016.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExDoConcatenate (
    ACPI_OPERAND_OBJECT     *Operand0,
    ACPI_OPERAND_OBJECT     *Operand1,
    ACPI_OPERAND_OBJECT     **ActualReturnDesc,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     *LocalOperand0 = Operand0;
    ACPI_OPERAND_OBJECT     *LocalOperand1 = Operand1;
    ACPI_OPERAND_OBJECT     *TempOperand1 = NULL;
    ACPI_OPERAND_OBJECT     *ReturnDesc;
    char                    *Buffer;
    ACPI_OBJECT_TYPE        Operand0Type;
    ACPI_OBJECT_TYPE        Operand1Type;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (ExDoConcatenate);


    /* Operand 0 preprocessing */

    switch (Operand0->Common.Type)
    {
    case ACPI_TYPE_INTEGER:
    case ACPI_TYPE_STRING:
    case ACPI_TYPE_BUFFER:

        Operand0Type = Operand0->Common.Type;
        break;

    default:

        /* For all other types, get the "object type" string */

        Status = AcpiExConvertToObjectTypeString (
            Operand0, &LocalOperand0);
        if (ACPI_FAILURE (Status))
        {
            goto Cleanup;
        }

        Operand0Type = ACPI_TYPE_STRING;
        break;
    }

    /* Operand 1 preprocessing */

    switch (Operand1->Common.Type)
    {
    case ACPI_TYPE_INTEGER:
    case ACPI_TYPE_STRING:
    case ACPI_TYPE_BUFFER:

        Operand1Type = Operand1->Common.Type;
        break;

    default:

        /* For all other types, get the "object type" string */

        Status = AcpiExConvertToObjectTypeString (
            Operand1, &LocalOperand1);
        if (ACPI_FAILURE (Status))
        {
            goto Cleanup;
        }

        Operand1Type = ACPI_TYPE_STRING;
        break;
    }

    /*
     * Convert the second operand if necessary. The first operand (0)
     * determines the type of the second operand (1) (See the Data Types
     * section of the ACPI specification). Both object types are
     * guaranteed to be either Integer/String/Buffer by the operand
     * resolution mechanism.
     */
    switch (Operand0Type)
    {
    case ACPI_TYPE_INTEGER:

        Status = AcpiExConvertToInteger (LocalOperand1, &TempOperand1,
            ACPI_IMPLICIT_CONVERSION);
        break;

    case ACPI_TYPE_BUFFER:

        Status = AcpiExConvertToBuffer (LocalOperand1, &TempOperand1);
        break;

    case ACPI_TYPE_STRING:

        switch (Operand1Type)
        {
        case ACPI_TYPE_INTEGER:
        case ACPI_TYPE_STRING:
        case ACPI_TYPE_BUFFER:

            /* Other types have already been converted to string */

            Status = AcpiExConvertToString (
                LocalOperand1, &TempOperand1, ACPI_IMPLICIT_CONVERT_HEX);
            break;

        default:

            Status = AE_OK;
            break;
        }
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Invalid object type: 0x%X",
            Operand0->Common.Type));
        Status = AE_AML_INTERNAL;
    }

    if (ACPI_FAILURE (Status))
    {
        goto Cleanup;
    }

    /* Take care with any newly created operand objects */

    if ((LocalOperand1 != Operand1) &&
        (LocalOperand1 != TempOperand1))
    {
        AcpiUtRemoveReference (LocalOperand1);
    }

    LocalOperand1 = TempOperand1;

    /*
     * Both operands are now known to be the same object type
     * (Both are Integer, String, or Buffer), and we can now perform
     * the concatenation.
     *
     * There are three cases to handle, as per the ACPI spec:
     *
     * 1) Two Integers concatenated to produce a new Buffer
     * 2) Two Strings concatenated to produce a new String
     * 3) Two Buffers concatenated to produce a new Buffer
     */
    switch (Operand0Type)
    {
    case ACPI_TYPE_INTEGER:

        /* Result of two Integers is a Buffer */
        /* Need enough buffer space for two integers */

        ReturnDesc = AcpiUtCreateBufferObject (
            (ACPI_SIZE) ACPI_MUL_2 (AcpiGbl_IntegerByteWidth));
        if (!ReturnDesc)
        {
            Status = AE_NO_MEMORY;
            goto Cleanup;
        }

        Buffer = (char *) ReturnDesc->Buffer.Pointer;

        /* Copy the first integer, LSB first */

        memcpy (Buffer, &Operand0->Integer.Value,
            AcpiGbl_IntegerByteWidth);

        /* Copy the second integer (LSB first) after the first */

        memcpy (Buffer + AcpiGbl_IntegerByteWidth,
            &LocalOperand1->Integer.Value, AcpiGbl_IntegerByteWidth);
        break;

    case ACPI_TYPE_STRING:

        /* Result of two Strings is a String */

        ReturnDesc = AcpiUtCreateStringObject (
            ((ACPI_SIZE) LocalOperand0->String.Length +
            LocalOperand1->String.Length));
        if (!ReturnDesc)
        {
            Status = AE_NO_MEMORY;
            goto Cleanup;
        }

        Buffer = ReturnDesc->String.Pointer;

        /* Concatenate the strings */

        strcpy (Buffer, LocalOperand0->String.Pointer);
        strcat (Buffer, LocalOperand1->String.Pointer);
        break;

    case ACPI_TYPE_BUFFER:

        /* Result of two Buffers is a Buffer */

        ReturnDesc = AcpiUtCreateBufferObject (
            ((ACPI_SIZE) Operand0->Buffer.Length +
            LocalOperand1->Buffer.Length));
        if (!ReturnDesc)
        {
            Status = AE_NO_MEMORY;
            goto Cleanup;
        }

        Buffer = (char *) ReturnDesc->Buffer.Pointer;

        /* Concatenate the buffers */

        memcpy (Buffer, Operand0->Buffer.Pointer,
            Operand0->Buffer.Length);
        memcpy (Buffer + Operand0->Buffer.Length,
            LocalOperand1->Buffer.Pointer,
            LocalOperand1->Buffer.Length);
        break;

    default:

        /* Invalid object type, should not happen here */

        ACPI_ERROR ((AE_INFO, "Invalid object type: 0x%X",
            Operand0->Common.Type));
        Status = AE_AML_INTERNAL;
        goto Cleanup;
    }

    *ActualReturnDesc = ReturnDesc;

Cleanup:
    if (LocalOperand0 != Operand0)
    {
        AcpiUtRemoveReference (LocalOperand0);
    }

    if (LocalOperand1 != Operand1)
    {
        AcpiUtRemoveReference (LocalOperand1);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExConvertToObjectTypeString
 *
 * PARAMETERS:  ObjDesc             - Object to be converted
 *              ReturnDesc          - Where to place the return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an object of arbitrary type to a string object that
 *              contains the namestring for the object. Used for the
 *              concatenate operator.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiExConvertToObjectTypeString (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     **ResultDesc)
{
    ACPI_OPERAND_OBJECT     *ReturnDesc;
    const char              *TypeString;


    TypeString = AcpiUtGetTypeName (ObjDesc->Common.Type);

    ReturnDesc = AcpiUtCreateStringObject (
        ((ACPI_SIZE) strlen (TypeString) + 9)); /* 9 For "[ Object]" */
    if (!ReturnDesc)
    {
        return (AE_NO_MEMORY);
    }

    strcpy (ReturnDesc->String.Pointer, "[");
    strcat (ReturnDesc->String.Pointer, TypeString);
    strcat (ReturnDesc->String.Pointer, " Object]");

    *ResultDesc = ReturnDesc;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExConcatTemplate
 *
 * PARAMETERS:  Operand0            - First source object
 *              Operand1            - Second source object
 *              ActualReturnDesc    - Where to place the return object
 *              WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Concatenate two resource templates
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExConcatTemplate (
    ACPI_OPERAND_OBJECT     *Operand0,
    ACPI_OPERAND_OBJECT     *Operand1,
    ACPI_OPERAND_OBJECT     **ActualReturnDesc,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ReturnDesc;
    UINT8                   *NewBuf;
    UINT8                   *EndTag;
    ACPI_SIZE               Length0;
    ACPI_SIZE               Length1;
    ACPI_SIZE               NewLength;


    ACPI_FUNCTION_TRACE (ExConcatTemplate);


    /*
     * Find the EndTag descriptor in each resource template.
     * Note1: returned pointers point TO the EndTag, not past it.
     * Note2: zero-length buffers are allowed; treated like one EndTag
     */

    /* Get the length of the first resource template */

    Status = AcpiUtGetResourceEndTag (Operand0, &EndTag);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    Length0 = ACPI_PTR_DIFF (EndTag, Operand0->Buffer.Pointer);

    /* Get the length of the second resource template */

    Status = AcpiUtGetResourceEndTag (Operand1, &EndTag);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    Length1 = ACPI_PTR_DIFF (EndTag, Operand1->Buffer.Pointer);

    /* Combine both lengths, minimum size will be 2 for EndTag */

    NewLength = Length0 + Length1 + sizeof (AML_RESOURCE_END_TAG);

    /* Create a new buffer object for the result (with one EndTag) */

    ReturnDesc = AcpiUtCreateBufferObject (NewLength);
    if (!ReturnDesc)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /*
     * Copy the templates to the new buffer, 0 first, then 1 follows. One
     * EndTag descriptor is copied from Operand1.
     */
    NewBuf = ReturnDesc->Buffer.Pointer;
    memcpy (NewBuf, Operand0->Buffer.Pointer, Length0);
    memcpy (NewBuf + Length0, Operand1->Buffer.Pointer, Length1);

    /* Insert EndTag and set the checksum to zero, means "ignore checksum" */

    NewBuf[NewLength - 1] = 0;
    NewBuf[NewLength - 2] = ACPI_RESOURCE_NAME_END_TAG | 1;

    /* Return the completed resource template */

    *ActualReturnDesc = ReturnDesc;
    return_ACPI_STATUS (AE_OK);
}
