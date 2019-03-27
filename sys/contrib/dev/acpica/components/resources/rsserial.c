/*******************************************************************************
 *
 * Module Name: rsserial - GPIO/SerialBus resource descriptors
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
#include <contrib/dev/acpica/include/acresrc.h>

#define _COMPONENT          ACPI_RESOURCES
        ACPI_MODULE_NAME    ("rsserial")


/*******************************************************************************
 *
 * AcpiRsConvertGpio
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertGpio[18] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_GPIO,
                        ACPI_RS_SIZE (ACPI_RESOURCE_GPIO),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertGpio)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_GPIO,
                        sizeof (AML_RESOURCE_GPIO),
                        0},

    /*
     * These fields are contiguous in both the source and destination:
     * RevisionId
     * ConnectionType
     */
    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.Gpio.RevisionId),
                        AML_OFFSET (Gpio.RevisionId),
                        2},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Gpio.ProducerConsumer),
                        AML_OFFSET (Gpio.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Gpio.Shareable),
                        AML_OFFSET (Gpio.IntFlags),
                        3},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Gpio.WakeCapable),
                        AML_OFFSET (Gpio.IntFlags),
                        4},

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.Gpio.IoRestriction),
                        AML_OFFSET (Gpio.IntFlags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Gpio.Triggering),
                        AML_OFFSET (Gpio.IntFlags),
                        0},

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.Gpio.Polarity),
                        AML_OFFSET (Gpio.IntFlags),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.Gpio.PinConfig),
                        AML_OFFSET (Gpio.PinConfig),
                        1},

    /*
     * These fields are contiguous in both the source and destination:
     * DriveStrength
     * DebounceTimeout
     */
    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.Gpio.DriveStrength),
                        AML_OFFSET (Gpio.DriveStrength),
                        2},

    /* Pin Table */

    {ACPI_RSC_COUNT_GPIO_PIN, ACPI_RS_OFFSET (Data.Gpio.PinTableLength),
                        AML_OFFSET (Gpio.PinTableOffset),
                        AML_OFFSET (Gpio.ResSourceOffset)},

    {ACPI_RSC_MOVE_GPIO_PIN, ACPI_RS_OFFSET (Data.Gpio.PinTable),
                        AML_OFFSET (Gpio.PinTableOffset),
                        0},

    /* Resource Source */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.Gpio.ResourceSource.Index),
                        AML_OFFSET (Gpio.ResSourceIndex),
                        1},

    {ACPI_RSC_COUNT_GPIO_RES,  ACPI_RS_OFFSET (Data.Gpio.ResourceSource.StringLength),
                        AML_OFFSET (Gpio.ResSourceOffset),
                        AML_OFFSET (Gpio.VendorOffset)},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.Gpio.ResourceSource.StringPtr),
                        AML_OFFSET (Gpio.ResSourceOffset),
                        0},

    /* Vendor Data */

    {ACPI_RSC_COUNT_GPIO_VEN,   ACPI_RS_OFFSET (Data.Gpio.VendorLength),
                        AML_OFFSET (Gpio.VendorLength),
                        1},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.Gpio.VendorData),
                        AML_OFFSET (Gpio.VendorOffset),
                        0},
};

/*******************************************************************************
 *
 * AcpiRsConvertPinfunction
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertPinFunction[13] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_PIN_FUNCTION,
                        ACPI_RS_SIZE (ACPI_RESOURCE_PIN_FUNCTION),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertPinFunction)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_PIN_FUNCTION,
                        sizeof (AML_RESOURCE_PIN_FUNCTION),
                        0},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinFunction.RevisionId),
                        AML_OFFSET (PinFunction.RevisionId),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.PinFunction.Shareable),
                        AML_OFFSET (PinFunction.Flags),
                        0},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinFunction.PinConfig),
                        AML_OFFSET (PinFunction.PinConfig),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.PinFunction.FunctionNumber),
                        AML_OFFSET (PinFunction.FunctionNumber),
                        2},

    /* Pin Table */

    /*
     * It is OK to use GPIO operations here because none of them refer GPIO
     * structures directly but instead use offsets given here.
     */

    {ACPI_RSC_COUNT_GPIO_PIN, ACPI_RS_OFFSET (Data.PinFunction.PinTableLength),
                        AML_OFFSET (PinFunction.PinTableOffset),
                        AML_OFFSET (PinFunction.ResSourceOffset)},

    {ACPI_RSC_MOVE_GPIO_PIN, ACPI_RS_OFFSET (Data.PinFunction.PinTable),
                        AML_OFFSET (PinFunction.PinTableOffset),
                        0},

    /* Resource Source */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinFunction.ResourceSource.Index),
                        AML_OFFSET (PinFunction.ResSourceIndex),
                        1},

    {ACPI_RSC_COUNT_GPIO_RES,  ACPI_RS_OFFSET (Data.PinFunction.ResourceSource.StringLength),
                        AML_OFFSET (PinFunction.ResSourceOffset),
                        AML_OFFSET (PinFunction.VendorOffset)},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinFunction.ResourceSource.StringPtr),
                        AML_OFFSET (PinFunction.ResSourceOffset),
                        0},

    /* Vendor Data */

    {ACPI_RSC_COUNT_GPIO_VEN,   ACPI_RS_OFFSET (Data.PinFunction.VendorLength),
                        AML_OFFSET (PinFunction.VendorLength),
                        1},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinFunction.VendorData),
                        AML_OFFSET (PinFunction.VendorOffset),
                        0},
};


/*******************************************************************************
 *
 * AcpiRsConvertI2cSerialBus
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertI2cSerialBus[17] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_SERIAL_BUS,
                        ACPI_RS_SIZE (ACPI_RESOURCE_I2C_SERIALBUS),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertI2cSerialBus)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_SERIAL_BUS,
                        sizeof (AML_RESOURCE_I2C_SERIALBUS),
                        0},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.RevisionId),
                        AML_OFFSET (CommonSerialBus.RevisionId),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.Type),
                        AML_OFFSET (CommonSerialBus.Type),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.CommonSerialBus.SlaveMode),
                        AML_OFFSET (CommonSerialBus.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.CommonSerialBus.ProducerConsumer),
                        AML_OFFSET (CommonSerialBus.Flags),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.CommonSerialBus.ConnectionSharing),
                        AML_OFFSET (CommonSerialBus.Flags),
                        2},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.TypeRevisionId),
                        AML_OFFSET (CommonSerialBus.TypeRevisionId),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.CommonSerialBus.TypeDataLength),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        1},

    /* Vendor data */

    {ACPI_RSC_COUNT_SERIAL_VEN, ACPI_RS_OFFSET (Data.CommonSerialBus.VendorLength),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        AML_RESOURCE_I2C_MIN_DATA_LEN},

    {ACPI_RSC_MOVE_SERIAL_VEN,  ACPI_RS_OFFSET (Data.CommonSerialBus.VendorData),
                        0,
                        sizeof (AML_RESOURCE_I2C_SERIALBUS)},

    /* Resource Source */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.ResourceSource.Index),
                        AML_OFFSET (CommonSerialBus.ResSourceIndex),
                        1},

    {ACPI_RSC_COUNT_SERIAL_RES, ACPI_RS_OFFSET (Data.CommonSerialBus.ResourceSource.StringLength),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        sizeof (AML_RESOURCE_COMMON_SERIALBUS)},

    {ACPI_RSC_MOVE_SERIAL_RES,  ACPI_RS_OFFSET (Data.CommonSerialBus.ResourceSource.StringPtr),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        sizeof (AML_RESOURCE_COMMON_SERIALBUS)},

    /* I2C bus type specific */

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.I2cSerialBus.AccessMode),
                        AML_OFFSET (I2cSerialBus.TypeSpecificFlags),
                        0},

    {ACPI_RSC_MOVE32,   ACPI_RS_OFFSET (Data.I2cSerialBus.ConnectionSpeed),
                        AML_OFFSET (I2cSerialBus.ConnectionSpeed),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.I2cSerialBus.SlaveAddress),
                        AML_OFFSET (I2cSerialBus.SlaveAddress),
                        1},
};


/*******************************************************************************
 *
 * AcpiRsConvertSpiSerialBus
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertSpiSerialBus[21] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_SERIAL_BUS,
                        ACPI_RS_SIZE (ACPI_RESOURCE_SPI_SERIALBUS),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertSpiSerialBus)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_SERIAL_BUS,
                        sizeof (AML_RESOURCE_SPI_SERIALBUS),
                        0},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.RevisionId),
                        AML_OFFSET (CommonSerialBus.RevisionId),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.Type),
                        AML_OFFSET (CommonSerialBus.Type),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.CommonSerialBus.SlaveMode),
                        AML_OFFSET (CommonSerialBus.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.CommonSerialBus.ProducerConsumer),
                        AML_OFFSET (CommonSerialBus.Flags),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.CommonSerialBus.ConnectionSharing),
                        AML_OFFSET (CommonSerialBus.Flags),
                        2},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.TypeRevisionId),
                        AML_OFFSET (CommonSerialBus.TypeRevisionId),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.CommonSerialBus.TypeDataLength),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        1},

    /* Vendor data */

    {ACPI_RSC_COUNT_SERIAL_VEN, ACPI_RS_OFFSET (Data.CommonSerialBus.VendorLength),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        AML_RESOURCE_SPI_MIN_DATA_LEN},

    {ACPI_RSC_MOVE_SERIAL_VEN,  ACPI_RS_OFFSET (Data.CommonSerialBus.VendorData),
                        0,
                        sizeof (AML_RESOURCE_SPI_SERIALBUS)},

    /* Resource Source */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.ResourceSource.Index),
                        AML_OFFSET (CommonSerialBus.ResSourceIndex),
                        1},

    {ACPI_RSC_COUNT_SERIAL_RES, ACPI_RS_OFFSET (Data.CommonSerialBus.ResourceSource.StringLength),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        sizeof (AML_RESOURCE_COMMON_SERIALBUS)},

    {ACPI_RSC_MOVE_SERIAL_RES,  ACPI_RS_OFFSET (Data.CommonSerialBus.ResourceSource.StringPtr),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        sizeof (AML_RESOURCE_COMMON_SERIALBUS)},

    /* Spi bus type specific  */

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.SpiSerialBus.WireMode),
                        AML_OFFSET (SpiSerialBus.TypeSpecificFlags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.SpiSerialBus.DevicePolarity),
                        AML_OFFSET (SpiSerialBus.TypeSpecificFlags),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.SpiSerialBus.DataBitLength),
                        AML_OFFSET (SpiSerialBus.DataBitLength),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.SpiSerialBus.ClockPhase),
                        AML_OFFSET (SpiSerialBus.ClockPhase),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.SpiSerialBus.ClockPolarity),
                        AML_OFFSET (SpiSerialBus.ClockPolarity),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.SpiSerialBus.DeviceSelection),
                        AML_OFFSET (SpiSerialBus.DeviceSelection),
                        1},

    {ACPI_RSC_MOVE32,   ACPI_RS_OFFSET (Data.SpiSerialBus.ConnectionSpeed),
                        AML_OFFSET (SpiSerialBus.ConnectionSpeed),
                        1},
};


/*******************************************************************************
 *
 * AcpiRsConvertUartSerialBus
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertUartSerialBus[23] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_SERIAL_BUS,
                        ACPI_RS_SIZE (ACPI_RESOURCE_UART_SERIALBUS),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertUartSerialBus)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_SERIAL_BUS,
                        sizeof (AML_RESOURCE_UART_SERIALBUS),
                        0},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.RevisionId),
                        AML_OFFSET (CommonSerialBus.RevisionId),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.Type),
                        AML_OFFSET (CommonSerialBus.Type),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.CommonSerialBus.SlaveMode),
                        AML_OFFSET (CommonSerialBus.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.CommonSerialBus.ProducerConsumer),
                        AML_OFFSET (CommonSerialBus.Flags),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.CommonSerialBus.ConnectionSharing),
                        AML_OFFSET (CommonSerialBus.Flags),
                        2},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.TypeRevisionId),
                        AML_OFFSET (CommonSerialBus.TypeRevisionId),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.CommonSerialBus.TypeDataLength),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        1},

    /* Vendor data */

    {ACPI_RSC_COUNT_SERIAL_VEN, ACPI_RS_OFFSET (Data.CommonSerialBus.VendorLength),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        AML_RESOURCE_UART_MIN_DATA_LEN},

    {ACPI_RSC_MOVE_SERIAL_VEN,  ACPI_RS_OFFSET (Data.CommonSerialBus.VendorData),
                        0,
                        sizeof (AML_RESOURCE_UART_SERIALBUS)},

    /* Resource Source */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.CommonSerialBus.ResourceSource.Index),
                        AML_OFFSET (CommonSerialBus.ResSourceIndex),
                        1},

    {ACPI_RSC_COUNT_SERIAL_RES, ACPI_RS_OFFSET (Data.CommonSerialBus.ResourceSource.StringLength),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        sizeof (AML_RESOURCE_COMMON_SERIALBUS)},

    {ACPI_RSC_MOVE_SERIAL_RES,  ACPI_RS_OFFSET (Data.CommonSerialBus.ResourceSource.StringPtr),
                        AML_OFFSET (CommonSerialBus.TypeDataLength),
                        sizeof (AML_RESOURCE_COMMON_SERIALBUS)},

    /* Uart bus type specific  */

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.UartSerialBus.FlowControl),
                        AML_OFFSET (UartSerialBus.TypeSpecificFlags),
                        0},

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.UartSerialBus.StopBits),
                        AML_OFFSET (UartSerialBus.TypeSpecificFlags),
                        2},

    {ACPI_RSC_3BITFLAG, ACPI_RS_OFFSET (Data.UartSerialBus.DataBits),
                        AML_OFFSET (UartSerialBus.TypeSpecificFlags),
                        4},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.UartSerialBus.Endian),
                        AML_OFFSET (UartSerialBus.TypeSpecificFlags),
                        7},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.UartSerialBus.Parity),
                        AML_OFFSET (UartSerialBus.Parity),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.UartSerialBus.LinesEnabled),
                        AML_OFFSET (UartSerialBus.LinesEnabled),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.UartSerialBus.RxFifoSize),
                        AML_OFFSET (UartSerialBus.RxFifoSize),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.UartSerialBus.TxFifoSize),
                        AML_OFFSET (UartSerialBus.TxFifoSize),
                        1},

    {ACPI_RSC_MOVE32,   ACPI_RS_OFFSET (Data.UartSerialBus.DefaultBaudRate),
                        AML_OFFSET (UartSerialBus.DefaultBaudRate),
                        1},
};


/*******************************************************************************
 *
 * AcpiRsConvertPinConfig
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertPinConfig[14] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_PIN_CONFIG,
                        ACPI_RS_SIZE (ACPI_RESOURCE_PIN_CONFIG),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertPinConfig)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_PIN_CONFIG,
                        sizeof (AML_RESOURCE_PIN_CONFIG),
                        0},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinConfig.RevisionId),
                        AML_OFFSET (PinConfig.RevisionId),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.PinConfig.Shareable),
                        AML_OFFSET (PinConfig.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.PinConfig.ProducerConsumer),
                        AML_OFFSET (PinConfig.Flags),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinConfig.PinConfigType),
                        AML_OFFSET (PinConfig.PinConfigType),
                        1},

    {ACPI_RSC_MOVE32,   ACPI_RS_OFFSET (Data.PinConfig.PinConfigValue),
                        AML_OFFSET (PinConfig.PinConfigValue),
                        1},

    /* Pin Table */

    /*
     * It is OK to use GPIO operations here because none of them refer GPIO
     * structures directly but instead use offsets given here.
     */

    {ACPI_RSC_COUNT_GPIO_PIN, ACPI_RS_OFFSET (Data.PinConfig.PinTableLength),
                        AML_OFFSET (PinConfig.PinTableOffset),
                        AML_OFFSET (PinConfig.ResSourceOffset)},

    {ACPI_RSC_MOVE_GPIO_PIN, ACPI_RS_OFFSET (Data.PinConfig.PinTable),
                        AML_OFFSET (PinConfig.PinTableOffset),
                        0},

    /* Resource Source */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinConfig.ResourceSource.Index),
                        AML_OFFSET (PinConfig.ResSourceIndex),
                        1},

    {ACPI_RSC_COUNT_GPIO_RES,  ACPI_RS_OFFSET (Data.PinConfig.ResourceSource.StringLength),
                        AML_OFFSET (PinConfig.ResSourceOffset),
                        AML_OFFSET (PinConfig.VendorOffset)},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinConfig.ResourceSource.StringPtr),
                        AML_OFFSET (PinConfig.ResSourceOffset),
                        0},

    /* Vendor Data */

    {ACPI_RSC_COUNT_GPIO_VEN,   ACPI_RS_OFFSET (Data.PinConfig.VendorLength),
                        AML_OFFSET (PinConfig.VendorLength),
                        1},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinConfig.VendorData),
                        AML_OFFSET (PinConfig.VendorOffset),
                        0},
};

/*******************************************************************************
 *
 * AcpiRsConvertPinGroup
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertPinGroup[10] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_PIN_GROUP,
                        ACPI_RS_SIZE (ACPI_RESOURCE_PIN_GROUP),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertPinGroup)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_PIN_GROUP,
                        sizeof (AML_RESOURCE_PIN_GROUP),
                        0},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinGroup.RevisionId),
                        AML_OFFSET (PinGroup.RevisionId),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.PinGroup.ProducerConsumer),
                        AML_OFFSET (PinGroup.Flags),
                        0},

    /* Pin Table */

    /*
     * It is OK to use GPIO operations here because none of them refer GPIO
     * structures directly but instead use offsets given here.
     */

    {ACPI_RSC_COUNT_GPIO_PIN, ACPI_RS_OFFSET (Data.PinGroup.PinTableLength),
                        AML_OFFSET (PinGroup.PinTableOffset),
                        AML_OFFSET (PinGroup.LabelOffset)},

    {ACPI_RSC_MOVE_GPIO_PIN, ACPI_RS_OFFSET (Data.PinGroup.PinTable),
                        AML_OFFSET (PinGroup.PinTableOffset),
                        0},

    /* Resource Label */

    {ACPI_RSC_COUNT_GPIO_RES, ACPI_RS_OFFSET (Data.PinGroup.ResourceLabel.StringLength),
                        AML_OFFSET (PinGroup.LabelOffset),
                        AML_OFFSET (PinGroup.VendorOffset)},

    {ACPI_RSC_MOVE_GPIO_RES, ACPI_RS_OFFSET (Data.PinGroup.ResourceLabel.StringPtr),
                        AML_OFFSET (PinGroup.LabelOffset),
                        0},

    /* Vendor Data */

    {ACPI_RSC_COUNT_GPIO_VEN,   ACPI_RS_OFFSET (Data.PinGroup.VendorLength),
                        AML_OFFSET (PinGroup.VendorLength),
                        1},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinGroup.VendorData),
                        AML_OFFSET (PinGroup.VendorOffset),
                        0},
};

/*******************************************************************************
 *
 * AcpiRsConvertPinGroupFunction
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertPinGroupFunction[13] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_PIN_GROUP_FUNCTION,
                        ACPI_RS_SIZE (ACPI_RESOURCE_PIN_GROUP_FUNCTION),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertPinGroupFunction)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_PIN_GROUP_FUNCTION,
                        sizeof (AML_RESOURCE_PIN_GROUP_FUNCTION),
                        0},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinGroupFunction.RevisionId),
                        AML_OFFSET (PinGroupFunction.RevisionId),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.PinGroupFunction.Shareable),
                        AML_OFFSET (PinGroupFunction.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.PinGroupFunction.ProducerConsumer),
                        AML_OFFSET (PinGroupFunction.Flags),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.PinGroupFunction.FunctionNumber),
                        AML_OFFSET (PinGroupFunction.FunctionNumber),
                        1},

    /* Resource Source */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinGroupFunction.ResourceSource.Index),
                        AML_OFFSET (PinGroupFunction.ResSourceIndex),
                        1},

    {ACPI_RSC_COUNT_GPIO_RES,  ACPI_RS_OFFSET (Data.PinGroupFunction.ResourceSource.StringLength),
                        AML_OFFSET (PinGroupFunction.ResSourceOffset),
                        AML_OFFSET (PinGroupFunction.ResSourceLabelOffset)},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinGroupFunction.ResourceSource.StringPtr),
                        AML_OFFSET (PinGroupFunction.ResSourceOffset),
                        0},

    /* Resource Source Label */

    {ACPI_RSC_COUNT_GPIO_RES,  ACPI_RS_OFFSET (Data.PinGroupFunction.ResourceSourceLabel.StringLength),
                        AML_OFFSET (PinGroupFunction.ResSourceLabelOffset),
                        AML_OFFSET (PinGroupFunction.VendorOffset)},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinGroupFunction.ResourceSourceLabel.StringPtr),
                        AML_OFFSET (PinGroupFunction.ResSourceLabelOffset),
                        0},

    /* Vendor Data */

    {ACPI_RSC_COUNT_GPIO_VEN,   ACPI_RS_OFFSET (Data.PinGroupFunction.VendorLength),
                        AML_OFFSET (PinGroupFunction.VendorLength),
                        1},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinGroupFunction.VendorData),
                        AML_OFFSET (PinGroupFunction.VendorOffset),
                        0},
};

/*******************************************************************************
 *
 * AcpiRsConvertPinGroupConfig
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertPinGroupConfig[14] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_PIN_GROUP_CONFIG,
                        ACPI_RS_SIZE (ACPI_RESOURCE_PIN_GROUP_CONFIG),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertPinGroupConfig)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_PIN_GROUP_CONFIG,
                        sizeof (AML_RESOURCE_PIN_GROUP_CONFIG),
                        0},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinGroupConfig.RevisionId),
                        AML_OFFSET (PinGroupConfig.RevisionId),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.PinGroupConfig.Shareable),
                        AML_OFFSET (PinGroupConfig.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.PinGroupConfig.ProducerConsumer),
                        AML_OFFSET (PinGroupConfig.Flags),
                        1},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinGroupConfig.PinConfigType),
                        AML_OFFSET (PinGroupConfig.PinConfigType),
                        1},

    {ACPI_RSC_MOVE32,   ACPI_RS_OFFSET (Data.PinGroupConfig.PinConfigValue),
                        AML_OFFSET (PinGroupConfig.PinConfigValue),
                        1},

    /* Resource Source */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.PinGroupConfig.ResourceSource.Index),
                        AML_OFFSET (PinGroupConfig.ResSourceIndex),
                        1},

    {ACPI_RSC_COUNT_GPIO_RES,  ACPI_RS_OFFSET (Data.PinGroupConfig.ResourceSource.StringLength),
                        AML_OFFSET (PinGroupConfig.ResSourceOffset),
                        AML_OFFSET (PinGroupConfig.ResSourceLabelOffset)},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinGroupConfig.ResourceSource.StringPtr),
                        AML_OFFSET (PinGroupConfig.ResSourceOffset),
                        0},

    /* Resource Source Label */

    {ACPI_RSC_COUNT_GPIO_RES,  ACPI_RS_OFFSET (Data.PinGroupConfig.ResourceSourceLabel.StringLength),
                        AML_OFFSET (PinGroupConfig.ResSourceLabelOffset),
                        AML_OFFSET (PinGroupConfig.VendorOffset)},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinGroupConfig.ResourceSourceLabel.StringPtr),
                        AML_OFFSET (PinGroupConfig.ResSourceLabelOffset),
                        0},

    /* Vendor Data */

    {ACPI_RSC_COUNT_GPIO_VEN,   ACPI_RS_OFFSET (Data.PinGroupConfig.VendorLength),
                        AML_OFFSET (PinGroupConfig.VendorLength),
                        1},

    {ACPI_RSC_MOVE_GPIO_RES,   ACPI_RS_OFFSET (Data.PinGroupConfig.VendorData),
                        AML_OFFSET (PinGroupConfig.VendorOffset),
                        0},
};
