/******************************************************************************
 *
 * Module Name: ahids - Table of ACPI/PNP _HID/_CID values
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

#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("ahids")


/*
 * ACPI/PNP Device IDs with description strings
 */
const AH_DEVICE_ID  AslDeviceIds[] =
{
    {"10EC5640",    "Realtek I2S Audio Codec"},
    {"80860F09",    "Intel PWM Controller"},
    {"80860F0A",    "Intel Atom UART Controller"},
    {"80860F0E",    "Intel SPI Controller"},
    {"80860F14",    "Intel Baytrail SDIO/MMC Host Controller"},
    {"80860F28",    "Intel SST Audio DSP"},
    {"80860F41",    "Intel Baytrail I2C Host Controller"},
    {"ACPI0001",    "SMBus 1.0 Host Controller"},
    {"ACPI0002",    "Smart Battery Subsystem"},
    {"ACPI0003",    "Power Source Device"},
    {"ACPI0004",    "Module Device"},
    {"ACPI0005",    "SMBus 2.0 Host Controller"},
    {"ACPI0006",    "GPE Block Device"},
    {"ACPI0007",    "Processor Device"},
    {"ACPI0008",    "Ambient Light Sensor Device"},
    {"ACPI0009",    "I/O xAPIC Device"},
    {"ACPI000A",    "I/O APIC Device"},
    {"ACPI000B",    "I/O SAPIC Device"},
    {"ACPI000C",    "Processor Aggregator Device"},
    {"ACPI000D",    "Power Meter Device"},
    {"ACPI000E",    "Time and Alarm Device"},
    {"ACPI000F",    "User Presence Detection Device"},
    {"ACPI0010",    "Processor Container Device"},
    {"ACPI0011",    "Generic Buttons Device"},
    {"ACPI0012",    "NVDIMM Root Device"},
    {"ACPI0013",    "Generic Event Device"},
    {"ACPI0014",    "Wireless Power Calibration Device"},
    {"ADMA0F28",    "Intel Audio DMA"},
    {"AMCR0F28",    "Intel Audio Machine Driver"},
    {"ATK4001",     "Asus Radio Control Button"},
    {"ATML1000",    "Atmel Touchscreen Controller"},
    {"AUTH2750",    "AuthenTec AES2750"},
    {"BCM2E39",     "Broadcom BT Serial Bus Driver over UART Bus Enumerator"},
    {"BCM4752E",    "Broadcom GPS Controller"},
    {"BMG0160",     "Bosch Gyro Sensor"},
    {"CPLM3218",    "Capella Micro CM3218x Ambient Light Sensor"},
    {"DELLABCE",    "Dell Airplane Mode Switch Driver"},
    {"DLAC3002",    "Qualcomm Atheros Bluetooth UART Transport"},
    {"FTTH5506",    "FocalTech 5506 Touch Controller"},
    {"HAD0F28",     "Intel HDMI Audio Driver"},
    {"INBC0000",    "GPIO Expander"},
    {"INT0002",     "Virtual GPIO Controller"},
    {"INT0800",     "Intel 82802 Firmware Hub Device"},
    {"INT3394",     "ACPI System Fan"},
    {"INT3396",     "Standard Power Management Controller"},
    {"INT33A0",     "Intel Smart Connect Technology Device"},
    {"INT33A1",     "Intel Power Engine"},
    {"INT33BB",     "Intel Baytrail SD Host Controller"},
    {"INT33BD",     "Intel Baytrail Mailbox Device"},
    {"INT33BE",     "Camera Sensor OV5693"},
    {"INT33C0",     "Intel Serial I/O SPI Host Controller"},
    {"INT33C1",     "Intel Serial I/O SPI Host Controller"},
    {"INT33C2",     "Intel Serial I/O I2C Host Controller"},
    {"INT33C3",     "Intel Serial I/O I2C Host Controller"},
    {"INT33C4",     "Intel Serial I/O UART Host Controller"},
    {"INT33C5",     "Intel Serial I/O UART Host Controller"},
    {"INT33C6",     "Intel SD Host Controller"},
    {"INT33C7",     "Intel Serial I/O GPIO Host Controller"},
    {"INT33C8",     "Intel Smart Sound Technology Host Controller"},
    {"INT33C9",     "Wolfson Microelectronics Audio WM5102"},
    {"INT33CA",     "Intel SPB Peripheral"},
    {"INT33CB",     "Intel Smart Sound Technology Audio Codec"},
    {"INT33D1",     "Intel GPIO Buttons"},
    {"INT33D2",     "Intel GPIO Buttons"},
    {"INT33D3",     "Intel GPIO Buttons"},
    {"INT33D4",     "Intel GPIO Buttons"},
    {"INT33D6",     "Intel Virtual Buttons Device"},
    {"INT33F0",     "Camera Sensor MT9M114"},
    {"INT33F4",     "XPOWER PMIC Controller"},
    {"INT33F5",     "TI PMIC Controller"},
    {"INT33FB",     "MIPI-CSI Camera Sensor OV2722"},
    {"INT33FC",     "Intel Baytrail GPIO Controller"},
    {"INT33FD",     "Intel Baytrail Power Management IC"},
    {"INT33FE",     "XPOWER Battery Device"},
    {"INT3400",     "Intel Dynamic Power Performance Management"},
    {"INT3401",     "Intel Extended Thermal Model CPU"},
    {"INT3403",     "DPTF Temperature Sensor"},
    {"INT3406",     "Intel Dynamic Platform & Thermal Framework Display Participant"},
    {"INT3407",     "DPTF Platform Power Meter"},
    {"INT340E",     "Motherboard Resources"},
    {"INT3420",     "Intel Bluetooth RF Kill"},
    {"INT3F0D",     "ACPI Motherboard Resources"},
    {"INTCF1A",     "Sony IMX175 Camera Sensor"},
    {"INTCFD9",     "Intel Baytrail SOC GPIO Controller"},
    {"INTL9C60",    "Intel Baytrail SOC DMA Controller"},
    {"INVN6500",    "InvenSense MPU-6500 Six Axis Gyroscope and Accelerometer"},
    {"LNXCPU",      "Linux Logical CPU"},
    {"LNXPOWER",    "ACPI Power Resource (power gating)"},
    {"LNXPWRBN",    "System Power Button"},
    {"LNXSYBUS",    "System Bus"},
    {"LNXSYSTM",    "ACPI Root Node"},
    {"LNXTHERM",    "ACPI Thermal Zone"},
    {"LNXVIDEO",    "ACPI Video Controller"},
    {"MAX17047",    "Fuel Gauge Controller"},
    {"MSFT0101",    "TPM 2.0 Security Device"},
    {"NXP5442",     "NXP 5442 Near Field Communications Controller"},
    {"NXP5472",     "NXP NFC"},
    {"PNP0000",     "8259-compatible Programmable Interrupt Controller"},
    {"PNP0001",     "EISA Interrupt Controller"},
    {"PNP0002",     "MCA Interrupt Controller"},
    {"PNP0003",     "IO-APIC Interrupt Controller"},
    {"PNP0100",     "PC-class System Timer"},
    {"PNP0103",     "HPET System Timer"},
    {"PNP0200",     "PC-class DMA Controller"},
    {"PNP0300",     "IBM PC/XT Keyboard Controller (83 key)"},
    {"PNP0301",     "IBM PC/XT Keyboard Controller (86 key)"},
    {"PNP0302",     "IBM PC/XT Keyboard Controller (84 key)"},
    {"PNP0303",     "IBM Enhanced Keyboard (101/102-key, PS/2 Mouse)"},
    {"PNP0400",     "Standard LPT Parallel Port"},
    {"PNP0401",     "ECP Parallel Port"},
    {"PNP0500",     "Standard PC COM Serial Port"},
    {"PNP0501",     "16550A-compatible COM Serial Port"},
    {"PNP0510",     "Generic IRDA-compatible Device"},
    {"PNP0800",     "Microsoft Sound System Compatible Device"},
    {"PNP0A03",     "PCI Bus"},
    {"PNP0A05",     "Generic Container Device"},
    {"PNP0A06",     "Generic Container Device"},
    {"PNP0A08",     "PCI Express Bus"},
    {"PNP0B00",     "AT Real-Time Clock"},
    {"PNP0B01",     "Intel PIIX4-compatible RTC/CMOS Device"},
    {"PNP0B02",     "Dallas Semiconductor-compatible RTC/CMOS Device"},
    {"PNP0C01",     "System Board"},
    {"PNP0C02",     "PNP Motherboard Resources"},
    {"PNP0C04",     "x87-compatible Floating Point Processing Unit"},
    {"PNP0C08",     "ACPI Core Hardware"},
    {"PNP0C09",     "Embedded Controller Device"},
    {"PNP0C0A",     "Control Method Battery"},
    {"PNP0C0B",     "Fan (Thermal Solution)"},
    {"PNP0C0C",     "Power Button Device"},
    {"PNP0C0D",     "Lid Device"},
    {"PNP0C0E",     "Sleep Button Device"},
    {"PNP0C0F",     "PCI Interrupt Link Device"},
    {"PNP0C10",     "System Indicator Device"},
    {"PNP0C11",     "Thermal Zone"},
    {"PNP0C12",     "Device Bay Controller"},
    {"PNP0C14",     "Windows Management Instrumentation Device"},
    {"PNP0C15",     "Docking Station"},
    {"PNP0C33",     "Error Device"},
    {"PNP0C40",     "Standard Button Controller"},
    {"PNP0C50",     "HID Protocol Device (I2C bus)"},
    {"PNP0C60",     "Display Sensor Device"},
    {"PNP0C70",     "Dock Sensor Device"},
    {"PNP0C80",     "Memory Device"},
    {"PNP0D10",     "XHCI USB Controller with debug"},
    {"PNP0D15",     "XHCI USB Controller without debug"},
    {"PNP0D20",     "EHCI USB Controller without debug"},
    {"PNP0D25",     "EHCI USB Controller with debug"},
    {"PNP0D40",     "SDA Standard Compliant SD Host Controller"},
    {"PNP0D80",     "Windows-compatible System Power Management Controller"},
    {"PNP0F03",     "Microsoft PS/2-style Mouse"},
    {"PNP0F13",     "PS/2 Mouse"},
    {"RTL8723",     "Realtek Wireless Controller"},
    {"SMB0349",     "Charger"},
    {"SMO91D0",     "Sensor Hub"},
    {"SMSC3750",    "SMSC 3750 USB MUX"},
    {"SSPX0000",    "Intel SSP Device"},
    {"TBQ24296",    "Charger"},

    {NULL, NULL}
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiAhMatchHardwareId
 *
 * PARAMETERS:  HardwareId          - String representation of an _HID or _CID
 *
 * RETURN:      ID info struct. NULL if HardwareId is not found
 *
 * DESCRIPTION: Lookup an _HID/_CID in the device ID table
 *
 ******************************************************************************/

const AH_DEVICE_ID *
AcpiAhMatchHardwareId (
    char                    *HardwareId)
{
    const AH_DEVICE_ID      *Info;


    for (Info = AslDeviceIds; Info->Name; Info++)
    {
        if (!strcmp (HardwareId, Info->Name))
        {
            return (Info);
        }
    }

    return (NULL);
}
