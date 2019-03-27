/*******************************************************************************
 *
 * Module Name: dmresrcl2.c - "Large" Resource Descriptor disassembly (#2)
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
#include <contrib/dev/acpica/include/acdisasm.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dbresrcl2")

/* Local prototypes */

static void
AcpiDmI2cSerialBusDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level);

static void
AcpiDmSpiSerialBusDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level);

static void
AcpiDmUartSerialBusDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level);

static void
AcpiDmGpioCommon (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Level);

static void
AcpiDmDumpRawDataBuffer (
    UINT8                   *Buffer,
    UINT32                  Length,
    UINT32                  Level);


/* Dispatch table for the serial bus descriptors */

static ACPI_RESOURCE_HANDLER        SerialBusResourceDispatch [] =
{
    NULL,
    AcpiDmI2cSerialBusDescriptor,
    AcpiDmSpiSerialBusDescriptor,
    AcpiDmUartSerialBusDescriptor
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpRawDataBuffer
 *
 * PARAMETERS:  Buffer              - Pointer to the data bytes
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump a data buffer as a RawDataBuffer() object. Used for
 *              vendor data bytes.
 *
 ******************************************************************************/

static void
AcpiDmDumpRawDataBuffer (
    UINT8                   *Buffer,
    UINT32                  Length,
    UINT32                  Level)
{
    UINT32                  Index;
    UINT32                  i;
    UINT32                  j;


    if (!Length)
    {
        return;
    }

    AcpiOsPrintf ("RawDataBuffer (0x%.2X)  // Vendor Data", Length);

    AcpiOsPrintf ("\n");
    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("{\n");
    AcpiDmIndent (Level + 2);

    for (i = 0; i < Length;)
    {
        for (j = 0; j < 8; j++)
        {
            Index = i + j;
            if (Index >= Length)
            {
                goto Finish;
            }

            AcpiOsPrintf ("0x%2.2X", Buffer[Index]);
            if ((Index + 1) >= Length)
            {
                goto Finish;
            }

            AcpiOsPrintf (", ");
        }

        AcpiOsPrintf ("\n");
        AcpiDmIndent (Level + 2);

        i += 8;
    }

Finish:
    AcpiOsPrintf ("\n");
    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("}");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmGpioCommon
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode common parts of a GPIO Interrupt descriptor
 *
 ******************************************************************************/

static void
AcpiDmGpioCommon (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Level)
{
    UINT16                  *PinList;
    UINT8                   *VendorData;
    char                    *DeviceName = NULL;
    UINT32                  PinCount;
    UINT32                  i;


    /* ResourceSource, ResourceSourceIndex, ResourceType */

    AcpiDmIndent (Level + 1);
    if (Resource->Gpio.ResSourceOffset)
    {
        DeviceName = ACPI_ADD_PTR (char,
            Resource, Resource->Gpio.ResSourceOffset),
        AcpiUtPrintString (DeviceName, ACPI_UINT16_MAX);
    }

    AcpiOsPrintf (", ");
    AcpiOsPrintf ("0x%2.2X, ", Resource->Gpio.ResSourceIndex);
    AcpiOsPrintf ("%s, ",
        AcpiGbl_ConsumeDecode [ACPI_GET_1BIT_FLAG (Resource->Gpio.Flags)]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();
    AcpiOsPrintf (",");

    /* Dump the vendor data */

    if (Resource->Gpio.VendorOffset)
    {
        AcpiOsPrintf ("\n");
        AcpiDmIndent (Level + 1);
        VendorData = ACPI_ADD_PTR (UINT8, Resource,
            Resource->Gpio.VendorOffset);

        AcpiDmDumpRawDataBuffer (VendorData,
            Resource->Gpio.VendorLength, Level);
    }

    AcpiOsPrintf (")\n");

    /* Dump the interrupt list */

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("{   // Pin list\n");

    PinCount = ((UINT32) (Resource->Gpio.ResSourceOffset -
        Resource->Gpio.PinTableOffset)) /
        sizeof (UINT16);

    PinList = (UINT16 *) ACPI_ADD_PTR (char, Resource,
        Resource->Gpio.PinTableOffset);

    for (i = 0; i < PinCount; i++)
    {
        AcpiDmIndent (Level + 2);
        AcpiOsPrintf ("0x%4.4X%s\n", PinList[i],
            ((i + 1) < PinCount) ? "," : "");
    }

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("}\n");

#ifndef _KERNEL
    MpSaveGpioInfo (Info->MappingOp, Resource,
        PinCount, PinList, DeviceName);
#endif
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmGpioIntDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a GPIO Interrupt descriptor
 *
 ******************************************************************************/

static void
AcpiDmGpioIntDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    /* Dump the GpioInt-specific portion of the descriptor */

    /* EdgeLevel, ActiveLevel, Shared */

    AcpiDmIndent (Level);
    AcpiOsPrintf ("GpioInt (%s, %s, %s, ",
        AcpiGbl_HeDecode [ACPI_GET_1BIT_FLAG (Resource->Gpio.IntFlags)],
        AcpiGbl_LlDecode [ACPI_EXTRACT_2BIT_FLAG (Resource->Gpio.IntFlags, 1)],
        AcpiGbl_ShrDecode [ACPI_EXTRACT_2BIT_FLAG (Resource->Gpio.IntFlags, 3)]);

    /* PinConfig, DebounceTimeout */

    if (Resource->Gpio.PinConfig <= 3)
    {
        AcpiOsPrintf ("%s, ",
            AcpiGbl_PpcDecode[Resource->Gpio.PinConfig]);
    }
    else
    {
        AcpiOsPrintf ("0x%2.2X, ", Resource->Gpio.PinConfig);
    }
    AcpiOsPrintf ("0x%4.4X,\n", Resource->Gpio.DebounceTimeout);

    /* Dump the GpioInt/GpioIo common portion of the descriptor */

    AcpiDmGpioCommon (Info, Resource, Level);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmGpioIoDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a GPIO I/O descriptor
 *
 ******************************************************************************/

static void
AcpiDmGpioIoDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    /* Dump the GpioIo-specific portion of the descriptor */

    /* Shared, PinConfig */

    AcpiDmIndent (Level);
    AcpiOsPrintf ("GpioIo (%s, ",
        AcpiGbl_ShrDecode [ACPI_EXTRACT_2BIT_FLAG (Resource->Gpio.IntFlags, 3)]);

    if (Resource->Gpio.PinConfig <= 3)
    {
        AcpiOsPrintf ("%s, ",
            AcpiGbl_PpcDecode[Resource->Gpio.PinConfig]);
    }
    else
    {
        AcpiOsPrintf ("0x%2.2X, ", Resource->Gpio.PinConfig);
    }

    /* DebounceTimeout, DriveStrength, IoRestriction */

    AcpiOsPrintf ("0x%4.4X, ", Resource->Gpio.DebounceTimeout);
    AcpiOsPrintf ("0x%4.4X, ", Resource->Gpio.DriveStrength);
    AcpiOsPrintf ("%s,\n",
        AcpiGbl_IorDecode [ACPI_GET_2BIT_FLAG (Resource->Gpio.IntFlags)]);

    /* Dump the GpioInt/GpioIo common portion of the descriptor */

    AcpiDmGpioCommon (Info, Resource, Level);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmGpioDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a GpioInt/GpioIo GPIO Interrupt/IO descriptor
 *
 ******************************************************************************/

void
AcpiDmGpioDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{
    UINT8                   ConnectionType;


    ConnectionType = Resource->Gpio.ConnectionType;

    switch (ConnectionType)
    {
    case AML_RESOURCE_GPIO_TYPE_INT:

        AcpiDmGpioIntDescriptor (Info, Resource, Length, Level);
        break;

    case AML_RESOURCE_GPIO_TYPE_IO:

        AcpiDmGpioIoDescriptor (Info, Resource, Length, Level);
        break;

    default:

        AcpiOsPrintf ("Unknown GPIO type\n");
        break;
    }
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPinFunctionDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a PinFunction descriptor
 *
 ******************************************************************************/

void
AcpiDmPinFunctionDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{
    UINT16                  *PinList;
    UINT8                   *VendorData;
    char                    *DeviceName = NULL;
    UINT32                  PinCount;
    UINT32                  i;

    AcpiDmIndent (Level);
    AcpiOsPrintf ("PinFunction (%s, ",
        AcpiGbl_ShrDecode [ACPI_GET_1BIT_FLAG (Resource->PinFunction.Flags)]);

    if (Resource->PinFunction.PinConfig <= 3)
    {
        AcpiOsPrintf ("%s, ",
            AcpiGbl_PpcDecode[Resource->PinFunction.PinConfig]);
    }
    else
    {
        AcpiOsPrintf ("0x%2.2X, ", Resource->PinFunction.PinConfig);
    }

    /* FunctionNumber */

    AcpiOsPrintf ("0x%4.4X, ", Resource->PinFunction.FunctionNumber);

    if (Resource->PinFunction.ResSourceOffset)
    {
        DeviceName = ACPI_ADD_PTR (char,
            Resource, Resource->PinFunction.ResSourceOffset),
        AcpiUtPrintString (DeviceName, ACPI_UINT16_MAX);
    }

    AcpiOsPrintf (", ");
    AcpiOsPrintf ("0x%2.2X,\n", Resource->PinFunction.ResSourceIndex);

    AcpiDmIndent (Level + 1);

    /* Always ResourceConsumer */
    AcpiOsPrintf ("%s, ", AcpiGbl_ConsumeDecode [ACPI_CONSUMER]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();

    AcpiOsPrintf (",");

    /* Dump the vendor data */

    if (Resource->PinFunction.VendorLength)
    {
        AcpiOsPrintf ("\n");
        AcpiDmIndent (Level + 1);
        VendorData = ACPI_ADD_PTR (UINT8, Resource,
            Resource->PinFunction.VendorOffset);

        AcpiDmDumpRawDataBuffer (VendorData,
            Resource->PinFunction.VendorLength, Level);
    }

    AcpiOsPrintf (")\n");

    AcpiDmIndent (Level + 1);

    /* Dump the interrupt list */

    AcpiOsPrintf ("{   // Pin list\n");

    PinCount = ((UINT32) (Resource->PinFunction.ResSourceOffset -
        Resource->PinFunction.PinTableOffset)) /
        sizeof (UINT16);

    PinList = (UINT16 *) ACPI_ADD_PTR (char, Resource,
        Resource->PinFunction.PinTableOffset);

    for (i = 0; i < PinCount; i++)
    {
        AcpiDmIndent (Level + 2);
        AcpiOsPrintf ("0x%4.4X%s\n", PinList[i],
            ((i + 1) < PinCount) ? "," : "");
    }

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("}\n");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpSerialBusVendorData
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump optional serial bus vendor data
 *
 ******************************************************************************/

static void
AcpiDmDumpSerialBusVendorData (
    AML_RESOURCE            *Resource,
    UINT32                  Level)
{
    UINT8                   *VendorData;
    UINT32                  VendorLength;


    /* Get the (optional) vendor data and length */

    switch (Resource->CommonSerialBus.Type)
    {
    case AML_RESOURCE_I2C_SERIALBUSTYPE:

        VendorLength = Resource->CommonSerialBus.TypeDataLength -
            AML_RESOURCE_I2C_MIN_DATA_LEN;

        VendorData = ACPI_ADD_PTR (UINT8, Resource,
            sizeof (AML_RESOURCE_I2C_SERIALBUS));
        break;

    case AML_RESOURCE_SPI_SERIALBUSTYPE:

        VendorLength = Resource->CommonSerialBus.TypeDataLength -
            AML_RESOURCE_SPI_MIN_DATA_LEN;

        VendorData = ACPI_ADD_PTR (UINT8, Resource,
            sizeof (AML_RESOURCE_SPI_SERIALBUS));
        break;

    case AML_RESOURCE_UART_SERIALBUSTYPE:

        VendorLength = Resource->CommonSerialBus.TypeDataLength -
            AML_RESOURCE_UART_MIN_DATA_LEN;

        VendorData = ACPI_ADD_PTR (UINT8, Resource,
            sizeof (AML_RESOURCE_UART_SERIALBUS));
        break;

    default:

        return;
    }

    /* Dump the vendor bytes as a RawDataBuffer object */

    AcpiDmDumpRawDataBuffer (VendorData, VendorLength, Level);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmI2cSerialBusDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a I2C serial bus descriptor
 *
 ******************************************************************************/

static void
AcpiDmI2cSerialBusDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{
    UINT32                  ResourceSourceOffset;
    char                    *DeviceName;


    /* SlaveAddress, SlaveMode, ConnectionSpeed, AddressingMode */

    AcpiDmIndent (Level);
    AcpiOsPrintf ("I2cSerialBusV2 (0x%4.4X, %s, 0x%8.8X,\n",
        Resource->I2cSerialBus.SlaveAddress,
        AcpiGbl_SmDecode [ACPI_GET_1BIT_FLAG (Resource->I2cSerialBus.Flags)],
        Resource->I2cSerialBus.ConnectionSpeed);

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("%s, ",
        AcpiGbl_AmDecode [ACPI_GET_1BIT_FLAG (Resource->I2cSerialBus.TypeSpecificFlags)]);

    /* ResourceSource is a required field */

    ResourceSourceOffset = sizeof (AML_RESOURCE_COMMON_SERIALBUS) +
        Resource->CommonSerialBus.TypeDataLength;

    DeviceName = ACPI_ADD_PTR (char, Resource, ResourceSourceOffset);
    AcpiUtPrintString (DeviceName, ACPI_UINT16_MAX);

    /* ResourceSourceIndex, ResourceUsage */

    AcpiOsPrintf (",\n");
    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("0x%2.2X, ", Resource->I2cSerialBus.ResSourceIndex);

    AcpiOsPrintf ("%s, ",
        AcpiGbl_ConsumeDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->I2cSerialBus.Flags, 1)]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();

    /* Share */

    AcpiOsPrintf (", %s,\n",
        AcpiGbl_ShrDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->I2cSerialBus.Flags, 2)]);

    /* Dump the vendor data */

    AcpiDmIndent (Level + 1);
    AcpiDmDumpSerialBusVendorData (Resource, Level);
    AcpiOsPrintf (")\n");

#ifndef _KERNEL
    MpSaveSerialInfo (Info->MappingOp, Resource, DeviceName);
#endif
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmSpiSerialBusDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a SPI serial bus descriptor
 *
 ******************************************************************************/

static void
AcpiDmSpiSerialBusDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{
    UINT32                  ResourceSourceOffset;
    char                    *DeviceName;


    /* DeviceSelection, DeviceSelectionPolarity, WireMode, DataBitLength */

    AcpiDmIndent (Level);
    AcpiOsPrintf ("SpiSerialBusV2 (0x%4.4X, %s, %s, 0x%2.2X,\n",
        Resource->SpiSerialBus.DeviceSelection,
        AcpiGbl_DpDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->SpiSerialBus.TypeSpecificFlags, 1)],
        AcpiGbl_WmDecode [ACPI_GET_1BIT_FLAG (Resource->SpiSerialBus.TypeSpecificFlags)],
        Resource->SpiSerialBus.DataBitLength);

    /* SlaveMode, ConnectionSpeed, ClockPolarity, ClockPhase */

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("%s, 0x%8.8X, %s,\n",
        AcpiGbl_SmDecode [ACPI_GET_1BIT_FLAG (Resource->SpiSerialBus.Flags)],
        Resource->SpiSerialBus.ConnectionSpeed,
        AcpiGbl_CpoDecode [ACPI_GET_1BIT_FLAG (Resource->SpiSerialBus.ClockPolarity)]);

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("%s, ",
        AcpiGbl_CphDecode [ACPI_GET_1BIT_FLAG (Resource->SpiSerialBus.ClockPhase)]);

    /* ResourceSource is a required field */

    ResourceSourceOffset = sizeof (AML_RESOURCE_COMMON_SERIALBUS) +
        Resource->CommonSerialBus.TypeDataLength;

    DeviceName = ACPI_ADD_PTR (char, Resource, ResourceSourceOffset);
    AcpiUtPrintString (DeviceName, ACPI_UINT16_MAX);

    /* ResourceSourceIndex, ResourceUsage */

    AcpiOsPrintf (",\n");
    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("0x%2.2X, ", Resource->SpiSerialBus.ResSourceIndex);

    AcpiOsPrintf ("%s, ",
        AcpiGbl_ConsumeDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->SpiSerialBus.Flags, 1)]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();

    /* Share */

    AcpiOsPrintf (", %s,\n",
        AcpiGbl_ShrDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->SpiSerialBus.Flags, 2)]);

    /* Dump the vendor data */

    AcpiDmIndent (Level + 1);
    AcpiDmDumpSerialBusVendorData (Resource, Level);
    AcpiOsPrintf (")\n");

#ifndef _KERNEL
    MpSaveSerialInfo (Info->MappingOp, Resource, DeviceName);
#endif
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmUartSerialBusDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a UART serial bus descriptor
 *
 ******************************************************************************/

static void
AcpiDmUartSerialBusDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{
    UINT32                  ResourceSourceOffset;
    char                    *DeviceName;


    /* ConnectionSpeed, BitsPerByte, StopBits */

    AcpiDmIndent (Level);
    AcpiOsPrintf ("UartSerialBusV2 (0x%8.8X, %s, %s,\n",
        Resource->UartSerialBus.DefaultBaudRate,
        AcpiGbl_BpbDecode [ACPI_EXTRACT_3BIT_FLAG (Resource->UartSerialBus.TypeSpecificFlags, 4)],
        AcpiGbl_SbDecode [ACPI_EXTRACT_2BIT_FLAG (Resource->UartSerialBus.TypeSpecificFlags, 2)]);

    /* LinesInUse, IsBigEndian, Parity, FlowControl */

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("0x%2.2X, %s, %s, %s,\n",
        Resource->UartSerialBus.LinesEnabled,
        AcpiGbl_EdDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->UartSerialBus.TypeSpecificFlags, 7)],
        AcpiGbl_PtDecode [ACPI_GET_3BIT_FLAG (Resource->UartSerialBus.Parity)],
        AcpiGbl_FcDecode [ACPI_GET_2BIT_FLAG (Resource->UartSerialBus.TypeSpecificFlags)]);

    /* ReceiveBufferSize, TransmitBufferSize */

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("0x%4.4X, 0x%4.4X, ",
        Resource->UartSerialBus.RxFifoSize,
        Resource->UartSerialBus.TxFifoSize);

    /* ResourceSource is a required field */

    ResourceSourceOffset = sizeof (AML_RESOURCE_COMMON_SERIALBUS) +
        Resource->CommonSerialBus.TypeDataLength;

    DeviceName = ACPI_ADD_PTR (char, Resource, ResourceSourceOffset);
    AcpiUtPrintString (DeviceName, ACPI_UINT16_MAX);

    /* ResourceSourceIndex, ResourceUsage */

    AcpiOsPrintf (",\n");
    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("0x%2.2X, ", Resource->UartSerialBus.ResSourceIndex);

    AcpiOsPrintf ("%s, ",
        AcpiGbl_ConsumeDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->UartSerialBus.Flags, 1)]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();

    /* Share */

    AcpiOsPrintf (", %s,\n",
        AcpiGbl_ShrDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->UartSerialBus.Flags, 2)]);

    /* Dump the vendor data */

    AcpiDmIndent (Level + 1);
    AcpiDmDumpSerialBusVendorData (Resource, Level);
    AcpiOsPrintf (")\n");

#ifndef _KERNEL
    MpSaveSerialInfo (Info->MappingOp, Resource, DeviceName);
#endif
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmSerialBusDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a I2C/SPI/UART serial bus descriptor
 *
 ******************************************************************************/

void
AcpiDmSerialBusDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    SerialBusResourceDispatch [Resource->CommonSerialBus.Type] (
        Info, Resource, Length, Level);
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPinConfig
 *
 * PARAMETERS:  PinConfigType       - Pin configuration type
 *              PinConfigValue      - Pin configuration value
 *
 * RETURN:      None
 *
 * DESCRIPTION: Pretty prints PinConfig type and value.
 *
 ******************************************************************************/

static void
AcpiDmPinConfig(
    UINT8                   PinConfigType,
    UINT32                  PinConfigValue)
{
    if (PinConfigType <= 13)
    {
        AcpiOsPrintf ("0x%2.2X /* %s */, ", PinConfigType,
            AcpiGbl_PtypDecode[PinConfigType]);
    }
    else
    {
        AcpiOsPrintf ("0x%2.2X, /* Vendor Defined */ ", PinConfigType);
    }

    /* PinConfigValue */

    AcpiOsPrintf ("0x%4.4X,\n", PinConfigValue);
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPinConfigDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a PinConfig descriptor
 *
 ******************************************************************************/

void
AcpiDmPinConfigDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{
    UINT16                  *PinList;
    UINT8                   *VendorData;
    char                    *DeviceName = NULL;
    UINT32                  PinCount;
    UINT32                  i;

    AcpiDmIndent (Level);
    AcpiOsPrintf ("PinConfig (%s, ",
        AcpiGbl_ShrDecode [ACPI_GET_1BIT_FLAG (Resource->PinConfig.Flags)]);

    AcpiDmPinConfig (Resource->PinConfig.PinConfigType,
        Resource->PinConfig.PinConfigValue);

    AcpiDmIndent (Level + 1);

    if (Resource->PinConfig.ResSourceOffset)
    {
        DeviceName = ACPI_ADD_PTR (char,
            Resource, Resource->PinConfig.ResSourceOffset),
        AcpiUtPrintString (DeviceName, ACPI_UINT16_MAX);
    }

    AcpiOsPrintf (", ");
    AcpiOsPrintf ("0x%2.2X, ", Resource->PinConfig.ResSourceIndex);

    AcpiOsPrintf ("%s, ",
        AcpiGbl_ConsumeDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->PinConfig.Flags, 1)]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();

    AcpiOsPrintf (",");

    /* Dump the vendor data */

    if (Resource->PinConfig.VendorLength)
    {
        AcpiOsPrintf ("\n");
        AcpiDmIndent (Level + 1);
        VendorData = ACPI_ADD_PTR (UINT8, Resource,
            Resource->PinConfig.VendorOffset);

        AcpiDmDumpRawDataBuffer (VendorData,
            Resource->PinConfig.VendorLength, Level);
    }

    AcpiOsPrintf (")\n");

    AcpiDmIndent (Level + 1);

    /* Dump the interrupt list */

    AcpiOsPrintf ("{   // Pin list\n");

    PinCount = ((UINT32) (Resource->PinConfig.ResSourceOffset -
        Resource->PinConfig.PinTableOffset)) /
        sizeof (UINT16);

    PinList = (UINT16 *) ACPI_ADD_PTR (char, Resource,
        Resource->PinConfig.PinTableOffset);

    for (i = 0; i < PinCount; i++)
    {
        AcpiDmIndent (Level + 2);
        AcpiOsPrintf ("0x%4.4X%s\n", PinList[i],
            ((i + 1) < PinCount) ? "," : "");
    }

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("}\n");
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPinGroupDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a PinGroup descriptor
 *
 ******************************************************************************/

void
AcpiDmPinGroupDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{
    char                    *Label;
    UINT16                  *PinList;
    UINT8                   *VendorData;
    UINT32                  PinCount;
    UINT32                  i;

    AcpiDmIndent (Level);
    /* Always producer */
    AcpiOsPrintf ("PinGroup (");

    Label = ACPI_ADD_PTR (char,
        Resource, Resource->PinGroup.LabelOffset),
    AcpiUtPrintString (Label, ACPI_UINT16_MAX);

    AcpiOsPrintf (", ");

    AcpiOsPrintf ("%s, ",
        AcpiGbl_ConsumeDecode [ACPI_GET_1BIT_FLAG (Resource->PinGroup.Flags)]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();

    AcpiOsPrintf (",");

    /* Dump the vendor data */

    if (Resource->PinGroup.VendorLength)
    {
        AcpiOsPrintf ("\n");
        AcpiDmIndent (Level + 1);
        VendorData = ACPI_ADD_PTR (UINT8, Resource,
            Resource->PinGroup.VendorOffset);

        AcpiDmDumpRawDataBuffer (VendorData,
            Resource->PinGroup.VendorLength, Level);
    }

    AcpiOsPrintf (")\n");

    AcpiDmIndent (Level + 1);

    /* Dump the interrupt list */

    AcpiOsPrintf ("{   // Pin list\n");

    PinCount = (Resource->PinGroup.LabelOffset -
        Resource->PinGroup.PinTableOffset) / sizeof (UINT16);

    PinList = (UINT16 *) ACPI_ADD_PTR (char, Resource,
        Resource->PinGroup.PinTableOffset);

    for (i = 0; i < PinCount; i++)
    {
        AcpiDmIndent (Level + 2);
        AcpiOsPrintf ("0x%4.4X%s\n", PinList[i],
            ((i + 1) < PinCount) ? "," : "");
    }

    AcpiDmIndent (Level + 1);
    AcpiOsPrintf ("}\n");
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPinGroupFunctionDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a PinGroupFunction descriptor
 *
 ******************************************************************************/

void
AcpiDmPinGroupFunctionDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{
    UINT8                   *VendorData;
    char                    *DeviceName = NULL;
    char                    *Label = NULL;

    AcpiDmIndent (Level);
    AcpiOsPrintf ("PinGroupFunction (%s, ",
        AcpiGbl_ShrDecode [ACPI_GET_1BIT_FLAG (Resource->PinGroupFunction.Flags)]);

    /* FunctionNumber */

    AcpiOsPrintf ("0x%4.4X, ", Resource->PinGroupFunction.FunctionNumber);

    DeviceName = ACPI_ADD_PTR (char,
        Resource, Resource->PinGroupFunction.ResSourceOffset),
    AcpiUtPrintString (DeviceName, ACPI_UINT16_MAX);

    AcpiOsPrintf (", ");
    AcpiOsPrintf ("0x%2.2X,\n", Resource->PinGroupFunction.ResSourceIndex);

    AcpiDmIndent (Level + 1);

    Label = ACPI_ADD_PTR (char, Resource,
        Resource->PinGroupFunction.ResSourceLabelOffset);
    AcpiUtPrintString (Label, ACPI_UINT16_MAX);

    AcpiOsPrintf (", ");

    AcpiOsPrintf ("%s, ",
        AcpiGbl_ConsumeDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->PinGroupFunction.Flags, 1)]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();

    AcpiOsPrintf (",");

    /* Dump the vendor data */

    if (Resource->PinGroupFunction.VendorLength)
    {
        AcpiOsPrintf ("\n");
        AcpiDmIndent (Level + 1);
        VendorData = ACPI_ADD_PTR (UINT8, Resource,
            Resource->PinGroupFunction.VendorOffset);

        AcpiDmDumpRawDataBuffer (VendorData,
            Resource->PinGroupFunction.VendorLength, Level);
    }

    AcpiOsPrintf (")\n");
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPinGroupConfigDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a PinGroupConfig descriptor
 *
 ******************************************************************************/

void
AcpiDmPinGroupConfigDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{
    UINT8                   *VendorData;
    char                    *DeviceName = NULL;
    char                    *Label = NULL;

    AcpiDmIndent (Level);
    AcpiOsPrintf ("PinGroupConfig (%s, ",
        AcpiGbl_ShrDecode [ACPI_GET_1BIT_FLAG (Resource->PinGroupConfig.Flags)]);

    AcpiDmPinConfig(Resource->PinGroupConfig.PinConfigType,
        Resource->PinGroupConfig.PinConfigValue);

    AcpiDmIndent (Level + 1);

    DeviceName = ACPI_ADD_PTR (char,
        Resource, Resource->PinGroupConfig.ResSourceOffset),
    AcpiUtPrintString (DeviceName, ACPI_UINT16_MAX);

    AcpiOsPrintf (", ");
    AcpiOsPrintf ("0x%2.2X, ", Resource->PinGroupConfig.ResSourceIndex);

    Label = ACPI_ADD_PTR (char, Resource,
        Resource->PinGroupConfig.ResSourceLabelOffset);
    AcpiUtPrintString (Label, ACPI_UINT16_MAX);

    AcpiOsPrintf (", ");

    AcpiOsPrintf ("%s, ",
        AcpiGbl_ConsumeDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->PinGroupConfig.Flags, 1)]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();

    AcpiOsPrintf (",");

    /* Dump the vendor data */

    if (Resource->PinGroupConfig.VendorLength)
    {
        AcpiOsPrintf ("\n");
        AcpiDmIndent (Level + 1);
        VendorData = ACPI_ADD_PTR (UINT8, Resource,
            Resource->PinGroupConfig.VendorOffset);

        AcpiDmDumpRawDataBuffer (VendorData,
            Resource->PinGroupConfig.VendorLength, Level);
    }

    AcpiOsPrintf (")\n");
}
