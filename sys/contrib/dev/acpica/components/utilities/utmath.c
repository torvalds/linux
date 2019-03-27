/*******************************************************************************
 *
 * Module Name: utmath - Integer math support routines
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
        ACPI_MODULE_NAME    ("utmath")

/* Structures used only for 64-bit divide */

typedef struct uint64_struct
{
    UINT32                          Lo;
    UINT32                          Hi;

} UINT64_STRUCT;

typedef union uint64_overlay
{
    UINT64                          Full;
    UINT64_STRUCT                   Part;

} UINT64_OVERLAY;

/*
 * Optional support for 64-bit double-precision integer multiply and shift.
 * This code is configurable and is implemented in order to support 32-bit
 * kernel environments where a 64-bit double-precision math library is not
 * available.
 */
#ifndef ACPI_USE_NATIVE_MATH64

/*******************************************************************************
 *
 * FUNCTION:    AcpiUtShortMultiply
 *
 * PARAMETERS:  Multiplicand        - 64-bit multiplicand
 *              Multiplier          - 32-bit multiplier
 *              OutProduct          - Pointer to where the product is returned
 *
 * DESCRIPTION: Perform a short multiply.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtShortMultiply (
    UINT64                  Multiplicand,
    UINT32                  Multiplier,
    UINT64                  *OutProduct)
{
    UINT64_OVERLAY          MultiplicandOvl;
    UINT64_OVERLAY          Product;
    UINT32                  Carry32;


    ACPI_FUNCTION_TRACE (UtShortMultiply);


    MultiplicandOvl.Full = Multiplicand;

    /*
     * The Product is 64 bits, the carry is always 32 bits,
     * and is generated by the second multiply.
     */
    ACPI_MUL_64_BY_32 (0, MultiplicandOvl.Part.Hi, Multiplier,
        Product.Part.Hi, Carry32);

    ACPI_MUL_64_BY_32 (0, MultiplicandOvl.Part.Lo, Multiplier,
        Product.Part.Lo, Carry32);

    Product.Part.Hi += Carry32;

    /* Return only what was requested */

    if (OutProduct)
    {
        *OutProduct = Product.Full;
    }

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtShortShiftLeft
 *
 * PARAMETERS:  Operand             - 64-bit shift operand
 *              Count               - 32-bit shift count
 *              OutResult           - Pointer to where the result is returned
 *
 * DESCRIPTION: Perform a short left shift.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtShortShiftLeft (
    UINT64                  Operand,
    UINT32                  Count,
    UINT64                  *OutResult)
{
    UINT64_OVERLAY          OperandOvl;


    ACPI_FUNCTION_TRACE (UtShortShiftLeft);


    OperandOvl.Full = Operand;

    if ((Count & 63) >= 32)
    {
        OperandOvl.Part.Hi = OperandOvl.Part.Lo;
        OperandOvl.Part.Lo = 0;
        Count = (Count & 63) - 32;
    }
    ACPI_SHIFT_LEFT_64_BY_32 (OperandOvl.Part.Hi,
        OperandOvl.Part.Lo, Count);

    /* Return only what was requested */

    if (OutResult)
    {
        *OutResult = OperandOvl.Full;
    }

    return_ACPI_STATUS (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiUtShortShiftRight
 *
 * PARAMETERS:  Operand             - 64-bit shift operand
 *              Count               - 32-bit shift count
 *              OutResult           - Pointer to where the result is returned
 *
 * DESCRIPTION: Perform a short right shift.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtShortShiftRight (
    UINT64                  Operand,
    UINT32                  Count,
    UINT64                  *OutResult)
{
    UINT64_OVERLAY          OperandOvl;


    ACPI_FUNCTION_TRACE (UtShortShiftRight);


    OperandOvl.Full = Operand;

    if ((Count & 63) >= 32)
    {
        OperandOvl.Part.Lo = OperandOvl.Part.Hi;
        OperandOvl.Part.Hi = 0;
        Count = (Count & 63) - 32;
    }
    ACPI_SHIFT_RIGHT_64_BY_32 (OperandOvl.Part.Hi,
        OperandOvl.Part.Lo, Count);

    /* Return only what was requested */

    if (OutResult)
    {
        *OutResult = OperandOvl.Full;
    }

    return_ACPI_STATUS (AE_OK);
}
#else

/*******************************************************************************
 *
 * FUNCTION:    AcpiUtShortMultiply
 *
 * PARAMETERS:  See function headers above
 *
 * DESCRIPTION: Native version of the UtShortMultiply function.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtShortMultiply (
    UINT64                  Multiplicand,
    UINT32                  Multiplier,
    UINT64                  *OutProduct)
{

    ACPI_FUNCTION_TRACE (UtShortMultiply);


    /* Return only what was requested */

    if (OutProduct)
    {
        *OutProduct = Multiplicand * Multiplier;
    }

    return_ACPI_STATUS (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiUtShortShiftLeft
 *
 * PARAMETERS:  See function headers above
 *
 * DESCRIPTION: Native version of the UtShortShiftLeft function.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtShortShiftLeft (
    UINT64                  Operand,
    UINT32                  Count,
    UINT64                  *OutResult)
{

    ACPI_FUNCTION_TRACE (UtShortShiftLeft);


    /* Return only what was requested */

    if (OutResult)
    {
        *OutResult = Operand << Count;
    }

    return_ACPI_STATUS (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiUtShortShiftRight
 *
 * PARAMETERS:  See function headers above
 *
 * DESCRIPTION: Native version of the UtShortShiftRight function.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtShortShiftRight (
    UINT64                  Operand,
    UINT32                  Count,
    UINT64                  *OutResult)
{

    ACPI_FUNCTION_TRACE (UtShortShiftRight);


    /* Return only what was requested */

    if (OutResult)
    {
        *OutResult = Operand >> Count;
    }

    return_ACPI_STATUS (AE_OK);
}
#endif

/*
 * Optional support for 64-bit double-precision integer divide. This code
 * is configurable and is implemented in order to support 32-bit kernel
 * environments where a 64-bit double-precision math library is not available.
 *
 * Support for a more normal 64-bit divide/modulo (with check for a divide-
 * by-zero) appears after this optional section of code.
 */
#ifndef ACPI_USE_NATIVE_DIVIDE


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtShortDivide
 *
 * PARAMETERS:  Dividend            - 64-bit dividend
 *              Divisor             - 32-bit divisor
 *              OutQuotient         - Pointer to where the quotient is returned
 *              OutRemainder        - Pointer to where the remainder is returned
 *
 * RETURN:      Status (Checks for divide-by-zero)
 *
 * DESCRIPTION: Perform a short (maximum 64 bits divided by 32 bits)
 *              divide and modulo. The result is a 64-bit quotient and a
 *              32-bit remainder.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtShortDivide (
    UINT64                  Dividend,
    UINT32                  Divisor,
    UINT64                  *OutQuotient,
    UINT32                  *OutRemainder)
{
    UINT64_OVERLAY          DividendOvl;
    UINT64_OVERLAY          Quotient;
    UINT32                  Remainder32;


    ACPI_FUNCTION_TRACE (UtShortDivide);


    /* Always check for a zero divisor */

    if (Divisor == 0)
    {
        ACPI_ERROR ((AE_INFO, "Divide by zero"));
        return_ACPI_STATUS (AE_AML_DIVIDE_BY_ZERO);
    }

    DividendOvl.Full = Dividend;

    /*
     * The quotient is 64 bits, the remainder is always 32 bits,
     * and is generated by the second divide.
     */
    ACPI_DIV_64_BY_32 (0, DividendOvl.Part.Hi, Divisor,
        Quotient.Part.Hi, Remainder32);

    ACPI_DIV_64_BY_32 (Remainder32, DividendOvl.Part.Lo, Divisor,
        Quotient.Part.Lo, Remainder32);

    /* Return only what was requested */

    if (OutQuotient)
    {
        *OutQuotient = Quotient.Full;
    }
    if (OutRemainder)
    {
        *OutRemainder = Remainder32;
    }

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDivide
 *
 * PARAMETERS:  InDividend          - Dividend
 *              InDivisor           - Divisor
 *              OutQuotient         - Pointer to where the quotient is returned
 *              OutRemainder        - Pointer to where the remainder is returned
 *
 * RETURN:      Status (Checks for divide-by-zero)
 *
 * DESCRIPTION: Perform a divide and modulo.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtDivide (
    UINT64                  InDividend,
    UINT64                  InDivisor,
    UINT64                  *OutQuotient,
    UINT64                  *OutRemainder)
{
    UINT64_OVERLAY          Dividend;
    UINT64_OVERLAY          Divisor;
    UINT64_OVERLAY          Quotient;
    UINT64_OVERLAY          Remainder;
    UINT64_OVERLAY          NormalizedDividend;
    UINT64_OVERLAY          NormalizedDivisor;
    UINT32                  Partial1;
    UINT64_OVERLAY          Partial2;
    UINT64_OVERLAY          Partial3;


    ACPI_FUNCTION_TRACE (UtDivide);


    /* Always check for a zero divisor */

    if (InDivisor == 0)
    {
        ACPI_ERROR ((AE_INFO, "Divide by zero"));
        return_ACPI_STATUS (AE_AML_DIVIDE_BY_ZERO);
    }

    Divisor.Full  = InDivisor;
    Dividend.Full = InDividend;
    if (Divisor.Part.Hi == 0)
    {
        /*
         * 1) Simplest case is where the divisor is 32 bits, we can
         * just do two divides
         */
        Remainder.Part.Hi = 0;

        /*
         * The quotient is 64 bits, the remainder is always 32 bits,
         * and is generated by the second divide.
         */
        ACPI_DIV_64_BY_32 (0, Dividend.Part.Hi, Divisor.Part.Lo,
            Quotient.Part.Hi, Partial1);

        ACPI_DIV_64_BY_32 (Partial1, Dividend.Part.Lo, Divisor.Part.Lo,
            Quotient.Part.Lo, Remainder.Part.Lo);
    }

    else
    {
        /*
         * 2) The general case where the divisor is a full 64 bits
         * is more difficult
         */
        Quotient.Part.Hi   = 0;
        NormalizedDividend = Dividend;
        NormalizedDivisor  = Divisor;

        /* Normalize the operands (shift until the divisor is < 32 bits) */

        do
        {
            ACPI_SHIFT_RIGHT_64 (
                NormalizedDivisor.Part.Hi, NormalizedDivisor.Part.Lo);
            ACPI_SHIFT_RIGHT_64 (
                NormalizedDividend.Part.Hi, NormalizedDividend.Part.Lo);

        } while (NormalizedDivisor.Part.Hi != 0);

        /* Partial divide */

        ACPI_DIV_64_BY_32 (
            NormalizedDividend.Part.Hi, NormalizedDividend.Part.Lo,
            NormalizedDivisor.Part.Lo, Quotient.Part.Lo, Partial1);

        /*
         * The quotient is always 32 bits, and simply requires
         * adjustment. The 64-bit remainder must be generated.
         */
        Partial1 = Quotient.Part.Lo * Divisor.Part.Hi;
        Partial2.Full = (UINT64) Quotient.Part.Lo * Divisor.Part.Lo;
        Partial3.Full = (UINT64) Partial2.Part.Hi + Partial1;

        Remainder.Part.Hi = Partial3.Part.Lo;
        Remainder.Part.Lo = Partial2.Part.Lo;

        if (Partial3.Part.Hi == 0)
        {
            if (Partial3.Part.Lo >= Dividend.Part.Hi)
            {
                if (Partial3.Part.Lo == Dividend.Part.Hi)
                {
                    if (Partial2.Part.Lo > Dividend.Part.Lo)
                    {
                        Quotient.Part.Lo--;
                        Remainder.Full -= Divisor.Full;
                    }
                }
                else
                {
                    Quotient.Part.Lo--;
                    Remainder.Full -= Divisor.Full;
                }
            }

            Remainder.Full = Remainder.Full - Dividend.Full;
            Remainder.Part.Hi = (UINT32) -((INT32) Remainder.Part.Hi);
            Remainder.Part.Lo = (UINT32) -((INT32) Remainder.Part.Lo);

            if (Remainder.Part.Lo)
            {
                Remainder.Part.Hi--;
            }
        }
    }

    /* Return only what was requested */

    if (OutQuotient)
    {
        *OutQuotient = Quotient.Full;
    }
    if (OutRemainder)
    {
        *OutRemainder = Remainder.Full;
    }

    return_ACPI_STATUS (AE_OK);
}

#else

/*******************************************************************************
 *
 * FUNCTION:    AcpiUtShortDivide, AcpiUtDivide
 *
 * PARAMETERS:  See function headers above
 *
 * DESCRIPTION: Native versions of the UtDivide functions. Use these if either
 *              1) The target is a 64-bit platform and therefore 64-bit
 *                 integer math is supported directly by the machine.
 *              2) The target is a 32-bit or 16-bit platform, and the
 *                 double-precision integer math library is available to
 *                 perform the divide.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtShortDivide (
    UINT64                  InDividend,
    UINT32                  Divisor,
    UINT64                  *OutQuotient,
    UINT32                  *OutRemainder)
{

    ACPI_FUNCTION_TRACE (UtShortDivide);


    /* Always check for a zero divisor */

    if (Divisor == 0)
    {
        ACPI_ERROR ((AE_INFO, "Divide by zero"));
        return_ACPI_STATUS (AE_AML_DIVIDE_BY_ZERO);
    }

    /* Return only what was requested */

    if (OutQuotient)
    {
        *OutQuotient = InDividend / Divisor;
    }
    if (OutRemainder)
    {
        *OutRemainder = (UINT32) (InDividend % Divisor);
    }

    return_ACPI_STATUS (AE_OK);
}

ACPI_STATUS
AcpiUtDivide (
    UINT64                  InDividend,
    UINT64                  InDivisor,
    UINT64                  *OutQuotient,
    UINT64                  *OutRemainder)
{
    ACPI_FUNCTION_TRACE (UtDivide);


    /* Always check for a zero divisor */

    if (InDivisor == 0)
    {
        ACPI_ERROR ((AE_INFO, "Divide by zero"));
        return_ACPI_STATUS (AE_AML_DIVIDE_BY_ZERO);
    }


    /* Return only what was requested */

    if (OutQuotient)
    {
        *OutQuotient = InDividend / InDivisor;
    }
    if (OutRemainder)
    {
        *OutRemainder = InDividend % InDivisor;
    }

    return_ACPI_STATUS (AE_OK);
}

#endif
