/*******************************************************************************
 *
 * Module Name: utstrtoul64 - String-to-integer conversion support for both
 *                            64-bit and 32-bit integers
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
        ACPI_MODULE_NAME    ("utstrtoul64")


/*******************************************************************************
 *
 * This module contains the top-level string to 64/32-bit unsigned integer
 * conversion functions:
 *
 *  1) A standard strtoul() function that supports 64-bit integers, base
 *     8/10/16, with integer overflow support. This is used mainly by the
 *     iASL compiler, which implements tighter constraints on integer
 *     constants than the runtime (interpreter) integer-to-string conversions.
 *  2) Runtime "Explicit conversion" as defined in the ACPI specification.
 *  3) Runtime "Implicit conversion" as defined in the ACPI specification.
 *
 * Current users of this module:
 *
 *  iASL        - Preprocessor (constants and math expressions)
 *  iASL        - Main parser, conversion of constants to integers
 *  iASL        - Data Table Compiler parser (constants and math expressions)
 *  Interpreter - Implicit and explicit conversions, GPE method names
 *  Interpreter - Repair code for return values from predefined names
 *  Debugger    - Command line input string conversion
 *  AcpiDump    - ACPI table physical addresses
 *  AcpiExec    - Support for namespace overrides
 *
 * Notes concerning users of these interfaces:
 *
 * AcpiGbl_IntegerByteWidth is used to set the 32/64 bit limit for explicit
 * and implicit conversions. This global must be set to the proper width.
 * For the core ACPICA code, the width depends on the DSDT version. For the
 * AcpiUtStrtoul64 interface, all conversions are 64 bits. This interface is
 * used primarily for iASL, where the default width is 64 bits for all parsers,
 * but error checking is performed later to flag cases where a 64-bit constant
 * is wrongly defined in a 32-bit DSDT/SSDT.
 *
 * In ACPI, the only place where octal numbers are supported is within
 * the ASL language itself. This is implemented via the main AcpiUtStrtoul64
 * interface. According the ACPI specification, there is no ACPI runtime
 * support (explicit/implicit) for octal string conversions.
 *
 ******************************************************************************/


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtStrtoul64
 *
 * PARAMETERS:  String                  - Null terminated input string,
 *                                        must be a valid pointer
 *              ReturnValue             - Where the converted integer is
 *                                        returned. Must be a valid pointer
 *
 * RETURN:      Status and converted integer. Returns an exception on a
 *              64-bit numeric overflow
 *
 * DESCRIPTION: Convert a string into an unsigned integer. Always performs a
 *              full 64-bit conversion, regardless of the current global
 *              integer width. Supports Decimal, Hex, and Octal strings.
 *
 * Current users of this function:
 *
 *  iASL        - Preprocessor (constants and math expressions)
 *  iASL        - Main ASL parser, conversion of ASL constants to integers
 *  iASL        - Data Table Compiler parser (constants and math expressions)
 *  Interpreter - Repair code for return values from predefined names
 *  AcpiDump    - ACPI table physical addresses
 *  AcpiExec    - Support for namespace overrides
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtStrtoul64 (
    char                    *String,
    UINT64                  *ReturnValue)
{
    ACPI_STATUS             Status = AE_OK;
    UINT8                   OriginalBitWidth;
    UINT32                  Base = 10;          /* Default is decimal */


    ACPI_FUNCTION_TRACE_STR (UtStrtoul64, String);


    *ReturnValue = 0;

    /* A NULL return string returns a value of zero */

    if (*String == 0)
    {
        return_ACPI_STATUS (AE_OK);
    }

    if (!AcpiUtRemoveWhitespace (&String))
    {
        return_ACPI_STATUS (AE_OK);
    }

    /*
     * 1) Check for a hex constant. A "0x" prefix indicates base 16.
     */
    if (AcpiUtDetectHexPrefix (&String))
    {
        Base = 16;
    }

    /*
     * 2) Check for an octal constant, defined to be a leading zero
     * followed by sequence of octal digits (0-7)
     */
    else if (AcpiUtDetectOctalPrefix (&String))
    {
        Base = 8;
    }

    if (!AcpiUtRemoveLeadingZeros (&String))
    {
        return_ACPI_STATUS (AE_OK);     /* Return value 0 */
    }

    /*
     * Force a full 64-bit conversion. The caller (usually iASL) must
     * check for a 32-bit overflow later as necessary (If current mode
     * is 32-bit, meaning a 32-bit DSDT).
     */
    OriginalBitWidth = AcpiGbl_IntegerBitWidth;
    AcpiGbl_IntegerBitWidth = 64;

    /*
     * Perform the base 8, 10, or 16 conversion. A 64-bit numeric overflow
     * will return an exception (to allow iASL to flag the statement).
     */
    switch (Base)
    {
    case 8:
        Status = AcpiUtConvertOctalString (String, ReturnValue);
        break;

    case 10:
        Status = AcpiUtConvertDecimalString (String, ReturnValue);
        break;

    case 16:
    default:
        Status = AcpiUtConvertHexString (String, ReturnValue);
        break;
    }

    /* Only possible exception from above is a 64-bit overflow */

    AcpiGbl_IntegerBitWidth = OriginalBitWidth;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtImplicitStrtoul64
 *
 * PARAMETERS:  String                  - Null terminated input string,
 *                                        must be a valid pointer
 *
 * RETURN:      Converted integer
 *
 * DESCRIPTION: Perform a 64-bit conversion with restrictions placed upon
 *              an "implicit conversion" by the ACPI specification. Used by
 *              many ASL operators that require an integer operand, and support
 *              an automatic (implicit) conversion from a string operand
 *              to the final integer operand. The major restriction is that
 *              only hex strings are supported.
 *
 * -----------------------------------------------------------------------------
 *
 * Base is always 16, either with or without the 0x prefix. Decimal and
 * Octal strings are not supported, as per the ACPI specification.
 *
 * Examples (both are hex values):
 *      Add ("BA98", Arg0, Local0)
 *      Subtract ("0x12345678", Arg1, Local1)
 *
 * Conversion rules as extracted from the ACPI specification:
 *
 *  The converted integer is initialized to the value zero.
 *  The ASCII string is always interpreted as a hexadecimal constant.
 *
 *  1)  According to the ACPI specification, a "0x" prefix is not allowed.
 *      However, ACPICA allows this as an ACPI extension on general
 *      principle. (NO ERROR)
 *
 *  2)  The conversion terminates when the size of an integer is reached
 *      (32 or 64 bits). There are no numeric overflow conditions. (NO ERROR)
 *
 *  3)  The first non-hex character terminates the conversion and returns
 *      the current accumulated value of the converted integer (NO ERROR).
 *
 *  4)  Conversion of a null (zero-length) string to an integer is
 *      technically not allowed. However, ACPICA allows this as an ACPI
 *      extension. The conversion returns the value 0. (NO ERROR)
 *
 * NOTE: There are no error conditions returned by this function. At
 * the minimum, a value of zero is returned.
 *
 * Current users of this function:
 *
 *  Interpreter - All runtime implicit conversions, as per ACPI specification
 *  iASL        - Data Table Compiler parser (constants and math expressions)
 *
 ******************************************************************************/

UINT64
AcpiUtImplicitStrtoul64 (
    char                    *String)
{
    UINT64                  ConvertedInteger = 0;


    ACPI_FUNCTION_TRACE_STR (UtImplicitStrtoul64, String);


    if (!AcpiUtRemoveWhitespace (&String))
    {
        return_VALUE (0);
    }

    /*
     * Per the ACPI specification, only hexadecimal is supported for
     * implicit conversions, and the "0x" prefix is "not allowed".
     * However, allow a "0x" prefix as an ACPI extension.
     */
    AcpiUtRemoveHexPrefix (&String);

    if (!AcpiUtRemoveLeadingZeros (&String))
    {
        return_VALUE (0);
    }

    /*
     * Ignore overflow as per the ACPI specification. This is implemented by
     * ignoring the return status from the conversion function called below.
     * On overflow, the input string is simply truncated.
     */
    AcpiUtConvertHexString (String, &ConvertedInteger);
    return_VALUE (ConvertedInteger);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtExplicitStrtoul64
 *
 * PARAMETERS:  String                  - Null terminated input string,
 *                                        must be a valid pointer
 *
 * RETURN:      Converted integer
 *
 * DESCRIPTION: Perform a 64-bit conversion with the restrictions placed upon
 *              an "explicit conversion" by the ACPI specification. The
 *              main restriction is that only hex and decimal are supported.
 *
 * -----------------------------------------------------------------------------
 *
 * Base is either 10 (default) or 16 (with 0x prefix). Octal (base 8) strings
 * are not supported, as per the ACPI specification.
 *
 * Examples:
 *      ToInteger ("1000")      Decimal
 *      ToInteger ("0xABCD")    Hex
 *
 * Conversion rules as extracted from the ACPI specification:
 *
 *  1)  The input string is either a decimal or hexadecimal numeric string.
 *      A hex value must be prefixed by "0x" or it is interpreted as decimal.
 *
 *  2)  The value must not exceed the maximum of an integer value
 *      (32 or 64 bits). The ACPI specification states the behavior is
 *      "unpredictable", so ACPICA matches the behavior of the implicit
 *      conversion case. There are no numeric overflow conditions. (NO ERROR)
 *
 *  3)  Behavior on the first non-hex character is not defined by the ACPI
 *      specification (for the ToInteger operator), so ACPICA matches the
 *      behavior of the implicit conversion case. It terminates the
 *      conversion and returns the current accumulated value of the converted
 *      integer. (NO ERROR)
 *
 *  4)  Conversion of a null (zero-length) string to an integer is
 *      technically not allowed. However, ACPICA allows this as an ACPI
 *      extension. The conversion returns the value 0. (NO ERROR)
 *
 * NOTE: There are no error conditions returned by this function. At the
 * minimum, a value of zero is returned.
 *
 * Current users of this function:
 *
 *  Interpreter - Runtime ASL ToInteger operator, as per the ACPI specification
 *
 ******************************************************************************/

UINT64
AcpiUtExplicitStrtoul64 (
    char                    *String)
{
    UINT64                  ConvertedInteger = 0;
    UINT32                  Base = 10;          /* Default is decimal */


    ACPI_FUNCTION_TRACE_STR (UtExplicitStrtoul64, String);


    if (!AcpiUtRemoveWhitespace (&String))
    {
        return_VALUE (0);
    }

    /*
     * Only Hex and Decimal are supported, as per the ACPI specification.
     * A "0x" prefix indicates hex; otherwise decimal is assumed.
     */
    if (AcpiUtDetectHexPrefix (&String))
    {
        Base = 16;
    }

    if (!AcpiUtRemoveLeadingZeros (&String))
    {
        return_VALUE (0);
    }

    /*
     * Ignore overflow as per the ACPI specification. This is implemented by
     * ignoring the return status from the conversion functions called below.
     * On overflow, the input string is simply truncated.
     */
    switch (Base)
    {
    case 10:
    default:
        AcpiUtConvertDecimalString (String, &ConvertedInteger);
        break;

    case 16:
        AcpiUtConvertHexString (String, &ConvertedInteger);
        break;
    }

    return_VALUE (ConvertedInteger);
}
