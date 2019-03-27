/******************************************************************************
 *
 * Module Name: aslmapenter - Build resource descriptor/device maps
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
#include <contrib/dev/acpica/include/acapps.h>
#include <contrib/dev/acpica/compiler/aslcompiler.h>

/* This module used for application-level code only */

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslmapenter")

/* Local prototypes */

static ACPI_GPIO_INFO *
MpCreateGpioInfo (
    UINT16                  PinNumber,
    char                    *DeviceName);

static ACPI_SERIAL_INFO *
MpCreateSerialInfo (
    char                    *DeviceName,
    UINT16                  Address);


/*******************************************************************************
 *
 * FUNCTION:    MpSaveGpioInfo
 *
 * PARAMETERS:  Resource                - GPIO resource descriptor
 *              PinCount                - From GPIO descriptor
 *              PinList                 - From GPIO descriptor
 *              DeviceName              - The "ResourceSource" name
 *
 * RETURN:      None
 *
 * DESCRIPTION: External Interface.
 *              Save GPIO resource descriptor information.
 *              Creates new GPIO info blocks, one for each pin defined by the
 *              GPIO descriptor.
 *
 ******************************************************************************/

void
MpSaveGpioInfo (
    ACPI_PARSE_OBJECT       *Op,
    AML_RESOURCE            *Resource,
    UINT32                  PinCount,
    UINT16                  *PinList,
    char                    *DeviceName)
{
    ACPI_GPIO_INFO          *Info;
    UINT32                  i;


    /* Mapfile option enabled? */

    if (!AslGbl_MapfileFlag)
    {
        return;
    }

    /* Create an info block for each pin defined in the descriptor */

    for (i = 0; i < PinCount; i++)
    {
        Info = MpCreateGpioInfo (PinList[i], DeviceName);

        Info->Op = Op;
        Info->DeviceName = DeviceName;
        Info->PinCount = PinCount;
        Info->PinIndex = i;
        Info->PinNumber = PinList[i];
        Info->Type = Resource->Gpio.ConnectionType;
        Info->Direction = (UINT8) (Resource->Gpio.IntFlags & 0x0003);       /* _IOR, for IO descriptor */
        Info->Polarity = (UINT8) ((Resource->Gpio.IntFlags >> 1) & 0x0003); /* _POL, for INT descriptor */
    }
}


/*******************************************************************************
 *
 * FUNCTION:    MpSaveSerialInfo
 *
 * PARAMETERS:  Resource                - A Serial resource descriptor
 *              DeviceName              - The "ResourceSource" name.
 *
 * RETURN:      None
 *
 * DESCRIPTION: External Interface.
 *              Save serial resource descriptor information.
 *              Creates a new serial info block.
 *
 ******************************************************************************/

void
MpSaveSerialInfo (
    ACPI_PARSE_OBJECT       *Op,
    AML_RESOURCE            *Resource,
    char                    *DeviceName)
{
    ACPI_SERIAL_INFO        *Info;
    UINT16                  Address;
    UINT32                  Speed;


    /* Mapfile option enabled? */

    if (!AslGbl_MapfileFlag)
    {
        return;
    }

    if (Resource->DescriptorType != ACPI_RESOURCE_NAME_SERIAL_BUS)
    {
        return;
    }

    /* Extract address and speed from the resource descriptor */

    switch (Resource->CommonSerialBus.Type)
    {
    case AML_RESOURCE_I2C_SERIALBUSTYPE:

        Address = Resource->I2cSerialBus.SlaveAddress;
        Speed = Resource->I2cSerialBus.ConnectionSpeed;
        break;

    case AML_RESOURCE_SPI_SERIALBUSTYPE:

        Address = Resource->SpiSerialBus.DeviceSelection;
        Speed = Resource->SpiSerialBus.ConnectionSpeed;
        break;

    case AML_RESOURCE_UART_SERIALBUSTYPE:

        Address = 0;
        Speed = Resource->UartSerialBus.DefaultBaudRate;
        break;

    default:    /* Invalid bus subtype */
        return;
    }

    Info = MpCreateSerialInfo (DeviceName, Address);

    Info->Op = Op;
    Info->DeviceName = DeviceName;
    Info->Resource = Resource;
    Info->Address = Address;
    Info->Speed = Speed;
}


/*******************************************************************************
 *
 * FUNCTION:    MpCreateGpioInfo
 *
 * PARAMETERS:  PinNumber               - GPIO pin number
 *              DeviceName              - The "ResourceSource" name
 *
 * RETURN:      New GPIO info block.
 *
 * DESCRIPTION: Create a new GPIO info block and place it on the global list.
 *              The list is sorted by GPIO device names first, and pin numbers
 *              secondarily.
 *
 ******************************************************************************/

static ACPI_GPIO_INFO *
MpCreateGpioInfo (
    UINT16                  PinNumber,
    char                    *DeviceName)
{
    ACPI_GPIO_INFO          *Info;
    ACPI_GPIO_INFO          *NextGpio;
    ACPI_GPIO_INFO          *PrevGpio;
    char                    *Buffer;


    /*
     * Allocate a new info block and insert it into the global GPIO list
     * sorted by both source device name and then the pin number. There is
     * one block per pin.
     */
    Buffer = UtLocalCacheCalloc (sizeof (ACPI_GPIO_INFO));
    Info = ACPI_CAST_PTR (ACPI_GPIO_INFO, Buffer);

    NextGpio = AslGbl_GpioList;
    PrevGpio = NULL;
    if (!AslGbl_GpioList)
    {
        AslGbl_GpioList = Info;
        Info->Next = NULL;
        return (Info);
    }

    /* Sort on source DeviceName first */

    while (NextGpio &&
        (strcmp (DeviceName, NextGpio->DeviceName) > 0))
    {
        PrevGpio = NextGpio;
        NextGpio = NextGpio->Next;
    }

    /* Now sort on the PinNumber */

    while (NextGpio &&
        (NextGpio->PinNumber < PinNumber) &&
        !strcmp (DeviceName, NextGpio->DeviceName))
    {
        PrevGpio = NextGpio;
        NextGpio = NextGpio->Next;
    }

    /* Finish the list insertion */

    if (PrevGpio)
    {
        PrevGpio->Next = Info;
    }
    else
    {
        AslGbl_GpioList = Info;
    }

    Info->Next = NextGpio;
    return (Info);
}


/*******************************************************************************
 *
 * FUNCTION:    MpCreateSerialInfo
 *
 * PARAMETERS:  DeviceName              - The "ResourceSource" name.
 *              Address                 - Physical address for the device
 *
 * RETURN:      New Serial info block.
 *
 * DESCRIPTION: Create a new Serial info block and place it on the global list.
 *              The list is sorted by Serial device names first, and addresses
 *              secondarily.
 *
 ******************************************************************************/

static ACPI_SERIAL_INFO *
MpCreateSerialInfo (
    char                    *DeviceName,
    UINT16                  Address)
{
    ACPI_SERIAL_INFO        *Info;
    ACPI_SERIAL_INFO        *NextSerial;
    ACPI_SERIAL_INFO        *PrevSerial;
    char                    *Buffer;


    /*
     * Allocate a new info block and insert it into the global Serial list
     * sorted by both source device name and then the address.
     */
    Buffer = UtLocalCacheCalloc (sizeof (ACPI_SERIAL_INFO));
    Info = ACPI_CAST_PTR (ACPI_SERIAL_INFO, Buffer);

    NextSerial = AslGbl_SerialList;
    PrevSerial = NULL;
    if (!AslGbl_SerialList)
    {
        AslGbl_SerialList = Info;
        Info->Next = NULL;
        return (Info);
    }

    /* Sort on source DeviceName */

    while (NextSerial &&
        (strcmp (DeviceName, NextSerial->DeviceName) > 0))
    {
        PrevSerial = NextSerial;
        NextSerial = NextSerial->Next;
    }

    /* Now sort on the Address */

    while (NextSerial &&
        (NextSerial->Address < Address) &&
        !strcmp (DeviceName, NextSerial->DeviceName))
    {
        PrevSerial = NextSerial;
        NextSerial = NextSerial->Next;
    }

    /* Finish the list insertion */

    if (PrevSerial)
    {
        PrevSerial->Next = Info;
    }
    else
    {
        AslGbl_SerialList = Info;
    }

    Info->Next = NextSerial;
    return (Info);
}
