/*******************************************************************************
 *
 * Module Name: rsdumpinfo - Tables used to display resource descriptors.
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
        ACPI_MODULE_NAME    ("rsdumpinfo")


#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DISASSEMBLER) || defined(ACPI_DEBUGGER)


#define ACPI_RSD_OFFSET(f)          (UINT8) ACPI_OFFSET (ACPI_RESOURCE_DATA,f)
#define ACPI_PRT_OFFSET(f)          (UINT8) ACPI_OFFSET (ACPI_PCI_ROUTING_TABLE,f)
#define ACPI_RSD_TABLE_SIZE(name)   (sizeof(name) / sizeof (ACPI_RSDUMP_INFO))


/*******************************************************************************
 *
 * Resource Descriptor info tables
 *
 * Note: The first table entry must be a Title or Literal and must contain
 * the table length (number of table entries)
 *
 ******************************************************************************/

ACPI_RSDUMP_INFO        AcpiRsDumpIrq[7] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpIrq),                "IRQ",                      NULL},
    {ACPI_RSD_UINT8 ,   ACPI_RSD_OFFSET (Irq.DescriptorLength),             "Descriptor Length",        NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Irq.Triggering),                   "Triggering",               AcpiGbl_HeDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Irq.Polarity),                     "Polarity",                 AcpiGbl_LlDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (Irq.Shareable),                    "Sharing",                  AcpiGbl_ShrDecode},
    {ACPI_RSD_UINT8 ,   ACPI_RSD_OFFSET (Irq.InterruptCount),               "Interrupt Count",          NULL},
    {ACPI_RSD_SHORTLIST,ACPI_RSD_OFFSET (Irq.Interrupts[0]),                "Interrupt List",           NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpDma[6] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpDma),                "DMA",                      NULL},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (Dma.Type),                         "Speed",                    AcpiGbl_TypDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Dma.BusMaster),                    "Mastering",                AcpiGbl_BmDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (Dma.Transfer),                     "Transfer Type",            AcpiGbl_SizDecode},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (Dma.ChannelCount),                 "Channel Count",            NULL},
    {ACPI_RSD_SHORTLIST,ACPI_RSD_OFFSET (Dma.Channels[0]),                  "Channel List",             NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpStartDpf[4] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpStartDpf),           "Start-Dependent-Functions",NULL},
    {ACPI_RSD_UINT8 ,   ACPI_RSD_OFFSET (StartDpf.DescriptorLength),        "Descriptor Length",        NULL},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (StartDpf.CompatibilityPriority),   "Compatibility Priority",   AcpiGbl_ConfigDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (StartDpf.PerformanceRobustness),   "Performance/Robustness",   AcpiGbl_ConfigDecode}
};

ACPI_RSDUMP_INFO        AcpiRsDumpEndDpf[1] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpEndDpf),             "End-Dependent-Functions",  NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpIo[6] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpIo),                 "I/O",                      NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Io.IoDecode),                      "Address Decoding",         AcpiGbl_IoDecode},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Io.Minimum),                       "Address Minimum",          NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Io.Maximum),                       "Address Maximum",          NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (Io.Alignment),                     "Alignment",                NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (Io.AddressLength),                 "Address Length",           NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpFixedIo[3] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpFixedIo),            "Fixed I/O",                NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (FixedIo.Address),                  "Address",                  NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (FixedIo.AddressLength),            "Address Length",           NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpVendor[3] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpVendor),             "Vendor Specific",          NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Vendor.ByteLength),                "Length",                   NULL},
    {ACPI_RSD_LONGLIST, ACPI_RSD_OFFSET (Vendor.ByteData[0]),               "Vendor Data",              NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpEndTag[1] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpEndTag),             "EndTag",                   NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpMemory24[6] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpMemory24),           "24-Bit Memory Range",      NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Memory24.WriteProtect),            "Write Protect",            AcpiGbl_RwDecode},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Memory24.Minimum),                 "Address Minimum",          NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Memory24.Maximum),                 "Address Maximum",          NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Memory24.Alignment),               "Alignment",                NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Memory24.AddressLength),           "Address Length",           NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpMemory32[6] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpMemory32),           "32-Bit Memory Range",      NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Memory32.WriteProtect),            "Write Protect",            AcpiGbl_RwDecode},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (Memory32.Minimum),                 "Address Minimum",          NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (Memory32.Maximum),                 "Address Maximum",          NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (Memory32.Alignment),               "Alignment",                NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (Memory32.AddressLength),           "Address Length",           NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpFixedMemory32[4] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpFixedMemory32),      "32-Bit Fixed Memory Range",NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (FixedMemory32.WriteProtect),       "Write Protect",            AcpiGbl_RwDecode},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (FixedMemory32.Address),            "Address",                  NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (FixedMemory32.AddressLength),      "Address Length",           NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpAddress16[8] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpAddress16),          "16-Bit WORD Address Space",NULL},
    {ACPI_RSD_ADDRESS,  0,                                                  NULL,                       NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Address16.Address.Granularity),    "Granularity",              NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Address16.Address.Minimum),        "Address Minimum",          NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Address16.Address.Maximum),        "Address Maximum",          NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Address16.Address.TranslationOffset),
                                                                            "Translation Offset",       NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Address16.Address.AddressLength),  "Address Length",           NULL},
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (Address16.ResourceSource),         NULL,                       NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpAddress32[8] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpAddress32),         "32-Bit DWORD Address Space", NULL},
    {ACPI_RSD_ADDRESS,  0,                                                  NULL,                       NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (Address32.Address.Granularity),    "Granularity",              NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (Address32.Address.Minimum),        "Address Minimum",          NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (Address32.Address.Maximum),        "Address Maximum",          NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (Address32.Address.TranslationOffset),
                                                                            "Translation Offset",       NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (Address32.Address.AddressLength),  "Address Length",           NULL},
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (Address32.ResourceSource),         NULL,                       NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpAddress64[8] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpAddress64),          "64-Bit QWORD Address Space", NULL},
    {ACPI_RSD_ADDRESS,  0,                                                  NULL,                       NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (Address64.Address.Granularity),    "Granularity",              NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (Address64.Address.Minimum),        "Address Minimum",          NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (Address64.Address.Maximum),        "Address Maximum",          NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (Address64.Address.TranslationOffset),
                                                                            "Translation Offset",       NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (Address64.Address.AddressLength),  "Address Length",           NULL},
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (Address64.ResourceSource),         NULL,                       NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpExtAddress64[8] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpExtAddress64),       "64-Bit Extended Address Space", NULL},
    {ACPI_RSD_ADDRESS,  0,                                                  NULL,                       NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (ExtAddress64.Address.Granularity), "Granularity",              NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (ExtAddress64.Address.Minimum),     "Address Minimum",          NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (ExtAddress64.Address.Maximum),     "Address Maximum",          NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (ExtAddress64.Address.TranslationOffset),
                                                                            "Translation Offset",       NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (ExtAddress64.Address.AddressLength),
                                                                            "Address Length",           NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (ExtAddress64.TypeSpecific),        "Type-Specific Attribute",  NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpExtIrq[8] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpExtIrq),             "Extended IRQ",             NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (ExtendedIrq.ProducerConsumer),     "Type",                     AcpiGbl_ConsumeDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (ExtendedIrq.Triggering),           "Triggering",               AcpiGbl_HeDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (ExtendedIrq.Polarity),             "Polarity",                 AcpiGbl_LlDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (ExtendedIrq.Shareable),            "Sharing",                  AcpiGbl_ShrDecode},
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (ExtendedIrq.ResourceSource),       NULL,                       NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (ExtendedIrq.InterruptCount),       "Interrupt Count",          NULL},
    {ACPI_RSD_DWORDLIST,ACPI_RSD_OFFSET (ExtendedIrq.Interrupts[0]),        "Interrupt List",           NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpGenericReg[6] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpGenericReg),         "Generic Register",         NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (GenericReg.SpaceId),               "Space ID",                 NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (GenericReg.BitWidth),              "Bit Width",                NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (GenericReg.BitOffset),             "Bit Offset",               NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (GenericReg.AccessSize),            "Access Size",              NULL},
    {ACPI_RSD_UINT64,   ACPI_RSD_OFFSET (GenericReg.Address),               "Address",                  NULL}
};

ACPI_RSDUMP_INFO        AcpiRsDumpGpio[16] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpGpio),               "GPIO",                     NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (Gpio.RevisionId),                  "RevisionId",               NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (Gpio.ConnectionType),              "ConnectionType",           AcpiGbl_CtDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Gpio.ProducerConsumer),            "ProducerConsumer",         AcpiGbl_ConsumeDecode},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (Gpio.PinConfig),                   "PinConfig",                AcpiGbl_PpcDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (Gpio.Shareable),                   "Sharing",                  AcpiGbl_ShrDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (Gpio.IoRestriction),               "IoRestriction",            AcpiGbl_IorDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Gpio.Triggering),                  "Triggering",               AcpiGbl_HeDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (Gpio.Polarity),                    "Polarity",                 AcpiGbl_LlDecode},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Gpio.DriveStrength),               "DriveStrength",            NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Gpio.DebounceTimeout),             "DebounceTimeout",          NULL},
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (Gpio.ResourceSource),              "ResourceSource",           NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Gpio.PinTableLength),              "PinTableLength",           NULL},
    {ACPI_RSD_WORDLIST, ACPI_RSD_OFFSET (Gpio.PinTable),                    "PinTable",                 NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (Gpio.VendorLength),                "VendorLength",             NULL},
    {ACPI_RSD_SHORTLISTX,ACPI_RSD_OFFSET (Gpio.VendorData),                 "VendorData",               NULL},
};

ACPI_RSDUMP_INFO        AcpiRsDumpPinFunction[10] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpPinFunction),        "PinFunction",              NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (PinFunction.RevisionId),           "RevisionId",               NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (PinFunction.PinConfig),            "PinConfig",                AcpiGbl_PpcDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (PinFunction.Shareable),            "Sharing",                  AcpiGbl_ShrDecode},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinFunction.FunctionNumber),       "FunctionNumber",           NULL},
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (PinFunction.ResourceSource),       "ResourceSource",           NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinFunction.PinTableLength),       "PinTableLength",           NULL},
    {ACPI_RSD_WORDLIST, ACPI_RSD_OFFSET (PinFunction.PinTable),             "PinTable",                 NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinFunction.VendorLength),         "VendorLength",             NULL},
    {ACPI_RSD_SHORTLISTX,ACPI_RSD_OFFSET (PinFunction.VendorData),          "VendorData",               NULL},
};

ACPI_RSDUMP_INFO        AcpiRsDumpPinConfig[11] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpPinConfig),          "PinConfig",                NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (PinConfig.RevisionId),             "RevisionId",               NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (PinConfig.ProducerConsumer),       "ProducerConsumer",         AcpiGbl_ConsumeDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (PinConfig.Shareable),              "Sharing",                  AcpiGbl_ShrDecode},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (PinConfig.PinConfigType),          "PinConfigType",            NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (PinConfig.PinConfigValue),         "PinConfigValue",           NULL},
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (PinConfig.ResourceSource),         "ResourceSource",           NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinConfig.PinTableLength),         "PinTableLength",           NULL},
    {ACPI_RSD_WORDLIST, ACPI_RSD_OFFSET (PinConfig.PinTable),               "PinTable",                 NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinConfig.VendorLength),           "VendorLength",             NULL},
    {ACPI_RSD_SHORTLISTX,ACPI_RSD_OFFSET (PinConfig.VendorData),            "VendorData",               NULL},
};

ACPI_RSDUMP_INFO        AcpiRsDumpPinGroup[8] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpPinGroup),           "PinGroup",                 NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (PinGroup.RevisionId),              "RevisionId",               NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (PinGroup.ProducerConsumer),        "ProducerConsumer",         AcpiGbl_ConsumeDecode},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinGroup.PinTableLength),          "PinTableLength",           NULL},
    {ACPI_RSD_WORDLIST, ACPI_RSD_OFFSET (PinGroup.PinTable),                "PinTable",                 NULL},
    {ACPI_RSD_LABEL,    ACPI_RSD_OFFSET (PinGroup.ResourceLabel),           "ResourceLabel",            NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinGroup.VendorLength),            "VendorLength",             NULL},
    {ACPI_RSD_SHORTLISTX,ACPI_RSD_OFFSET (PinGroup.VendorData),             "VendorData",               NULL},
};

ACPI_RSDUMP_INFO        AcpiRsDumpPinGroupFunction[9] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpPinGroupFunction),   "PinGroupFunction",         NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (PinGroupFunction.RevisionId),      "RevisionId",               NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (PinGroupFunction.ProducerConsumer), "ProducerConsumer",        AcpiGbl_ConsumeDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (PinGroupFunction.Shareable),       "Sharing",                  AcpiGbl_ShrDecode},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinGroupFunction.FunctionNumber),  "FunctionNumber",           NULL},
    {ACPI_RSD_SOURCE_LABEL, ACPI_RSD_OFFSET (PinGroupFunction.ResourceSourceLabel), "ResourceSourceLabel", NULL},
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (PinGroupFunction.ResourceSource),  "ResourceSource",           NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinGroupFunction.VendorLength),    "VendorLength",             NULL},
    {ACPI_RSD_SHORTLISTX,ACPI_RSD_OFFSET (PinGroupFunction.VendorData),     "VendorData",               NULL},
};

ACPI_RSDUMP_INFO        AcpiRsDumpPinGroupConfig[10] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpPinGroupConfig),     "PinGroupConfig",           NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (PinGroupConfig.RevisionId),        "RevisionId",               NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (PinGroupConfig.ProducerConsumer),  "ProducerConsumer",         AcpiGbl_ConsumeDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (PinGroupConfig.Shareable),         "Sharing",                  AcpiGbl_ShrDecode},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (PinGroupConfig.PinConfigType),     "PinConfigType",            NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (PinGroupConfig.PinConfigValue),    "PinConfigValue",           NULL},
    {ACPI_RSD_SOURCE_LABEL, ACPI_RSD_OFFSET (PinGroupConfig.ResourceSourceLabel), "ResourceSourceLabel", NULL},
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (PinGroupConfig.ResourceSource),    "ResourceSource",           NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (PinGroupConfig.VendorLength),      "VendorLength",             NULL},
    {ACPI_RSD_SHORTLISTX,ACPI_RSD_OFFSET (PinGroupConfig.VendorData),       "VendorData",               NULL},
};

ACPI_RSDUMP_INFO        AcpiRsDumpFixedDma[4] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpFixedDma),           "FixedDma",                 NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (FixedDma.RequestLines),            "RequestLines",             NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (FixedDma.Channels),                "Channels",                 NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (FixedDma.Width),                   "TransferWidth",            AcpiGbl_DtsDecode},
};

#define ACPI_RS_DUMP_COMMON_SERIAL_BUS \
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (CommonSerialBus.RevisionId),       "RevisionId",               NULL}, \
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (CommonSerialBus.Type),             "Type",                     AcpiGbl_SbtDecode}, \
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (CommonSerialBus.ProducerConsumer), "ProducerConsumer",         AcpiGbl_ConsumeDecode}, \
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (CommonSerialBus.SlaveMode),        "SlaveMode",                AcpiGbl_SmDecode}, \
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (CommonSerialBus.ConnectionSharing),"ConnectionSharing",        AcpiGbl_ShrDecode}, \
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (CommonSerialBus.TypeRevisionId),   "TypeRevisionId",           NULL}, \
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (CommonSerialBus.TypeDataLength),   "TypeDataLength",           NULL}, \
    {ACPI_RSD_SOURCE,   ACPI_RSD_OFFSET (CommonSerialBus.ResourceSource),   "ResourceSource",           NULL}, \
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (CommonSerialBus.VendorLength),     "VendorLength",             NULL}, \
    {ACPI_RSD_SHORTLISTX,ACPI_RSD_OFFSET (CommonSerialBus.VendorData),      "VendorData",               NULL},

ACPI_RSDUMP_INFO        AcpiRsDumpCommonSerialBus[11] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpCommonSerialBus),    "Common Serial Bus",        NULL},
    ACPI_RS_DUMP_COMMON_SERIAL_BUS
};

ACPI_RSDUMP_INFO        AcpiRsDumpI2cSerialBus[14] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpI2cSerialBus),       "I2C Serial Bus",           NULL},
    ACPI_RS_DUMP_COMMON_SERIAL_BUS
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (I2cSerialBus.AccessMode),          "AccessMode",               AcpiGbl_AmDecode},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (I2cSerialBus.ConnectionSpeed),     "ConnectionSpeed",          NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (I2cSerialBus.SlaveAddress),        "SlaveAddress",             NULL},
};

ACPI_RSDUMP_INFO        AcpiRsDumpSpiSerialBus[18] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpSpiSerialBus),       "Spi Serial Bus",           NULL},
    ACPI_RS_DUMP_COMMON_SERIAL_BUS
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (SpiSerialBus.WireMode),            "WireMode",                 AcpiGbl_WmDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (SpiSerialBus.DevicePolarity),      "DevicePolarity",           AcpiGbl_DpDecode},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (SpiSerialBus.DataBitLength),       "DataBitLength",            NULL},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (SpiSerialBus.ClockPhase),          "ClockPhase",               AcpiGbl_CphDecode},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (SpiSerialBus.ClockPolarity),       "ClockPolarity",            AcpiGbl_CpoDecode},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (SpiSerialBus.DeviceSelection),     "DeviceSelection",          NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (SpiSerialBus.ConnectionSpeed),     "ConnectionSpeed",          NULL},
};

ACPI_RSDUMP_INFO        AcpiRsDumpUartSerialBus[20] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpUartSerialBus),       "Uart Serial Bus",         NULL},
    ACPI_RS_DUMP_COMMON_SERIAL_BUS
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (UartSerialBus.FlowControl),         "FlowControl",             AcpiGbl_FcDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (UartSerialBus.StopBits),            "StopBits",                AcpiGbl_SbDecode},
    {ACPI_RSD_3BITFLAG, ACPI_RSD_OFFSET (UartSerialBus.DataBits),            "DataBits",                AcpiGbl_BpbDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (UartSerialBus.Endian),              "Endian",                  AcpiGbl_EdDecode},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (UartSerialBus.Parity),              "Parity",                  AcpiGbl_PtDecode},
    {ACPI_RSD_UINT8,    ACPI_RSD_OFFSET (UartSerialBus.LinesEnabled),        "LinesEnabled",            NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (UartSerialBus.RxFifoSize),          "RxFifoSize",              NULL},
    {ACPI_RSD_UINT16,   ACPI_RSD_OFFSET (UartSerialBus.TxFifoSize),          "TxFifoSize",              NULL},
    {ACPI_RSD_UINT32,   ACPI_RSD_OFFSET (UartSerialBus.DefaultBaudRate),     "ConnectionSpeed",         NULL},
};

/*
 * Tables used for common address descriptor flag fields
 */
ACPI_RSDUMP_INFO        AcpiRsDumpGeneralFlags[5] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpGeneralFlags),       NULL,                       NULL},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Address.ProducerConsumer),         "Consumer/Producer",        AcpiGbl_ConsumeDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Address.Decode),                   "Address Decode",           AcpiGbl_DecDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Address.MinAddressFixed),          "Min Relocatability",       AcpiGbl_MinDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Address.MaxAddressFixed),          "Max Relocatability",       AcpiGbl_MaxDecode}
};

ACPI_RSDUMP_INFO        AcpiRsDumpMemoryFlags[5] =
{
    {ACPI_RSD_LITERAL,  ACPI_RSD_TABLE_SIZE (AcpiRsDumpMemoryFlags),        "Resource Type",            (void *) "Memory Range"},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Address.Info.Mem.WriteProtect),    "Write Protect",            AcpiGbl_RwDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (Address.Info.Mem.Caching),         "Caching",                  AcpiGbl_MemDecode},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (Address.Info.Mem.RangeType),       "Range Type",               AcpiGbl_MtpDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Address.Info.Mem.Translation),     "Translation",              AcpiGbl_TtpDecode}
};

ACPI_RSDUMP_INFO        AcpiRsDumpIoFlags[4] =
{
    {ACPI_RSD_LITERAL,  ACPI_RSD_TABLE_SIZE (AcpiRsDumpIoFlags),            "Resource Type",            (void *) "I/O Range"},
    {ACPI_RSD_2BITFLAG, ACPI_RSD_OFFSET (Address.Info.Io.RangeType),        "Range Type",               AcpiGbl_RngDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Address.Info.Io.Translation),      "Translation",              AcpiGbl_TtpDecode},
    {ACPI_RSD_1BITFLAG, ACPI_RSD_OFFSET (Address.Info.Io.TranslationType),  "Translation Type",         AcpiGbl_TrsDecode}
};


/*
 * Table used to dump _PRT contents
 */
ACPI_RSDUMP_INFO        AcpiRsDumpPrt[5] =
{
    {ACPI_RSD_TITLE,    ACPI_RSD_TABLE_SIZE (AcpiRsDumpPrt),                NULL,                       NULL},
    {ACPI_RSD_UINT64,   ACPI_PRT_OFFSET (Address),                          "Address",                  NULL},
    {ACPI_RSD_UINT32,   ACPI_PRT_OFFSET (Pin),                              "Pin",                      NULL},
    {ACPI_RSD_STRING,   ACPI_PRT_OFFSET (Source[0]),                        "Source",                   NULL},
    {ACPI_RSD_UINT32,   ACPI_PRT_OFFSET (SourceIndex),                      "Source Index",             NULL}
};

#endif
