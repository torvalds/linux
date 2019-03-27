/******************************************************************************
 *
 * Module Name: hwvalid - I/O request validation
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

#define _COMPONENT          ACPI_HARDWARE
        ACPI_MODULE_NAME    ("hwvalid")

/* Local prototypes */

static ACPI_STATUS
AcpiHwValidateIoRequest (
    ACPI_IO_ADDRESS         Address,
    UINT32                  BitWidth);


/*
 * Protected I/O ports. Some ports are always illegal, and some are
 * conditionally illegal. This table must remain ordered by port address.
 *
 * The table is used to implement the Microsoft port access rules that
 * first appeared in Windows XP. Some ports are always illegal, and some
 * ports are only illegal if the BIOS calls _OSI with a WinXP string or
 * later (meaning that the BIOS itelf is post-XP.)
 *
 * This provides ACPICA with the desired port protections and
 * Microsoft compatibility.
 *
 * Description of port entries:
 *  DMA:   DMA controller
 *  PIC0:  Programmable Interrupt Controller (8259A)
 *  PIT1:  System Timer 1
 *  PIT2:  System Timer 2 failsafe
 *  RTC:   Real-time clock
 *  CMOS:  Extended CMOS
 *  DMA1:  DMA 1 page registers
 *  DMA1L: DMA 1 Ch 0 low page
 *  DMA2:  DMA 2 page registers
 *  DMA2L: DMA 2 low page refresh
 *  ARBC:  Arbitration control
 *  SETUP: Reserved system board setup
 *  POS:   POS channel select
 *  PIC1:  Cascaded PIC
 *  IDMA:  ISA DMA
 *  ELCR:  PIC edge/level registers
 *  PCI:   PCI configuration space
 */
static const ACPI_PORT_INFO     AcpiProtectedPorts[] =
{
    {"DMA",     0x0000, 0x000F, ACPI_OSI_WIN_XP},
    {"PIC0",    0x0020, 0x0021, ACPI_ALWAYS_ILLEGAL},
    {"PIT1",    0x0040, 0x0043, ACPI_OSI_WIN_XP},
    {"PIT2",    0x0048, 0x004B, ACPI_OSI_WIN_XP},
    {"RTC",     0x0070, 0x0071, ACPI_OSI_WIN_XP},
    {"CMOS",    0x0074, 0x0076, ACPI_OSI_WIN_XP},
    {"DMA1",    0x0081, 0x0083, ACPI_OSI_WIN_XP},
    {"DMA1L",   0x0087, 0x0087, ACPI_OSI_WIN_XP},
    {"DMA2",    0x0089, 0x008B, ACPI_OSI_WIN_XP},
    {"DMA2L",   0x008F, 0x008F, ACPI_OSI_WIN_XP},
    {"ARBC",    0x0090, 0x0091, ACPI_OSI_WIN_XP},
    {"SETUP",   0x0093, 0x0094, ACPI_OSI_WIN_XP},
    {"POS",     0x0096, 0x0097, ACPI_OSI_WIN_XP},
    {"PIC1",    0x00A0, 0x00A1, ACPI_ALWAYS_ILLEGAL},
    {"IDMA",    0x00C0, 0x00DF, ACPI_OSI_WIN_XP},
    {"ELCR",    0x04D0, 0x04D1, ACPI_ALWAYS_ILLEGAL},
    {"PCI",     0x0CF8, 0x0CFF, ACPI_OSI_WIN_XP}
};

#define ACPI_PORT_INFO_ENTRIES      ACPI_ARRAY_LENGTH (AcpiProtectedPorts)


/******************************************************************************
 *
 * FUNCTION:    AcpiHwValidateIoRequest
 *
 * PARAMETERS:  Address             Address of I/O port/register
 *              BitWidth            Number of bits (8,16,32)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Validates an I/O request (address/length). Certain ports are
 *              always illegal and some ports are only illegal depending on
 *              the requests the BIOS AML code makes to the predefined
 *              _OSI method.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiHwValidateIoRequest (
    ACPI_IO_ADDRESS         Address,
    UINT32                  BitWidth)
{
    UINT32                  i;
    UINT32                  ByteWidth;
    ACPI_IO_ADDRESS         LastAddress;
    const ACPI_PORT_INFO    *PortInfo;


    ACPI_FUNCTION_TRACE (HwValidateIoRequest);


    /* Supported widths are 8/16/32 */

    if ((BitWidth != 8) &&
        (BitWidth != 16) &&
        (BitWidth != 32))
    {
        ACPI_ERROR ((AE_INFO,
            "Bad BitWidth parameter: %8.8X", BitWidth));
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    PortInfo = AcpiProtectedPorts;
    ByteWidth = ACPI_DIV_8 (BitWidth);
    LastAddress = Address + ByteWidth - 1;

    ACPI_DEBUG_PRINT ((ACPI_DB_IO,
        "Address %8.8X%8.8X LastAddress %8.8X%8.8X Length %X",
        ACPI_FORMAT_UINT64 (Address), ACPI_FORMAT_UINT64 (LastAddress),
        ByteWidth));

    /* Maximum 16-bit address in I/O space */

    if (LastAddress > ACPI_UINT16_MAX)
    {
        ACPI_ERROR ((AE_INFO,
            "Illegal I/O port address/length above 64K: %8.8X%8.8X/0x%X",
            ACPI_FORMAT_UINT64 (Address), ByteWidth));
        return_ACPI_STATUS (AE_LIMIT);
    }

    /* Exit if requested address is not within the protected port table */

    if (Address > AcpiProtectedPorts[ACPI_PORT_INFO_ENTRIES - 1].End)
    {
        return_ACPI_STATUS (AE_OK);
    }

    /* Check request against the list of protected I/O ports */

    for (i = 0; i < ACPI_PORT_INFO_ENTRIES; i++, PortInfo++)
    {
        /*
         * Check if the requested address range will write to a reserved
         * port. There are four cases to consider:
         *
         * 1) Address range is contained completely in the port address range
         * 2) Address range overlaps port range at the port range start
         * 3) Address range overlaps port range at the port range end
         * 4) Address range completely encompasses the port range
         */
        if ((Address <= PortInfo->End) && (LastAddress >= PortInfo->Start))
        {
            /* Port illegality may depend on the _OSI calls made by the BIOS */

            if (AcpiGbl_OsiData >= PortInfo->OsiDependency)
            {
                ACPI_DEBUG_PRINT ((ACPI_DB_VALUES,
                    "Denied AML access to port 0x%8.8X%8.8X/%X (%s 0x%.4X-0x%.4X)\n",
                    ACPI_FORMAT_UINT64 (Address), ByteWidth, PortInfo->Name,
                    PortInfo->Start, PortInfo->End));

                return_ACPI_STATUS (AE_AML_ILLEGAL_ADDRESS);
            }
        }

        /* Finished if address range ends before the end of this port */

        if (LastAddress <= PortInfo->End)
        {
            break;
        }
    }

    return_ACPI_STATUS (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwReadPort
 *
 * PARAMETERS:  Address             Address of I/O port/register to read
 *              Value               Where value (data) is returned
 *              Width               Number of bits
 *
 * RETURN:      Status and value read from port
 *
 * DESCRIPTION: Read data from an I/O port or register. This is a front-end
 *              to AcpiOsReadPort that performs validation on both the port
 *              address and the length.
 *
 *****************************************************************************/

ACPI_STATUS
AcpiHwReadPort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  *Value,
    UINT32                  Width)
{
    ACPI_STATUS             Status;
    UINT32                  OneByte;
    UINT32                  i;


    /* Truncate address to 16 bits if requested */

    if (AcpiGbl_TruncateIoAddresses)
    {
        Address &= ACPI_UINT16_MAX;
    }

    /* Validate the entire request and perform the I/O */

    Status = AcpiHwValidateIoRequest (Address, Width);
    if (ACPI_SUCCESS (Status))
    {
        Status = AcpiOsReadPort (Address, Value, Width);
        return (Status);
    }

    if (Status != AE_AML_ILLEGAL_ADDRESS)
    {
        return (Status);
    }

    /*
     * There has been a protection violation within the request. Fall
     * back to byte granularity port I/O and ignore the failing bytes.
     * This provides compatibility with other ACPI implementations.
     */
    for (i = 0, *Value = 0; i < Width; i += 8)
    {
        /* Validate and read one byte */

        if (AcpiHwValidateIoRequest (Address, 8) == AE_OK)
        {
            Status = AcpiOsReadPort (Address, &OneByte, 8);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            *Value |= (OneByte << i);
        }

        Address++;
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiHwWritePort
 *
 * PARAMETERS:  Address             Address of I/O port/register to write
 *              Value               Value to write
 *              Width               Number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write data to an I/O port or register. This is a front-end
 *              to AcpiOsWritePort that performs validation on both the port
 *              address and the length.
 *
 *****************************************************************************/

ACPI_STATUS
AcpiHwWritePort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  Value,
    UINT32                  Width)
{
    ACPI_STATUS             Status;
    UINT32                  i;


    /* Truncate address to 16 bits if requested */

    if (AcpiGbl_TruncateIoAddresses)
    {
        Address &= ACPI_UINT16_MAX;
    }

    /* Validate the entire request and perform the I/O */

    Status = AcpiHwValidateIoRequest (Address, Width);
    if (ACPI_SUCCESS (Status))
    {
        Status = AcpiOsWritePort (Address, Value, Width);
        return (Status);
    }

    if (Status != AE_AML_ILLEGAL_ADDRESS)
    {
        return (Status);
    }

    /*
     * There has been a protection violation within the request. Fall
     * back to byte granularity port I/O and ignore the failing bytes.
     * This provides compatibility with other ACPI implementations.
     */
    for (i = 0; i < Width; i += 8)
    {
        /* Validate and write one byte */

        if (AcpiHwValidateIoRequest (Address, 8) == AE_OK)
        {
            Status = AcpiOsWritePort (Address, (Value >> i) & 0xFF, 8);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }
        }

        Address++;
    }

    return (AE_OK);
}
