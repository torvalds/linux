/*******************************************************************************
 *
 * Module Name: rsinfo - Dispatch and Info tables
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
        ACPI_MODULE_NAME    ("rsinfo")

/*
 * Resource dispatch and information tables. Any new resource types (either
 * Large or Small) must be reflected in each of these tables, so they are here
 * in one place.
 *
 * The tables for Large descriptors are indexed by bits 6:0 of the AML
 * descriptor type byte. The tables for Small descriptors are indexed by
 * bits 6:3 of the descriptor byte. The tables for internal resource
 * descriptors are indexed by the ACPI_RESOURCE_TYPE field.
 */


/* Dispatch table for resource-to-AML (Set Resource) conversion functions */

ACPI_RSCONVERT_INFO         *AcpiGbl_SetResourceDispatch[] =
{
    AcpiRsSetIrq,                   /* 0x00, ACPI_RESOURCE_TYPE_IRQ */
    AcpiRsConvertDma,               /* 0x01, ACPI_RESOURCE_TYPE_DMA */
    AcpiRsSetStartDpf,              /* 0x02, ACPI_RESOURCE_TYPE_START_DEPENDENT */
    AcpiRsConvertEndDpf,            /* 0x03, ACPI_RESOURCE_TYPE_END_DEPENDENT */
    AcpiRsConvertIo,                /* 0x04, ACPI_RESOURCE_TYPE_IO */
    AcpiRsConvertFixedIo,           /* 0x05, ACPI_RESOURCE_TYPE_FIXED_IO */
    AcpiRsSetVendor,                /* 0x06, ACPI_RESOURCE_TYPE_VENDOR */
    AcpiRsConvertEndTag,            /* 0x07, ACPI_RESOURCE_TYPE_END_TAG */
    AcpiRsConvertMemory24,          /* 0x08, ACPI_RESOURCE_TYPE_MEMORY24 */
    AcpiRsConvertMemory32,          /* 0x09, ACPI_RESOURCE_TYPE_MEMORY32 */
    AcpiRsConvertFixedMemory32,     /* 0x0A, ACPI_RESOURCE_TYPE_FIXED_MEMORY32 */
    AcpiRsConvertAddress16,         /* 0x0B, ACPI_RESOURCE_TYPE_ADDRESS16 */
    AcpiRsConvertAddress32,         /* 0x0C, ACPI_RESOURCE_TYPE_ADDRESS32 */
    AcpiRsConvertAddress64,         /* 0x0D, ACPI_RESOURCE_TYPE_ADDRESS64 */
    AcpiRsConvertExtAddress64,      /* 0x0E, ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64 */
    AcpiRsConvertExtIrq,            /* 0x0F, ACPI_RESOURCE_TYPE_EXTENDED_IRQ */
    AcpiRsConvertGenericReg,        /* 0x10, ACPI_RESOURCE_TYPE_GENERIC_REGISTER */
    AcpiRsConvertGpio,              /* 0x11, ACPI_RESOURCE_TYPE_GPIO */
    AcpiRsConvertFixedDma,          /* 0x12, ACPI_RESOURCE_TYPE_FIXED_DMA */
    NULL,                           /* 0x13, ACPI_RESOURCE_TYPE_SERIAL_BUS - Use subtype table below */
    AcpiRsConvertPinFunction,       /* 0x14, ACPI_RESOURCE_TYPE_PIN_FUNCTION */
    AcpiRsConvertPinConfig,         /* 0x15, ACPI_RESOURCE_TYPE_PIN_CONFIG */
    AcpiRsConvertPinGroup,          /* 0x16, ACPI_RESOURCE_TYPE_PIN_GROUP */
    AcpiRsConvertPinGroupFunction,  /* 0x17, ACPI_RESOURCE_TYPE_PIN_GROUP_FUNCTION */
    AcpiRsConvertPinGroupConfig,    /* 0x18, ACPI_RESOURCE_TYPE_PIN_GROUP_CONFIG */
};

/* Dispatch tables for AML-to-resource (Get Resource) conversion functions */

ACPI_RSCONVERT_INFO         *AcpiGbl_GetResourceDispatch[] =
{
    /* Small descriptors */

    NULL,                           /* 0x00, Reserved */
    NULL,                           /* 0x01, Reserved */
    NULL,                           /* 0x02, Reserved */
    NULL,                           /* 0x03, Reserved */
    AcpiRsGetIrq,                   /* 0x04, ACPI_RESOURCE_NAME_IRQ */
    AcpiRsConvertDma,               /* 0x05, ACPI_RESOURCE_NAME_DMA */
    AcpiRsGetStartDpf,              /* 0x06, ACPI_RESOURCE_NAME_START_DEPENDENT */
    AcpiRsConvertEndDpf,            /* 0x07, ACPI_RESOURCE_NAME_END_DEPENDENT */
    AcpiRsConvertIo,                /* 0x08, ACPI_RESOURCE_NAME_IO */
    AcpiRsConvertFixedIo,           /* 0x09, ACPI_RESOURCE_NAME_FIXED_IO */
    AcpiRsConvertFixedDma,          /* 0x0A, ACPI_RESOURCE_NAME_FIXED_DMA */
    NULL,                           /* 0x0B, Reserved */
    NULL,                           /* 0x0C, Reserved */
    NULL,                           /* 0x0D, Reserved */
    AcpiRsGetVendorSmall,           /* 0x0E, ACPI_RESOURCE_NAME_VENDOR_SMALL */
    AcpiRsConvertEndTag,            /* 0x0F, ACPI_RESOURCE_NAME_END_TAG */

    /* Large descriptors */

    NULL,                           /* 0x00, Reserved */
    AcpiRsConvertMemory24,          /* 0x01, ACPI_RESOURCE_NAME_MEMORY24 */
    AcpiRsConvertGenericReg,        /* 0x02, ACPI_RESOURCE_NAME_GENERIC_REGISTER */
    NULL,                           /* 0x03, Reserved */
    AcpiRsGetVendorLarge,           /* 0x04, ACPI_RESOURCE_NAME_VENDOR_LARGE */
    AcpiRsConvertMemory32,          /* 0x05, ACPI_RESOURCE_NAME_MEMORY32 */
    AcpiRsConvertFixedMemory32,     /* 0x06, ACPI_RESOURCE_NAME_FIXED_MEMORY32 */
    AcpiRsConvertAddress32,         /* 0x07, ACPI_RESOURCE_NAME_ADDRESS32 */
    AcpiRsConvertAddress16,         /* 0x08, ACPI_RESOURCE_NAME_ADDRESS16 */
    AcpiRsConvertExtIrq,            /* 0x09, ACPI_RESOURCE_NAME_EXTENDED_IRQ */
    AcpiRsConvertAddress64,         /* 0x0A, ACPI_RESOURCE_NAME_ADDRESS64 */
    AcpiRsConvertExtAddress64,      /* 0x0B, ACPI_RESOURCE_NAME_EXTENDED_ADDRESS64 */
    AcpiRsConvertGpio,              /* 0x0C, ACPI_RESOURCE_NAME_GPIO */
    AcpiRsConvertPinFunction,       /* 0x0D, ACPI_RESOURCE_NAME_PIN_FUNCTION */
    NULL,                           /* 0x0E, ACPI_RESOURCE_NAME_SERIAL_BUS - Use subtype table below */
    AcpiRsConvertPinConfig,         /* 0x0F, ACPI_RESOURCE_NAME_PIN_CONFIG */
    AcpiRsConvertPinGroup,          /* 0x10, ACPI_RESOURCE_NAME_PIN_GROUP */
    AcpiRsConvertPinGroupFunction,  /* 0x11, ACPI_RESOURCE_NAME_PIN_GROUP_FUNCTION */
    AcpiRsConvertPinGroupConfig,    /* 0x12, ACPI_RESOURCE_NAME_PIN_GROUP_CONFIG */
};

/* Subtype table for SerialBus -- I2C, SPI, and UART */

ACPI_RSCONVERT_INFO         *AcpiGbl_ConvertResourceSerialBusDispatch[] =
{
    NULL,
    AcpiRsConvertI2cSerialBus,
    AcpiRsConvertSpiSerialBus,
    AcpiRsConvertUartSerialBus,
};


#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DISASSEMBLER) || defined(ACPI_DEBUGGER)

/* Dispatch table for resource dump functions */

ACPI_RSDUMP_INFO            *AcpiGbl_DumpResourceDispatch[] =
{
    AcpiRsDumpIrq,                  /* ACPI_RESOURCE_TYPE_IRQ */
    AcpiRsDumpDma,                  /* ACPI_RESOURCE_TYPE_DMA */
    AcpiRsDumpStartDpf,             /* ACPI_RESOURCE_TYPE_START_DEPENDENT */
    AcpiRsDumpEndDpf,               /* ACPI_RESOURCE_TYPE_END_DEPENDENT */
    AcpiRsDumpIo,                   /* ACPI_RESOURCE_TYPE_IO */
    AcpiRsDumpFixedIo,              /* ACPI_RESOURCE_TYPE_FIXED_IO */
    AcpiRsDumpVendor,               /* ACPI_RESOURCE_TYPE_VENDOR */
    AcpiRsDumpEndTag,               /* ACPI_RESOURCE_TYPE_END_TAG */
    AcpiRsDumpMemory24,             /* ACPI_RESOURCE_TYPE_MEMORY24 */
    AcpiRsDumpMemory32,             /* ACPI_RESOURCE_TYPE_MEMORY32 */
    AcpiRsDumpFixedMemory32,        /* ACPI_RESOURCE_TYPE_FIXED_MEMORY32 */
    AcpiRsDumpAddress16,            /* ACPI_RESOURCE_TYPE_ADDRESS16 */
    AcpiRsDumpAddress32,            /* ACPI_RESOURCE_TYPE_ADDRESS32 */
    AcpiRsDumpAddress64,            /* ACPI_RESOURCE_TYPE_ADDRESS64 */
    AcpiRsDumpExtAddress64,         /* ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64 */
    AcpiRsDumpExtIrq,               /* ACPI_RESOURCE_TYPE_EXTENDED_IRQ */
    AcpiRsDumpGenericReg,           /* ACPI_RESOURCE_TYPE_GENERIC_REGISTER */
    AcpiRsDumpGpio,                 /* ACPI_RESOURCE_TYPE_GPIO */
    AcpiRsDumpFixedDma,             /* ACPI_RESOURCE_TYPE_FIXED_DMA */
    NULL,                           /* ACPI_RESOURCE_TYPE_SERIAL_BUS */
    AcpiRsDumpPinFunction,          /* ACPI_RESOURCE_TYPE_PIN_FUNCTION */
    AcpiRsDumpPinConfig,            /* ACPI_RESOURCE_TYPE_PIN_CONFIG */
    AcpiRsDumpPinGroup,             /* ACPI_RESOURCE_TYPE_PIN_GROUP */
    AcpiRsDumpPinGroupFunction,     /* ACPI_RESOURCE_TYPE_PIN_GROUP_FUNCTION */
    AcpiRsDumpPinGroupConfig,       /* ACPI_RESOURCE_TYPE_PIN_GROUP_CONFIG */
};

ACPI_RSDUMP_INFO            *AcpiGbl_DumpSerialBusDispatch[] =
{
    NULL,
    AcpiRsDumpI2cSerialBus,         /* AML_RESOURCE_I2C_BUS_TYPE */
    AcpiRsDumpSpiSerialBus,         /* AML_RESOURCE_SPI_BUS_TYPE */
    AcpiRsDumpUartSerialBus,        /* AML_RESOURCE_UART_BUS_TYPE */
};
#endif


/*
 * Base sizes for external AML resource descriptors, indexed by internal type.
 * Includes size of the descriptor header (1 byte for small descriptors,
 * 3 bytes for large descriptors)
 */
const UINT8                 AcpiGbl_AmlResourceSizes[] =
{
    sizeof (AML_RESOURCE_IRQ),              /* ACPI_RESOURCE_TYPE_IRQ (optional Byte 3 always created) */
    sizeof (AML_RESOURCE_DMA),              /* ACPI_RESOURCE_TYPE_DMA */
    sizeof (AML_RESOURCE_START_DEPENDENT),  /* ACPI_RESOURCE_TYPE_START_DEPENDENT (optional Byte 1 always created) */
    sizeof (AML_RESOURCE_END_DEPENDENT),    /* ACPI_RESOURCE_TYPE_END_DEPENDENT */
    sizeof (AML_RESOURCE_IO),               /* ACPI_RESOURCE_TYPE_IO */
    sizeof (AML_RESOURCE_FIXED_IO),         /* ACPI_RESOURCE_TYPE_FIXED_IO */
    sizeof (AML_RESOURCE_VENDOR_SMALL),     /* ACPI_RESOURCE_TYPE_VENDOR */
    sizeof (AML_RESOURCE_END_TAG),          /* ACPI_RESOURCE_TYPE_END_TAG */
    sizeof (AML_RESOURCE_MEMORY24),         /* ACPI_RESOURCE_TYPE_MEMORY24 */
    sizeof (AML_RESOURCE_MEMORY32),         /* ACPI_RESOURCE_TYPE_MEMORY32 */
    sizeof (AML_RESOURCE_FIXED_MEMORY32),   /* ACPI_RESOURCE_TYPE_FIXED_MEMORY32 */
    sizeof (AML_RESOURCE_ADDRESS16),        /* ACPI_RESOURCE_TYPE_ADDRESS16 */
    sizeof (AML_RESOURCE_ADDRESS32),        /* ACPI_RESOURCE_TYPE_ADDRESS32 */
    sizeof (AML_RESOURCE_ADDRESS64),        /* ACPI_RESOURCE_TYPE_ADDRESS64 */
    sizeof (AML_RESOURCE_EXTENDED_ADDRESS64),/*ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64 */
    sizeof (AML_RESOURCE_EXTENDED_IRQ),     /* ACPI_RESOURCE_TYPE_EXTENDED_IRQ */
    sizeof (AML_RESOURCE_GENERIC_REGISTER), /* ACPI_RESOURCE_TYPE_GENERIC_REGISTER */
    sizeof (AML_RESOURCE_GPIO),             /* ACPI_RESOURCE_TYPE_GPIO */
    sizeof (AML_RESOURCE_FIXED_DMA),        /* ACPI_RESOURCE_TYPE_FIXED_DMA */
    sizeof (AML_RESOURCE_COMMON_SERIALBUS), /* ACPI_RESOURCE_TYPE_SERIAL_BUS */
    sizeof (AML_RESOURCE_PIN_FUNCTION),     /* ACPI_RESOURCE_TYPE_PIN_FUNCTION */
    sizeof (AML_RESOURCE_PIN_CONFIG),       /* ACPI_RESOURCE_TYPE_PIN_CONFIG */
    sizeof (AML_RESOURCE_PIN_GROUP),        /* ACPI_RESOURCE_TYPE_PIN_GROUP */
    sizeof (AML_RESOURCE_PIN_GROUP_FUNCTION), /* ACPI_RESOURCE_TYPE_PIN_GROUP_FUNCTION */
    sizeof (AML_RESOURCE_PIN_GROUP_CONFIG), /* ACPI_RESOURCE_TYPE_PIN_GROUP_CONFIG */
};


const UINT8                 AcpiGbl_ResourceStructSizes[] =
{
    /* Small descriptors */

    0,
    0,
    0,
    0,
    ACPI_RS_SIZE (ACPI_RESOURCE_IRQ),
    ACPI_RS_SIZE (ACPI_RESOURCE_DMA),
    ACPI_RS_SIZE (ACPI_RESOURCE_START_DEPENDENT),
    ACPI_RS_SIZE_MIN,
    ACPI_RS_SIZE (ACPI_RESOURCE_IO),
    ACPI_RS_SIZE (ACPI_RESOURCE_FIXED_IO),
    ACPI_RS_SIZE (ACPI_RESOURCE_FIXED_DMA),
    0,
    0,
    0,
    ACPI_RS_SIZE (ACPI_RESOURCE_VENDOR),
    ACPI_RS_SIZE_MIN,

    /* Large descriptors */

    0,
    ACPI_RS_SIZE (ACPI_RESOURCE_MEMORY24),
    ACPI_RS_SIZE (ACPI_RESOURCE_GENERIC_REGISTER),
    0,
    ACPI_RS_SIZE (ACPI_RESOURCE_VENDOR),
    ACPI_RS_SIZE (ACPI_RESOURCE_MEMORY32),
    ACPI_RS_SIZE (ACPI_RESOURCE_FIXED_MEMORY32),
    ACPI_RS_SIZE (ACPI_RESOURCE_ADDRESS32),
    ACPI_RS_SIZE (ACPI_RESOURCE_ADDRESS16),
    ACPI_RS_SIZE (ACPI_RESOURCE_EXTENDED_IRQ),
    ACPI_RS_SIZE (ACPI_RESOURCE_ADDRESS64),
    ACPI_RS_SIZE (ACPI_RESOURCE_EXTENDED_ADDRESS64),
    ACPI_RS_SIZE (ACPI_RESOURCE_GPIO),
    ACPI_RS_SIZE (ACPI_RESOURCE_PIN_FUNCTION),
    ACPI_RS_SIZE (ACPI_RESOURCE_COMMON_SERIALBUS),
    ACPI_RS_SIZE (ACPI_RESOURCE_PIN_CONFIG),
    ACPI_RS_SIZE (ACPI_RESOURCE_PIN_GROUP),
    ACPI_RS_SIZE (ACPI_RESOURCE_PIN_GROUP_FUNCTION),
    ACPI_RS_SIZE (ACPI_RESOURCE_PIN_GROUP_CONFIG),
};

const UINT8                 AcpiGbl_AmlResourceSerialBusSizes[] =
{
    0,
    sizeof (AML_RESOURCE_I2C_SERIALBUS),
    sizeof (AML_RESOURCE_SPI_SERIALBUS),
    sizeof (AML_RESOURCE_UART_SERIALBUS),
};

const UINT8                 AcpiGbl_ResourceStructSerialBusSizes[] =
{
    0,
    ACPI_RS_SIZE (ACPI_RESOURCE_I2C_SERIALBUS),
    ACPI_RS_SIZE (ACPI_RESOURCE_SPI_SERIALBUS),
    ACPI_RS_SIZE (ACPI_RESOURCE_UART_SERIALBUS),
};
