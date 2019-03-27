/******************************************************************************
 *
 * Module Name: dmrestag - Add tags to resource descriptors (Application-level)
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acdisasm.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/amlcode.h>

/* This module used for application-level code only */

#define _COMPONENT          ACPI_CA_DISASSEMBLER
        ACPI_MODULE_NAME    ("dmrestag")

/* Local prototypes */

static void
AcpiDmUpdateResourceName (
    ACPI_NAMESPACE_NODE     *ResourceNode);

static char *
AcpiDmSearchTagList (
    UINT32                  BitIndex,
    const ACPI_RESOURCE_TAG *TagList);

static char *
AcpiDmGetResourceTag (
    UINT32                  BitIndex,
    AML_RESOURCE            *Resource,
    UINT8                   ResourceIndex);

static char *
AcpiGetTagPathname (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_NAMESPACE_NODE     *BufferNode,
    ACPI_NAMESPACE_NODE     *ResourceNode,
    UINT32                  BitIndex);

static ACPI_NAMESPACE_NODE *
AcpiDmGetResourceNode (
    ACPI_NAMESPACE_NODE     *BufferNode,
    UINT32                  BitIndex);

static ACPI_STATUS
AcpiDmAddResourceToNamespace (
    UINT8                   *Aml,
    UINT32                  Length,
    UINT32                  Offset,
    UINT8                   ResourceIndex,
    void                    **Context);

static void
AcpiDmAddResourcesToNamespace (
    ACPI_NAMESPACE_NODE     *BufferNode,
    ACPI_PARSE_OBJECT       *Op);


/******************************************************************************
 *
 * Resource Tag tables
 *
 * These are the predefined tags that refer to elements of a resource
 * descriptor. Each name and offset is defined in the ACPI specification.
 *
 * Each table entry contains the bit offset of the field and the associated
 * name.
 *
 ******************************************************************************/

static const ACPI_RESOURCE_TAG      AcpiDmIrqTags[] =
{
    {( 1 * 8),      ACPI_RESTAG_INTERRUPT},
    {( 3 * 8) + 0,  ACPI_RESTAG_INTERRUPTTYPE},
    {( 3 * 8) + 3,  ACPI_RESTAG_INTERRUPTLEVEL},
    {( 3 * 8) + 4,  ACPI_RESTAG_INTERRUPTSHARE},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmDmaTags[] =
{
    {( 1 * 8),      ACPI_RESTAG_DMA},
    {( 2 * 8) + 0,  ACPI_RESTAG_XFERTYPE},
    {( 2 * 8) + 2,  ACPI_RESTAG_BUSMASTER},
    {( 2 * 8) + 5,  ACPI_RESTAG_DMATYPE},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmIoTags[] =
{
    {( 1 * 8) + 0,  ACPI_RESTAG_DECODE},
    {( 2 * 8),      ACPI_RESTAG_MINADDR},
    {( 4 * 8),      ACPI_RESTAG_MAXADDR},
    {( 6 * 8),      ACPI_RESTAG_ALIGNMENT},
    {( 7 * 8),      ACPI_RESTAG_LENGTH},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmFixedIoTags[] =
{
    {( 1 * 8),      ACPI_RESTAG_BASEADDRESS},
    {( 3 * 8),      ACPI_RESTAG_LENGTH},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmFixedDmaTags[] =
{
    {( 1 * 8),      ACPI_RESTAG_DMA},
    {( 3 * 8),      ACPI_RESTAG_DMATYPE},
    {( 5 * 8),      ACPI_RESTAG_XFERTYPE},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmMemory24Tags[] =
{
    {( 3 * 8) + 0,  ACPI_RESTAG_READWRITETYPE},
    {( 4 * 8),      ACPI_RESTAG_MINADDR},
    {( 6 * 8),      ACPI_RESTAG_MAXADDR},
    {( 8 * 8),      ACPI_RESTAG_ALIGNMENT},
    {(10 * 8),      ACPI_RESTAG_LENGTH},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmRegisterTags[] =
{
    {( 3 * 8),      ACPI_RESTAG_ADDRESSSPACE},
    {( 4 * 8),      ACPI_RESTAG_REGISTERBITWIDTH},
    {( 5 * 8),      ACPI_RESTAG_REGISTERBITOFFSET},
    {( 6 * 8),      ACPI_RESTAG_ACCESSSIZE},
    {( 7 * 8),      ACPI_RESTAG_ADDRESS},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmMemory32Tags[] =
{
    {( 3 * 8) + 0,  ACPI_RESTAG_READWRITETYPE},
    {( 4 * 8),      ACPI_RESTAG_MINADDR},
    {( 8 * 8),      ACPI_RESTAG_MAXADDR},
    {(12 * 8),      ACPI_RESTAG_ALIGNMENT},
    {(16 * 8),      ACPI_RESTAG_LENGTH},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmFixedMemory32Tags[] =
{
    {( 3 * 8) + 0,  ACPI_RESTAG_READWRITETYPE},
    {( 4 * 8),      ACPI_RESTAG_BASEADDRESS},
    {( 8 * 8),      ACPI_RESTAG_LENGTH},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmInterruptTags[] =
{
    {( 3 * 8) + 1,  ACPI_RESTAG_INTERRUPTTYPE},
    {( 3 * 8) + 2,  ACPI_RESTAG_INTERRUPTLEVEL},
    {( 3 * 8) + 3,  ACPI_RESTAG_INTERRUPTSHARE},
    {( 5 * 8),      ACPI_RESTAG_INTERRUPT},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmAddress16Tags[] =
{
    {( 4 * 8) + 1,  ACPI_RESTAG_DECODE},
    {( 4 * 8) + 2,  ACPI_RESTAG_MINTYPE},
    {( 4 * 8) + 3,  ACPI_RESTAG_MAXTYPE},
    {( 6 * 8),      ACPI_RESTAG_GRANULARITY},
    {( 8 * 8),      ACPI_RESTAG_MINADDR},
    {(10 * 8),      ACPI_RESTAG_MAXADDR},
    {(12 * 8),      ACPI_RESTAG_TRANSLATION},
    {(14 * 8),      ACPI_RESTAG_LENGTH},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmAddress32Tags[] =
{
    {( 4 * 8) + 1,  ACPI_RESTAG_DECODE},
    {( 4 * 8) + 2,  ACPI_RESTAG_MINTYPE},
    {( 4 * 8) + 3,  ACPI_RESTAG_MAXTYPE},
    {( 6 * 8),      ACPI_RESTAG_GRANULARITY},
    {(10 * 8),      ACPI_RESTAG_MINADDR},
    {(14 * 8),      ACPI_RESTAG_MAXADDR},
    {(18 * 8),      ACPI_RESTAG_TRANSLATION},
    {(22 * 8),      ACPI_RESTAG_LENGTH},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmAddress64Tags[] =
{
    {( 4 * 8) + 1,  ACPI_RESTAG_DECODE},
    {( 4 * 8) + 2,  ACPI_RESTAG_MINTYPE},
    {( 4 * 8) + 3,  ACPI_RESTAG_MAXTYPE},
    {( 6 * 8),      ACPI_RESTAG_GRANULARITY},
    {(14 * 8),      ACPI_RESTAG_MINADDR},
    {(22 * 8),      ACPI_RESTAG_MAXADDR},
    {(30 * 8),      ACPI_RESTAG_TRANSLATION},
    {(38 * 8),      ACPI_RESTAG_LENGTH},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmExtendedAddressTags[] =
{
    {( 4 * 8) + 1,  ACPI_RESTAG_DECODE},
    {( 4 * 8) + 2,  ACPI_RESTAG_MINTYPE},
    {( 4 * 8) + 3,  ACPI_RESTAG_MAXTYPE},
    {( 8 * 8),      ACPI_RESTAG_GRANULARITY},
    {(16 * 8),      ACPI_RESTAG_MINADDR},
    {(24 * 8),      ACPI_RESTAG_MAXADDR},
    {(32 * 8),      ACPI_RESTAG_TRANSLATION},
    {(40 * 8),      ACPI_RESTAG_LENGTH},
    {(48 * 8),      ACPI_RESTAG_TYPESPECIFICATTRIBUTES},
    {0,             NULL}
};

/* Subtype tables for GPIO descriptors */

static const ACPI_RESOURCE_TAG      AcpiDmGpioIntTags[] =
{
    {( 7 * 8) + 0,  ACPI_RESTAG_MODE},
    {( 7 * 8) + 1,  ACPI_RESTAG_POLARITY},
    {( 7 * 8) + 3,  ACPI_RESTAG_INTERRUPTSHARE},
    {( 9 * 8),      ACPI_RESTAG_PINCONFIG},
    {(10 * 8),      ACPI_RESTAG_DRIVESTRENGTH},
    {(12 * 8),      ACPI_RESTAG_DEBOUNCETIME},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmGpioIoTags[] =
{
    {( 7 * 8) + 0,  ACPI_RESTAG_IORESTRICTION},
    {( 7 * 8) + 3,  ACPI_RESTAG_INTERRUPTSHARE},
    {( 9 * 8),      ACPI_RESTAG_PINCONFIG},
    {(10 * 8),      ACPI_RESTAG_DRIVESTRENGTH},
    {(12 * 8),      ACPI_RESTAG_DEBOUNCETIME},
    {0,             NULL}
};

/* Subtype tables for SerialBus descriptors */

static const ACPI_RESOURCE_TAG      AcpiDmI2cSerialBusTags[] =
{
    {( 6 * 8) + 0,  ACPI_RESTAG_SLAVEMODE},
    {( 6 * 8) + 2,  ACPI_RESTAG_INTERRUPTSHARE},    /* V2 - ACPI 6.0 */
    {( 7 * 8) + 0,  ACPI_RESTAG_MODE},
    {(12 * 8),      ACPI_RESTAG_SPEED},
    {(16 * 8),      ACPI_RESTAG_ADDRESS},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmSpiSerialBusTags[] =
{
    {( 6 * 8) + 0,  ACPI_RESTAG_SLAVEMODE},
    {( 6 * 8) + 2,  ACPI_RESTAG_INTERRUPTSHARE},    /* V2 - ACPI 6.0 */
    {( 7 * 8) + 0,  ACPI_RESTAG_MODE},
    {( 7 * 8) + 1,  ACPI_RESTAG_DEVICEPOLARITY},
    {(12 * 8),      ACPI_RESTAG_SPEED},
    {(16 * 8),      ACPI_RESTAG_LENGTH},
    {(17 * 8),      ACPI_RESTAG_PHASE},
    {(18 * 8),      ACPI_RESTAG_POLARITY},
    {(19 * 8),      ACPI_RESTAG_ADDRESS},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmUartSerialBusTags[] =
{
    {( 6 * 8) + 0,  ACPI_RESTAG_SLAVEMODE},         /* Note: not part of original macro */
    {( 6 * 8) + 2,  ACPI_RESTAG_INTERRUPTSHARE},    /* V2 - ACPI 6.0 */
    {( 7 * 8) + 0,  ACPI_RESTAG_FLOWCONTROL},
    {( 7 * 8) + 2,  ACPI_RESTAG_STOPBITS},
    {( 7 * 8) + 4,  ACPI_RESTAG_LENGTH},
    {( 7 * 8) + 7,  ACPI_RESTAG_ENDIANNESS},
    {(12 * 8),      ACPI_RESTAG_SPEED},
    {(16 * 8),      ACPI_RESTAG_LENGTH_RX},
    {(18 * 8),      ACPI_RESTAG_LENGTH_TX},
    {(20 * 8),      ACPI_RESTAG_PARITY},
    {(21 * 8),      ACPI_RESTAG_LINE},
    {0,             NULL}
};

/* Subtype tables for PinFunction descriptor */

static const ACPI_RESOURCE_TAG      AcpiDmPinFunctionTags[] =
{
    {( 4 * 8),      ACPI_RESTAG_INTERRUPTSHARE},
    {( 6 * 8),      ACPI_RESTAG_PINCONFIG},
    {( 7 * 8),      ACPI_RESTAG_FUNCTION},
    {0,             NULL}
};

/* Subtype tables for PinConfig descriptor */

static const ACPI_RESOURCE_TAG      AcpiDmPinConfigTags[] =
{
    {( 4 * 8),      ACPI_RESTAG_INTERRUPTSHARE},
    {( 6 * 8),      ACPI_RESTAG_PINCONFIG_TYPE},
    {( 7 * 8),      ACPI_RESTAG_PINCONFIG_VALUE},
    {0,             NULL}
};

/* Subtype tables for PinGroupFunction descriptor */

static const ACPI_RESOURCE_TAG      AcpiDmPinGroupFunctionTags[] =
{
    {( 6 * 8),      ACPI_RESTAG_FUNCTION},
    {0,             NULL}
};

/* Subtype tables for Address descriptor type-specific flags */

static const ACPI_RESOURCE_TAG      AcpiDmMemoryFlagTags[] =
{
    {( 5 * 8) + 0,  ACPI_RESTAG_READWRITETYPE},
    {( 5 * 8) + 1,  ACPI_RESTAG_MEMTYPE},
    {( 5 * 8) + 3,  ACPI_RESTAG_MEMATTRIBUTES},
    {( 5 * 8) + 5,  ACPI_RESTAG_TYPE},
    {0,             NULL}
};

static const ACPI_RESOURCE_TAG      AcpiDmIoFlagTags[] =
{
    {( 5 * 8) + 0,  ACPI_RESTAG_RANGETYPE},
    {( 5 * 8) + 4,  ACPI_RESTAG_TYPE},
    {( 5 * 8) + 5,  ACPI_RESTAG_TRANSTYPE},
    {0,             NULL}
};


/*
 * Dispatch table used to obtain the correct tag table for a descriptor.
 *
 * A NULL in this table means one of three things:
 * 1) The descriptor ID is reserved and invalid
 * 2) The descriptor has no tags associated with it
 * 3) The descriptor has subtypes and a separate table will be used.
 */
static const ACPI_RESOURCE_TAG      *AcpiGbl_ResourceTags[] =
{
    /* Small descriptors */

    NULL,                           /* 0x00, Reserved */
    NULL,                           /* 0x01, Reserved */
    NULL,                           /* 0x02, Reserved */
    NULL,                           /* 0x03, Reserved */
    AcpiDmIrqTags,                  /* 0x04, ACPI_RESOURCE_NAME_IRQ_FORMAT */
    AcpiDmDmaTags,                  /* 0x05, ACPI_RESOURCE_NAME_DMA_FORMAT */
    NULL,                           /* 0x06, ACPI_RESOURCE_NAME_START_DEPENDENT */
    NULL,                           /* 0x07, ACPI_RESOURCE_NAME_END_DEPENDENT */
    AcpiDmIoTags,                   /* 0x08, ACPI_RESOURCE_NAME_IO_PORT */
    AcpiDmFixedIoTags,              /* 0x09, ACPI_RESOURCE_NAME_FIXED_IO_PORT */
    AcpiDmFixedDmaTags,             /* 0x0A, ACPI_RESOURCE_NAME_FIXED_DMA */
    NULL,                           /* 0x0B, Reserved */
    NULL,                           /* 0x0C, Reserved */
    NULL,                           /* 0x0D, Reserved */
    NULL,                           /* 0x0E, ACPI_RESOURCE_NAME_SMALL_VENDOR */
    NULL,                           /* 0x0F, ACPI_RESOURCE_NAME_END_TAG (not used) */

    /* Large descriptors */

    NULL,                           /* 0x00, Reserved */
    AcpiDmMemory24Tags,             /* 0x01, ACPI_RESOURCE_NAME_MEMORY_24 */
    AcpiDmRegisterTags,             /* 0x02, ACPI_RESOURCE_NAME_GENERIC_REGISTER */
    NULL,                           /* 0x03, Reserved */
    NULL,                           /* 0x04, ACPI_RESOURCE_NAME_LARGE_VENDOR */
    AcpiDmMemory32Tags,             /* 0x05, ACPI_RESOURCE_NAME_MEMORY_32 */
    AcpiDmFixedMemory32Tags,        /* 0x06, ACPI_RESOURCE_NAME_FIXED_MEMORY_32 */
    AcpiDmAddress32Tags,            /* 0x07, ACPI_RESOURCE_NAME_DWORD_ADDRESS_SPACE */
    AcpiDmAddress16Tags,            /* 0x08, ACPI_RESOURCE_NAME_WORD_ADDRESS_SPACE */
    AcpiDmInterruptTags,            /* 0x09, ACPI_RESOURCE_NAME_EXTENDED_XRUPT */
    AcpiDmAddress64Tags,            /* 0x0A, ACPI_RESOURCE_NAME_QWORD_ADDRESS_SPACE */
    AcpiDmExtendedAddressTags,      /* 0x0B, ACPI_RESOURCE_NAME_EXTENDED_ADDRESS_SPACE */
    NULL,                           /* 0x0C, ACPI_RESOURCE_NAME_GPIO - Use Subtype table below */
    AcpiDmPinFunctionTags,          /* 0x0D, ACPI_RESOURCE_NAME_PIN_FUNCTION */
    NULL,                           /* 0x0E, ACPI_RESOURCE_NAME_SERIAL_BUS - Use Subtype table below */
    AcpiDmPinConfigTags,            /* 0x0F, ACPI_RESOURCE_NAME_PIN_CONFIG */
    NULL,                           /* 0x10, ACPI_RESOURCE_NAME_PIN_GROUP */
    AcpiDmPinGroupFunctionTags,     /* 0x11, ACPI_RESOURCE_NAME_PIN_GROUP_FUNCTION */
    AcpiDmPinConfigTags,            /* 0x12, ACPI_RESOURCE_NAME_PIN_GROUP_CONFIG - Same as PinConfig */
};

/* GPIO Subtypes */

static const ACPI_RESOURCE_TAG      *AcpiGbl_GpioResourceTags[] =
{
    AcpiDmGpioIntTags,              /* 0x00 Interrupt Connection */
    AcpiDmGpioIoTags                /* 0x01 I/O Connection */
};

/* Serial Bus Subtypes */

static const ACPI_RESOURCE_TAG      *AcpiGbl_SerialResourceTags[] =
{
    NULL,                           /* 0x00 Reserved */
    AcpiDmI2cSerialBusTags,         /* 0x01 I2C SerialBus */
    AcpiDmSpiSerialBusTags,         /* 0x02 SPI SerialBus */
    AcpiDmUartSerialBusTags         /* 0x03 UART SerialBus */
};

/*
 * Globals used to generate unique resource descriptor names. We use names that
 * start with underscore and a prefix letter that is not used by other ACPI
 * reserved names. To this, we append hex 0x00 through 0xFF. These 5 prefixes
 * allow for 5*256 = 1280 unique names, probably sufficient for any single ASL
 * file. If this becomes too small, we can use alpha+numerals for a total
 * of 5*36*36 = 6480.
 */
#define ACPI_NUM_RES_PREFIX     5

static UINT32                   AcpiGbl_NextResourceId = 0;
static UINT8                    AcpiGbl_NextPrefix = 0;
static char                     AcpiGbl_Prefix[ACPI_NUM_RES_PREFIX] =
                                    {'Y','Z','J','K','X'};


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmCheckResourceReference
 *
 * PARAMETERS:  Op                  - Parse Op for the AML opcode
 *              WalkState           - Current walk state (with valid scope)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert a reference to a resource descriptor to a symbolic
 *              reference if possible
 *
 * NOTE:        Bit index is used to transparently handle both resource bit
 *              fields and byte fields.
 *
 ******************************************************************************/

void
AcpiDmCheckResourceReference (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    ACPI_PARSE_OBJECT       *BufferNameOp;
    ACPI_PARSE_OBJECT       *IndexOp;
    ACPI_NAMESPACE_NODE     *BufferNode;
    ACPI_NAMESPACE_NODE     *ResourceNode;
    const ACPI_OPCODE_INFO  *OpInfo;
    UINT32                  BitIndex;


    /* We are only interested in the CreateXxxxField opcodes */

    OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);
    if (OpInfo->Type != AML_TYPE_CREATE_FIELD)
    {
        return;
    }

    /* Get the buffer term operand */

    BufferNameOp = AcpiPsGetDepthNext (NULL, Op);

    /* Must be a named buffer, not an arg or local or method call */

    if (BufferNameOp->Common.AmlOpcode != AML_INT_NAMEPATH_OP)
    {
        return;
    }

    /* Get the Index term, must be an integer constant to convert */

    IndexOp = BufferNameOp->Common.Next;

    /* Major cheat: The Node field is also used for the Tag ptr. Clear it now */

    IndexOp->Common.Node = NULL;

    OpInfo = AcpiPsGetOpcodeInfo (IndexOp->Common.AmlOpcode);
    if (OpInfo->ObjectType != ACPI_TYPE_INTEGER)
    {
        return;
    }

    /* Get the bit offset of the descriptor within the buffer */

    if ((Op->Common.AmlOpcode == AML_CREATE_BIT_FIELD_OP) ||
        (Op->Common.AmlOpcode == AML_CREATE_FIELD_OP))
    {
        /* Index operand is a bit offset */

        BitIndex = (UINT32) IndexOp->Common.Value.Integer;
    }
    else
    {
        /* Index operand is a byte offset, convert to bits */

        BitIndex = (UINT32) ACPI_MUL_8 (IndexOp->Common.Value.Integer);
    }

    /* Lookup the buffer in the namespace */

    Status = AcpiNsLookup (WalkState->ScopeInfo,
        BufferNameOp->Common.Value.String, ACPI_TYPE_BUFFER,
        ACPI_IMODE_EXECUTE, ACPI_NS_SEARCH_PARENT, WalkState,
        &BufferNode);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Validate object type, we must have a buffer */

    if (BufferNode->Type != ACPI_TYPE_BUFFER)
    {
        return;
    }

    /* Find the resource descriptor node corresponding to the index */

    ResourceNode = AcpiDmGetResourceNode (BufferNode, BitIndex);
    if (!ResourceNode)
    {
        return;
    }

    /* Translate the Index to a resource tag pathname */

    AcpiGetTagPathname (IndexOp, BufferNode, ResourceNode, BitIndex);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmGetResourceNode
 *
 * PARAMETERS:  BufferNode          - Node for the parent buffer
 *              BitIndex            - Index into the resource descriptor
 *
 * RETURN:      Namespace node for the resource descriptor. NULL if not found
 *
 * DESCRIPTION: Find a resource descriptor that corresponds to the bit index
 *
 ******************************************************************************/

static ACPI_NAMESPACE_NODE *
AcpiDmGetResourceNode (
    ACPI_NAMESPACE_NODE     *BufferNode,
    UINT32                  BitIndex)
{
    ACPI_NAMESPACE_NODE     *Node;
    UINT32                  ByteIndex = ACPI_DIV_8 (BitIndex);


    /*
     * Child list contains an entry for each resource descriptor. Find
     * the descriptor that corresponds to the Index.
     *
     * If there are no children, this is not a resource template
     */
    Node = BufferNode->Child;
    while (Node)
    {
        /*
         * Check if the Index falls within this resource.
         *
         * Value contains the resource offset, Object contains the resource
         * length (both in bytes)
         */
        if ((ByteIndex >= Node->Value) &&
            (ByteIndex < (Node->Value + Node->Length)))
        {
            return (Node);
        }

        Node = Node->Peer;
    }

    return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiGetTagPathname
 *
 * PARAMETERS:  BufferNode          - Node for the parent buffer
 *              ResourceNode        - Node for a resource descriptor
 *              BitIndex            - Index into the resource descriptor
 *
 * RETURN:      Full pathname for a resource tag. NULL if no match.
 *              Path is returned in AML (packed) format.
 *
 * DESCRIPTION: Convert a BitIndex into a symbolic resource tag (full pathname)
 *
 ******************************************************************************/

static char *
AcpiGetTagPathname (
    ACPI_PARSE_OBJECT       *IndexOp,
    ACPI_NAMESPACE_NODE     *BufferNode,
    ACPI_NAMESPACE_NODE     *ResourceNode,
    UINT32                  BitIndex)
{
    ACPI_STATUS             Status;
    UINT32                  ResourceBitIndex;
    UINT8                   ResourceTableIndex;
    ACPI_SIZE               RequiredSize;
    char                    *Pathname;
    AML_RESOURCE            *Aml;
    ACPI_PARSE_OBJECT       *Op;
    char                    *InternalPath;
    char                    *Tag;


    /* Get the Op that contains the actual buffer data */

    Op = BufferNode->Op->Common.Value.Arg;
    Op = Op->Common.Next;
    if (!Op)
    {
        return (NULL);
    }

    /* Get the individual resource descriptor and validate it */

    Aml = ACPI_CAST_PTR (
        AML_RESOURCE, &Op->Named.Data[ResourceNode->Value]);

    Status = AcpiUtValidateResource (NULL, Aml, &ResourceTableIndex);
    if (ACPI_FAILURE (Status))
    {
        return (NULL);
    }

    /* Get offset into this descriptor (from offset into entire buffer) */

    ResourceBitIndex = BitIndex - ACPI_MUL_8 (ResourceNode->Value);

    /* Get the tag associated with this resource descriptor and offset */

    Tag = AcpiDmGetResourceTag (ResourceBitIndex, Aml, ResourceTableIndex);
    if (!Tag)
    {
        return (NULL);
    }

    /*
     * Now that we know that we have a reference that can be converted to a
     * symbol, change the name of the resource to a unique name.
     */
    AcpiDmUpdateResourceName (ResourceNode);

    /* Get the full pathname to the parent buffer */

    RequiredSize = AcpiNsBuildNormalizedPath (BufferNode, NULL, 0, FALSE);
    if (!RequiredSize)
    {
        return (NULL);
    }

    Pathname = ACPI_ALLOCATE_ZEROED (RequiredSize + ACPI_PATH_SEGMENT_LENGTH);
    if (!Pathname)
    {
        return (NULL);
    }

    (void) AcpiNsBuildNormalizedPath (BufferNode, Pathname,
        RequiredSize, FALSE);

    /*
     * Create the full path to the resource and tag by: remove the buffer name,
     * append the resource descriptor name, append a dot, append the tag name.
     *
     * TBD: Always using the full path is a bit brute force, the path can be
     * often be optimized with carats (if the original buffer namepath is a
     * single nameseg). This doesn't really matter, because these paths do not
     * end up in the final compiled AML, it's just an appearance issue for the
     * disassembled code.
     */
    Pathname[strlen (Pathname) - ACPI_NAME_SIZE] = 0;
    strncat (Pathname, ResourceNode->Name.Ascii, ACPI_NAME_SIZE);
    strcat (Pathname, ".");
    strncat (Pathname, Tag, ACPI_NAME_SIZE);

    /* Internalize the namepath to AML format */

    AcpiNsInternalizeName (Pathname, &InternalPath);
    ACPI_FREE (Pathname);

    /* Update the Op with the symbol */

    AcpiPsInitOp (IndexOp, AML_INT_NAMEPATH_OP);
    IndexOp->Common.Value.String = InternalPath;

    /*
     * We will need the tag later. Cheat by putting it in the Node field.
     * Note, Tag is a const that is part of a lookup table.
     */
    IndexOp->Common.Node = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, Tag);
    return (InternalPath);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmUpdateResourceName
 *
 * PARAMETERS:  ResourceNode        - Node for a resource descriptor
 *
 * RETURN:      Stores new name in the ResourceNode
 *
 * DESCRIPTION: Create a new, unique name for a resource descriptor. Used by
 *              both the disassembly of the descriptor itself and any symbolic
 *              references to the descriptor. Ignored if a unique name has
 *              already been assigned to the resource.
 *
 * NOTE: Single threaded, suitable for applications only!
 *
 ******************************************************************************/

static void
AcpiDmUpdateResourceName (
    ACPI_NAMESPACE_NODE     *ResourceNode)
{
    char                    Name[ACPI_NAME_SIZE];


    /* Ignore if a unique name has already been assigned */

    if (ResourceNode->Name.Integer != ACPI_DEFAULT_RESNAME)
    {
        return;
    }

    /* Generate a new ACPI name for the descriptor */

    Name[0] = '_';
    Name[1] = AcpiGbl_Prefix[AcpiGbl_NextPrefix];
    Name[2] = AcpiUtHexToAsciiChar ((UINT64) AcpiGbl_NextResourceId, 4);
    Name[3] = AcpiUtHexToAsciiChar ((UINT64) AcpiGbl_NextResourceId, 0);

    /* Update globals for next name */

    AcpiGbl_NextResourceId++;
    if (AcpiGbl_NextResourceId >= 256)
    {
        AcpiGbl_NextResourceId = 0;
        AcpiGbl_NextPrefix++;

        if (AcpiGbl_NextPrefix > ACPI_NUM_RES_PREFIX)
        {
            AcpiGbl_NextPrefix = 0;
        }
    }

    /* Change the resource descriptor name */

    ResourceNode->Name.Integer = *ACPI_CAST_PTR (UINT32, &Name[0]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmGetResourceTag
 *
 * PARAMETERS:  BitIndex            - Index into the resource descriptor
 *              Resource            - Pointer to the raw resource data
 *              ResourceIndex       - Index corresponding to the resource type
 *
 * RETURN:      Pointer to the resource tag (ACPI_NAME). NULL if no match.
 *
 * DESCRIPTION: Convert a BitIndex into a symbolic resource tag.
 *
 * Note: ResourceIndex should be previously validated and guaranteed to ve
 *       valid.
 *
 ******************************************************************************/

static char *
AcpiDmGetResourceTag (
    UINT32                  BitIndex,
    AML_RESOURCE            *Resource,
    UINT8                   ResourceIndex)
{
    const ACPI_RESOURCE_TAG *TagList;
    char                    *Tag = NULL;


    /* Get the tag list for this resource descriptor type */

    TagList = AcpiGbl_ResourceTags[ResourceIndex];

    /*
     * Handle descriptors that have multiple subtypes
     */
    switch (Resource->DescriptorType)
    {
    case ACPI_RESOURCE_NAME_ADDRESS16:
    case ACPI_RESOURCE_NAME_ADDRESS32:
    case ACPI_RESOURCE_NAME_ADDRESS64:
    case ACPI_RESOURCE_NAME_EXTENDED_ADDRESS64:
        /*
         * Subtype differentiation is the flags.
         * Kindof brute force, but just blindly search for an index match
         */
        if (Resource->Address.ResourceType == ACPI_ADDRESS_TYPE_MEMORY_RANGE)
        {
            Tag = AcpiDmSearchTagList (BitIndex, AcpiDmMemoryFlagTags);
        }
        else if (Resource->Address.ResourceType == ACPI_ADDRESS_TYPE_IO_RANGE)
        {
            Tag = AcpiDmSearchTagList (BitIndex, AcpiDmIoFlagTags);
        }

        /* If we found a match, all done. Else, drop to normal search below */

        if (Tag)
        {
            return (Tag);
        }
        break;

    case ACPI_RESOURCE_NAME_GPIO:

        /* GPIO connection has 2 subtypes: Interrupt and I/O */

        if (Resource->Gpio.ConnectionType > AML_RESOURCE_MAX_GPIOTYPE)
        {
            return (NULL);
        }

        TagList = AcpiGbl_GpioResourceTags[Resource->Gpio.ConnectionType];
        break;

    case ACPI_RESOURCE_NAME_SERIAL_BUS:

        /* SerialBus has 3 subtypes: I2C, SPI, and UART */

        if ((Resource->CommonSerialBus.Type == 0) ||
            (Resource->CommonSerialBus.Type > AML_RESOURCE_MAX_SERIALBUSTYPE))
        {
            return (NULL);
        }

        TagList = AcpiGbl_SerialResourceTags[Resource->CommonSerialBus.Type];
        break;

    default:

        break;
    }

    /* Search for a match against the BitIndex */

    if (TagList)
    {
        Tag = AcpiDmSearchTagList (BitIndex, TagList);
    }

    return (Tag);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmSearchTagList
 *
 * PARAMETERS:  BitIndex            - Index into the resource descriptor
 *              TagList             - List to search
 *
 * RETURN:      Pointer to a tag (ACPI_NAME). NULL if no match found.
 *
 * DESCRIPTION: Search a tag list for a match to the input BitIndex. Matches
 *              a fixed offset to a symbolic resource tag name.
 *
 ******************************************************************************/

static char *
AcpiDmSearchTagList (
    UINT32                  BitIndex,
    const ACPI_RESOURCE_TAG *TagList)
{

    /*
     * Walk the null-terminated tag list to find a matching bit offset.
     * We are looking for an exact match.
     */
    for ( ; TagList->Tag; TagList++)
    {
        if (BitIndex == TagList->BitIndex)
        {
            return (TagList->Tag);
        }
    }

    /* A matching offset was not found */

    return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmFindResources
 *
 * PARAMETERS:  Root                - Root of the parse tree
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add all ResourceTemplate declarations to the namespace. Each
 *              resource descriptor in each template is given a node -- used
 *              for later conversion of resource references to symbolic refs.
 *
 ******************************************************************************/

void
AcpiDmFindResources (
    ACPI_PARSE_OBJECT       *Root)
{
    ACPI_PARSE_OBJECT       *Op = Root;
    ACPI_PARSE_OBJECT       *Parent;


    /* Walk the entire parse tree */

    while (Op)
    {
        /* We are interested in Buffer() declarations */

        if (Op->Common.AmlOpcode == AML_BUFFER_OP)
        {
            /* And only declarations of the form Name (XXXX, Buffer()... ) */

            Parent = Op->Common.Parent;
            if (Parent->Common.AmlOpcode == AML_NAME_OP)
            {
                /*
                 * If the buffer is a resource template, add the individual
                 * resource descriptors to the namespace, as children of the
                 * buffer node.
                 */
                if (ACPI_SUCCESS (AcpiDmIsResourceTemplate (NULL, Op)))
                {
                    Op->Common.DisasmOpcode = ACPI_DASM_RESOURCE;
                    AcpiDmAddResourcesToNamespace (Parent->Common.Node, Op);
                }
            }
        }

        Op = AcpiPsGetDepthNext (Root, Op);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmAddResourcesToNamespace
 *
 * PARAMETERS:  BufferNode          - Node for the parent buffer
 *              Op                  - Parse op for the buffer
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add an entire resource template to the namespace. Each
 *              resource descriptor is added as a namespace node.
 *
 ******************************************************************************/

static void
AcpiDmAddResourcesToNamespace (
    ACPI_NAMESPACE_NODE     *BufferNode,
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *NextOp;


    /* Get to the ByteData list */

    NextOp = Op->Common.Value.Arg;
    NextOp = NextOp->Common.Next;
    if (!NextOp)
    {
        return;
    }

    /* Set Node and Op to point to each other */

    BufferNode->Op = Op;
    Op->Common.Node = BufferNode;

    /*
     * Insert each resource into the namespace
     * NextOp contains the Aml pointer and the Aml length
     */
    AcpiUtWalkAmlResources (NULL, (UINT8 *) NextOp->Named.Data,
        (ACPI_SIZE) NextOp->Common.Value.Integer,
        AcpiDmAddResourceToNamespace, (void **) BufferNode);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmAddResourceToNamespace
 *
 * PARAMETERS:  ACPI_WALK_AML_CALLBACK
 *              BufferNode              - Node for the parent buffer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add one resource descriptor to the namespace as a child of the
 *              parent buffer. The same name is used for each descriptor. This
 *              is changed later to a unique name if the resource is actually
 *              referenced by an AML operator.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDmAddResourceToNamespace (
    UINT8                   *Aml,
    UINT32                  Length,
    UINT32                  Offset,
    UINT8                   ResourceIndex,
    void                    **Context)
{
    ACPI_STATUS             Status;
    ACPI_GENERIC_STATE      ScopeInfo;
    ACPI_NAMESPACE_NODE     *Node;


    /* TBD: Don't need to add descriptors that have no tags defined? */

    /* Add the resource to the namespace, as child of the buffer */

    ScopeInfo.Scope.Node = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, Context);
    Status = AcpiNsLookup (&ScopeInfo, "_TMP", ACPI_TYPE_LOCAL_RESOURCE,
        ACPI_IMODE_LOAD_PASS2,
        ACPI_NS_NO_UPSEARCH | ACPI_NS_DONT_OPEN_SCOPE | ACPI_NS_PREFIX_IS_SCOPE,
        NULL, &Node);
    if (ACPI_FAILURE (Status))
    {
        return (AE_OK);
    }

    /* Set the name to the default, changed later if resource is referenced */

    Node->Name.Integer = ACPI_DEFAULT_RESNAME;

    /* Save the offset of the descriptor (within the original buffer) */

    Node->Value = Offset;
    Node->Length = Length;
    return (AE_OK);
}
