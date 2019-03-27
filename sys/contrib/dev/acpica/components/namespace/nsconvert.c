/******************************************************************************
 *
 * Module Name: nsconvert - Object conversions for objects returned by
 *                          predefined methods
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
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acpredef.h>
#include <contrib/dev/acpica/include/amlresrc.h>

#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nsconvert")


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsConvertToInteger
 *
 * PARAMETERS:  OriginalObject      - Object to be converted
 *              ReturnObject        - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful.
 *
 * DESCRIPTION: Attempt to convert a String/Buffer object to an Integer.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsConvertToInteger (
    ACPI_OPERAND_OBJECT     *OriginalObject,
    ACPI_OPERAND_OBJECT     **ReturnObject)
{
    ACPI_OPERAND_OBJECT     *NewObject;
    ACPI_STATUS             Status;
    UINT64                  Value = 0;
    UINT32                  i;


    switch (OriginalObject->Common.Type)
    {
    case ACPI_TYPE_STRING:

        /* String-to-Integer conversion */

        Status = AcpiUtStrtoul64 (OriginalObject->String.Pointer, &Value);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        break;

    case ACPI_TYPE_BUFFER:

        /* Buffer-to-Integer conversion. Max buffer size is 64 bits. */

        if (OriginalObject->Buffer.Length > 8)
        {
            return (AE_AML_OPERAND_TYPE);
        }

        /* Extract each buffer byte to create the integer */

        for (i = 0; i < OriginalObject->Buffer.Length; i++)
        {
            Value |= ((UINT64)
                OriginalObject->Buffer.Pointer[i] << (i * 8));
        }
        break;

    default:

        return (AE_AML_OPERAND_TYPE);
    }

    NewObject = AcpiUtCreateIntegerObject (Value);
    if (!NewObject)
    {
        return (AE_NO_MEMORY);
    }

    *ReturnObject = NewObject;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsConvertToString
 *
 * PARAMETERS:  OriginalObject      - Object to be converted
 *              ReturnObject        - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful.
 *
 * DESCRIPTION: Attempt to convert a Integer/Buffer object to a String.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsConvertToString (
    ACPI_OPERAND_OBJECT     *OriginalObject,
    ACPI_OPERAND_OBJECT     **ReturnObject)
{
    ACPI_OPERAND_OBJECT     *NewObject;
    ACPI_SIZE               Length;
    ACPI_STATUS             Status;


    switch (OriginalObject->Common.Type)
    {
    case ACPI_TYPE_INTEGER:
        /*
         * Integer-to-String conversion. Commonly, convert
         * an integer of value 0 to a NULL string. The last element of
         * _BIF and _BIX packages occasionally need this fix.
         */
        if (OriginalObject->Integer.Value == 0)
        {
            /* Allocate a new NULL string object */

            NewObject = AcpiUtCreateStringObject (0);
            if (!NewObject)
            {
                return (AE_NO_MEMORY);
            }
        }
        else
        {
            Status = AcpiExConvertToString (OriginalObject,
                &NewObject, ACPI_IMPLICIT_CONVERT_HEX);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }
        }
        break;

    case ACPI_TYPE_BUFFER:
        /*
         * Buffer-to-String conversion. Use a ToString
         * conversion, no transform performed on the buffer data. The best
         * example of this is the _BIF method, where the string data from
         * the battery is often (incorrectly) returned as buffer object(s).
         */
        Length = 0;
        while ((Length < OriginalObject->Buffer.Length) &&
                (OriginalObject->Buffer.Pointer[Length]))
        {
            Length++;
        }

        /* Allocate a new string object */

        NewObject = AcpiUtCreateStringObject (Length);
        if (!NewObject)
        {
            return (AE_NO_MEMORY);
        }

        /*
         * Copy the raw buffer data with no transform. String is already NULL
         * terminated at Length+1.
         */
        memcpy (NewObject->String.Pointer,
            OriginalObject->Buffer.Pointer, Length);
        break;

    default:

        return (AE_AML_OPERAND_TYPE);
    }

    *ReturnObject = NewObject;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsConvertToBuffer
 *
 * PARAMETERS:  OriginalObject      - Object to be converted
 *              ReturnObject        - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful.
 *
 * DESCRIPTION: Attempt to convert a Integer/String/Package object to a Buffer.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsConvertToBuffer (
    ACPI_OPERAND_OBJECT     *OriginalObject,
    ACPI_OPERAND_OBJECT     **ReturnObject)
{
    ACPI_OPERAND_OBJECT     *NewObject;
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     **Elements;
    UINT32                  *DwordBuffer;
    UINT32                  Count;
    UINT32                  i;


    switch (OriginalObject->Common.Type)
    {
    case ACPI_TYPE_INTEGER:
        /*
         * Integer-to-Buffer conversion.
         * Convert the Integer to a packed-byte buffer. _MAT and other
         * objects need this sometimes, if a read has been performed on a
         * Field object that is less than or equal to the global integer
         * size (32 or 64 bits).
         */
        Status = AcpiExConvertToBuffer (OriginalObject, &NewObject);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        break;

    case ACPI_TYPE_STRING:

        /* String-to-Buffer conversion. Simple data copy */

        NewObject = AcpiUtCreateBufferObject
            (OriginalObject->String.Length);
        if (!NewObject)
        {
            return (AE_NO_MEMORY);
        }

        memcpy (NewObject->Buffer.Pointer,
            OriginalObject->String.Pointer, OriginalObject->String.Length);
        break;

    case ACPI_TYPE_PACKAGE:
        /*
         * This case is often seen for predefined names that must return a
         * Buffer object with multiple DWORD integers within. For example,
         * _FDE and _GTM. The Package can be converted to a Buffer.
         */

        /* All elements of the Package must be integers */

        Elements = OriginalObject->Package.Elements;
        Count = OriginalObject->Package.Count;

        for (i = 0; i < Count; i++)
        {
            if ((!*Elements) ||
                ((*Elements)->Common.Type != ACPI_TYPE_INTEGER))
            {
                return (AE_AML_OPERAND_TYPE);
            }
            Elements++;
        }

        /* Create the new buffer object to replace the Package */

        NewObject = AcpiUtCreateBufferObject (ACPI_MUL_4 (Count));
        if (!NewObject)
        {
            return (AE_NO_MEMORY);
        }

        /* Copy the package elements (integers) to the buffer as DWORDs */

        Elements = OriginalObject->Package.Elements;
        DwordBuffer = ACPI_CAST_PTR (UINT32, NewObject->Buffer.Pointer);

        for (i = 0; i < Count; i++)
        {
            *DwordBuffer = (UINT32) (*Elements)->Integer.Value;
            DwordBuffer++;
            Elements++;
        }
        break;

    default:

        return (AE_AML_OPERAND_TYPE);
    }

    *ReturnObject = NewObject;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsConvertToUnicode
 *
 * PARAMETERS:  Scope               - Namespace node for the method/object
 *              OriginalObject      - ASCII String Object to be converted
 *              ReturnObject        - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful.
 *
 * DESCRIPTION: Attempt to convert a String object to a Unicode string Buffer.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsConvertToUnicode (
    ACPI_NAMESPACE_NODE     *Scope,
    ACPI_OPERAND_OBJECT     *OriginalObject,
    ACPI_OPERAND_OBJECT     **ReturnObject)
{
    ACPI_OPERAND_OBJECT     *NewObject;
    char                    *AsciiString;
    UINT16                  *UnicodeBuffer;
    UINT32                  UnicodeLength;
    UINT32                  i;


    if (!OriginalObject)
    {
        return (AE_OK);
    }

    /* If a Buffer was returned, it must be at least two bytes long */

    if (OriginalObject->Common.Type == ACPI_TYPE_BUFFER)
    {
        if (OriginalObject->Buffer.Length < 2)
        {
            return (AE_AML_OPERAND_VALUE);
        }

        *ReturnObject = NULL;
        return (AE_OK);
    }

    /*
     * The original object is an ASCII string. Convert this string to
     * a unicode buffer.
     */
    AsciiString = OriginalObject->String.Pointer;
    UnicodeLength = (OriginalObject->String.Length * 2) + 2;

    /* Create a new buffer object for the Unicode data */

    NewObject = AcpiUtCreateBufferObject (UnicodeLength);
    if (!NewObject)
    {
        return (AE_NO_MEMORY);
    }

    UnicodeBuffer = ACPI_CAST_PTR (UINT16, NewObject->Buffer.Pointer);

    /* Convert ASCII to Unicode */

    for (i = 0; i < OriginalObject->String.Length; i++)
    {
        UnicodeBuffer[i] = (UINT16) AsciiString[i];
    }

    *ReturnObject = NewObject;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsConvertToResource
 *
 * PARAMETERS:  Scope               - Namespace node for the method/object
 *              OriginalObject      - Object to be converted
 *              ReturnObject        - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful
 *
 * DESCRIPTION: Attempt to convert a Integer object to a ResourceTemplate
 *              Buffer.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsConvertToResource (
    ACPI_NAMESPACE_NODE     *Scope,
    ACPI_OPERAND_OBJECT     *OriginalObject,
    ACPI_OPERAND_OBJECT     **ReturnObject)
{
    ACPI_OPERAND_OBJECT     *NewObject;
    UINT8                   *Buffer;


    /*
     * We can fix the following cases for an expected resource template:
     * 1. No return value (interpreter slack mode is disabled)
     * 2. A "Return (Zero)" statement
     * 3. A "Return empty buffer" statement
     *
     * We will return a buffer containing a single EndTag
     * resource descriptor.
     */
    if (OriginalObject)
    {
        switch (OriginalObject->Common.Type)
        {
        case ACPI_TYPE_INTEGER:

            /* We can only repair an Integer==0 */

            if (OriginalObject->Integer.Value)
            {
                return (AE_AML_OPERAND_TYPE);
            }
            break;

        case ACPI_TYPE_BUFFER:

            if (OriginalObject->Buffer.Length)
            {
                /* Additional checks can be added in the future */

                *ReturnObject = NULL;
                return (AE_OK);
            }
            break;

        case ACPI_TYPE_STRING:
        default:

            return (AE_AML_OPERAND_TYPE);
        }
    }

    /* Create the new buffer object for the resource descriptor */

    NewObject = AcpiUtCreateBufferObject (2);
    if (!NewObject)
    {
        return (AE_NO_MEMORY);
    }

    Buffer = ACPI_CAST_PTR (UINT8, NewObject->Buffer.Pointer);

    /* Initialize the Buffer with a single EndTag descriptor */

    Buffer[0] = (ACPI_RESOURCE_NAME_END_TAG | ASL_RDESC_END_TAG_SIZE);
    Buffer[1] = 0x00;

    *ReturnObject = NewObject;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsConvertToReference
 *
 * PARAMETERS:  Scope               - Namespace node for the method/object
 *              OriginalObject      - Object to be converted
 *              ReturnObject        - Where the new converted object is returned
 *
 * RETURN:      Status. AE_OK if conversion was successful
 *
 * DESCRIPTION: Attempt to convert a Integer object to a ObjectReference.
 *              Buffer.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsConvertToReference (
    ACPI_NAMESPACE_NODE     *Scope,
    ACPI_OPERAND_OBJECT     *OriginalObject,
    ACPI_OPERAND_OBJECT     **ReturnObject)
{
    ACPI_OPERAND_OBJECT     *NewObject = NULL;
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_GENERIC_STATE      ScopeInfo;
    char                    *Name;


    ACPI_FUNCTION_NAME (NsConvertToReference);


    /* Convert path into internal presentation */

    Status = AcpiNsInternalizeName (OriginalObject->String.Pointer, &Name);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Find the namespace node */

    ScopeInfo.Scope.Node = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, Scope);
    Status = AcpiNsLookup (&ScopeInfo, Name,
        ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
        ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE, NULL, &Node);
    if (ACPI_FAILURE (Status))
    {
        /* Check if we are resolving a named reference within a package */

        ACPI_ERROR_NAMESPACE (&ScopeInfo,
            OriginalObject->String.Pointer, Status);
        goto ErrorExit;
    }

    /* Create and init a new internal ACPI object */

    NewObject = AcpiUtCreateInternalObject (ACPI_TYPE_LOCAL_REFERENCE);
    if (!NewObject)
    {
        Status = AE_NO_MEMORY;
        goto ErrorExit;
    }
    NewObject->Reference.Node = Node;
    NewObject->Reference.Object = Node->Object;
    NewObject->Reference.Class = ACPI_REFCLASS_NAME;

    /*
     * Increase reference of the object if needed (the object is likely a
     * null for device nodes).
     */
    AcpiUtAddReference (Node->Object);

ErrorExit:
    ACPI_FREE (Name);
    *ReturnObject = NewObject;
    return (AE_OK);
}
