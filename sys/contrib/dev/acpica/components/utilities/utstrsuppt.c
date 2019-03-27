/*******************************************************************************
 *
 * Module Name: utstrsuppt - Support functions for string-to-integer conversion
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

#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utstrsuppt")


/* Local prototypes */

static ACPI_STATUS
AcpiUtInsertDigit (
    UINT64                  *AccumulatedValue,
    UINT32                  Base,
    int                     AsciiDigit);

static ACPI_STATUS
AcpiUtStrtoulMultiply64 (
    UINT64                  Multiplicand,
    UINT32                  Base,
    UINT64                  *OutProduct);

static ACPI_STATUS
AcpiUtStrtoulAdd64 (
    UINT64                  Addend1,
    UINT32                  Digit,
    UINT64                  *OutSum);


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtConvertOctalString
 *
 * PARAMETERS:  String                  - Null terminated input string
 *              ReturnValuePtr          - Where the converted value is returned
 *
 * RETURN:      Status and 64-bit converted integer
 *
 * DESCRIPTION: Performs a base 8 conversion of the input string to an
 *              integer value, either 32 or 64 bits.
 *
 * NOTE:        Maximum 64-bit unsigned octal value is 01777777777777777777777
 *              Maximum 32-bit unsigned octal value is 037777777777
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtConvertOctalString (
    char                    *String,
    UINT64                  *ReturnValuePtr)
{
    UINT64                  AccumulatedValue = 0;
    ACPI_STATUS             Status = AE_OK;


    /* Convert each ASCII byte in the input string */

    while (*String)
    {
        /* Character must be ASCII 0-7, otherwise terminate with no error */

        if (!(ACPI_IS_OCTAL_DIGIT (*String)))
        {
            break;
        }

        /* Convert and insert this octal digit into the accumulator */

        Status = AcpiUtInsertDigit (&AccumulatedValue, 8, *String);
        if (ACPI_FAILURE (Status))
        {
            Status = AE_OCTAL_OVERFLOW;
            break;
        }

        String++;
    }

    /* Always return the value that has been accumulated */

    *ReturnValuePtr = AccumulatedValue;
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtConvertDecimalString
 *
 * PARAMETERS:  String                  - Null terminated input string
 *              ReturnValuePtr          - Where the converted value is returned
 *
 * RETURN:      Status and 64-bit converted integer
 *
 * DESCRIPTION: Performs a base 10 conversion of the input string to an
 *              integer value, either 32 or 64 bits.
 *
 * NOTE:        Maximum 64-bit unsigned decimal value is 18446744073709551615
 *              Maximum 32-bit unsigned decimal value is 4294967295
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtConvertDecimalString (
    char                    *String,
    UINT64                  *ReturnValuePtr)
{
    UINT64                  AccumulatedValue = 0;
    ACPI_STATUS             Status = AE_OK;


    /* Convert each ASCII byte in the input string */

    while (*String)
    {
        /* Character must be ASCII 0-9, otherwise terminate with no error */

        if (!isdigit (*String))
        {
           break;
        }

        /* Convert and insert this decimal digit into the accumulator */

        Status = AcpiUtInsertDigit (&AccumulatedValue, 10, *String);
        if (ACPI_FAILURE (Status))
        {
            Status = AE_DECIMAL_OVERFLOW;
            break;
        }

        String++;
    }

    /* Always return the value that has been accumulated */

    *ReturnValuePtr = AccumulatedValue;
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtConvertHexString
 *
 * PARAMETERS:  String                  - Null terminated input string
 *              ReturnValuePtr          - Where the converted value is returned
 *
 * RETURN:      Status and 64-bit converted integer
 *
 * DESCRIPTION: Performs a base 16 conversion of the input string to an
 *              integer value, either 32 or 64 bits.
 *
 * NOTE:        Maximum 64-bit unsigned hex value is 0xFFFFFFFFFFFFFFFF
 *              Maximum 32-bit unsigned hex value is 0xFFFFFFFF
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtConvertHexString (
    char                    *String,
    UINT64                  *ReturnValuePtr)
{
    UINT64                  AccumulatedValue = 0;
    ACPI_STATUS             Status = AE_OK;


    /* Convert each ASCII byte in the input string */

    while (*String)
    {
        /* Must be ASCII A-F, a-f, or 0-9, otherwise terminate with no error */

        if (!isxdigit (*String))
        {
            break;
        }

        /* Convert and insert this hex digit into the accumulator */

        Status = AcpiUtInsertDigit (&AccumulatedValue, 16, *String);
        if (ACPI_FAILURE (Status))
        {
            Status = AE_HEX_OVERFLOW;
            break;
        }

        String++;
    }

    /* Always return the value that has been accumulated */

    *ReturnValuePtr = AccumulatedValue;
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtRemoveLeadingZeros
 *
 * PARAMETERS:  String                  - Pointer to input ASCII string
 *
 * RETURN:      Next character after any leading zeros. This character may be
 *              used by the caller to detect end-of-string.
 *
 * DESCRIPTION: Remove any leading zeros in the input string. Return the
 *              next character after the final ASCII zero to enable the caller
 *              to check for the end of the string (NULL terminator).
 *
 ******************************************************************************/

char
AcpiUtRemoveLeadingZeros (
    char                    **String)
{

    while (**String == ACPI_ASCII_ZERO)
    {
        *String += 1;
    }

    return (**String);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtRemoveWhitespace
 *
 * PARAMETERS:  String                  - Pointer to input ASCII string
 *
 * RETURN:      Next character after any whitespace. This character may be
 *              used by the caller to detect end-of-string.
 *
 * DESCRIPTION: Remove any leading whitespace in the input string. Return the
 *              next character after the final ASCII zero to enable the caller
 *              to check for the end of the string (NULL terminator).
 *
 ******************************************************************************/

char
AcpiUtRemoveWhitespace (
    char                    **String)
{

    while (isspace ((UINT8) **String))
    {
        *String += 1;
    }

    return (**String);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDetectHexPrefix
 *
 * PARAMETERS:  String                  - Pointer to input ASCII string
 *
 * RETURN:      TRUE if a "0x" prefix was found at the start of the string
 *
 * DESCRIPTION: Detect and remove a hex "0x" prefix
 *
 ******************************************************************************/

BOOLEAN
AcpiUtDetectHexPrefix (
    char                    **String)
{
    char                    *InitialPosition = *String;

    AcpiUtRemoveHexPrefix (String);
    if (*String != InitialPosition)
    {
        return (TRUE); /* String is past leading 0x */
    }

    return (FALSE);     /* Not a hex string */
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtRemoveHexPrefix
 *
 * PARAMETERS:  String                  - Pointer to input ASCII string
 *
 * RETURN:      none
 *
 * DESCRIPTION: Remove a hex "0x" prefix
 *
 ******************************************************************************/

void
AcpiUtRemoveHexPrefix (
    char                    **String)
{
    if ((**String == ACPI_ASCII_ZERO) &&
        (tolower ((int) *(*String + 1)) == 'x'))
    {
        *String += 2;        /* Go past the leading 0x */
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDetectOctalPrefix
 *
 * PARAMETERS:  String                  - Pointer to input ASCII string
 *
 * RETURN:      True if an octal "0" prefix was found at the start of the
 *              string
 *
 * DESCRIPTION: Detect and remove an octal prefix (zero)
 *
 ******************************************************************************/

BOOLEAN
AcpiUtDetectOctalPrefix (
    char                    **String)
{

    if (**String == ACPI_ASCII_ZERO)
    {
        *String += 1;       /* Go past the leading 0 */
        return (TRUE);
    }

    return (FALSE);     /* Not an octal string */
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtInsertDigit
 *
 * PARAMETERS:  AccumulatedValue        - Current value of the integer value
 *                                        accumulator. The new value is
 *                                        returned here.
 *              Base                    - Radix, either 8/10/16
 *              AsciiDigit              - ASCII single digit to be inserted
 *
 * RETURN:      Status and result of the convert/insert operation. The only
 *              possible returned exception code is numeric overflow of
 *              either the multiply or add conversion operations.
 *
 * DESCRIPTION: Generic conversion and insertion function for all bases:
 *
 *              1) Multiply the current accumulated/converted value by the
 *              base in order to make room for the new character.
 *
 *              2) Convert the new character to binary and add it to the
 *              current accumulated value.
 *
 *              Note: The only possible exception indicates an integer
 *              overflow (AE_NUMERIC_OVERFLOW)
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtInsertDigit (
    UINT64                  *AccumulatedValue,
    UINT32                  Base,
    int                     AsciiDigit)
{
    ACPI_STATUS             Status;
    UINT64                  Product;


     /* Make room in the accumulated value for the incoming digit */

    Status = AcpiUtStrtoulMultiply64 (*AccumulatedValue, Base, &Product);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* Add in the new digit, and store the sum to the accumulated value */

    Status = AcpiUtStrtoulAdd64 (Product, AcpiUtAsciiCharToHex (AsciiDigit),
        AccumulatedValue);

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtStrtoulMultiply64
 *
 * PARAMETERS:  Multiplicand            - Current accumulated converted integer
 *              Base                    - Base/Radix
 *              OutProduct              - Where the product is returned
 *
 * RETURN:      Status and 64-bit product
 *
 * DESCRIPTION: Multiply two 64-bit values, with checking for 64-bit overflow as
 *              well as 32-bit overflow if necessary (if the current global
 *              integer width is 32).
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtStrtoulMultiply64 (
    UINT64                  Multiplicand,
    UINT32                  Base,
    UINT64                  *OutProduct)
{
    UINT64                  Product;
    UINT64                  Quotient;


    /* Exit if either operand is zero */

    *OutProduct = 0;
    if (!Multiplicand || !Base)
    {
        return (AE_OK);
    }

    /*
     * Check for 64-bit overflow before the actual multiplication.
     *
     * Notes: 64-bit division is often not supported on 32-bit platforms
     * (it requires a library function), Therefore ACPICA has a local
     * 64-bit divide function. Also, Multiplier is currently only used
     * as the radix (8/10/16), to the 64/32 divide will always work.
     */
    AcpiUtShortDivide (ACPI_UINT64_MAX, Base, &Quotient, NULL);
    if (Multiplicand > Quotient)
    {
        return (AE_NUMERIC_OVERFLOW);
    }

    Product = Multiplicand * Base;

    /* Check for 32-bit overflow if necessary */

    if ((AcpiGbl_IntegerBitWidth == 32) && (Product > ACPI_UINT32_MAX))
    {
        return (AE_NUMERIC_OVERFLOW);
    }

    *OutProduct = Product;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtStrtoulAdd64
 *
 * PARAMETERS:  Addend1                 - Current accumulated converted integer
 *              Digit                   - New hex value/char
 *              OutSum                  - Where sum is returned (Accumulator)
 *
 * RETURN:      Status and 64-bit sum
 *
 * DESCRIPTION: Add two 64-bit values, with checking for 64-bit overflow as
 *              well as 32-bit overflow if necessary (if the current global
 *              integer width is 32).
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiUtStrtoulAdd64 (
    UINT64                  Addend1,
    UINT32                  Digit,
    UINT64                  *OutSum)
{
    UINT64                  Sum;


    /* Check for 64-bit overflow before the actual addition */

    if ((Addend1 > 0) && (Digit > (ACPI_UINT64_MAX - Addend1)))
    {
        return (AE_NUMERIC_OVERFLOW);
    }

    Sum = Addend1 + Digit;

    /* Check for 32-bit overflow if necessary */

    if ((AcpiGbl_IntegerBitWidth == 32) && (Sum > ACPI_UINT32_MAX))
    {
        return (AE_NUMERIC_OVERFLOW);
    }

    *OutSum = Sum;
    return (AE_OK);
}
