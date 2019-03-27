/*******************************************************************************
 *
 * Module Name: utresdecode - Resource descriptor keyword strings
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


#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utresdecode")


#if defined (ACPI_DEBUG_OUTPUT) || \
    defined (ACPI_DISASSEMBLER) || \
    defined (ACPI_DEBUGGER)

/*
 * Strings used to decode resource descriptors.
 * Used by both the disassembler and the debugger resource dump routines
 */
const char                      *AcpiGbl_BmDecode[] =
{
    "NotBusMaster",
    "BusMaster"
};

const char                      *AcpiGbl_ConfigDecode[] =
{
    "0 - Good Configuration",
    "1 - Acceptable Configuration",
    "2 - Suboptimal Configuration",
    "3 - ***Invalid Configuration***",
};

const char                      *AcpiGbl_ConsumeDecode[] =
{
    "ResourceProducer",
    "ResourceConsumer"
};

const char                      *AcpiGbl_DecDecode[] =
{
    "PosDecode",
    "SubDecode"
};

const char                      *AcpiGbl_HeDecode[] =
{
    "Level",
    "Edge"
};

const char                      *AcpiGbl_IoDecode[] =
{
    "Decode10",
    "Decode16"
};

const char                      *AcpiGbl_LlDecode[] =
{
    "ActiveHigh",
    "ActiveLow",
    "ActiveBoth",
    "Reserved"
};

const char                      *AcpiGbl_MaxDecode[] =
{
    "MaxNotFixed",
    "MaxFixed"
};

const char                      *AcpiGbl_MemDecode[] =
{
    "NonCacheable",
    "Cacheable",
    "WriteCombining",
    "Prefetchable"
};

const char                      *AcpiGbl_MinDecode[] =
{
    "MinNotFixed",
    "MinFixed"
};

const char                      *AcpiGbl_MtpDecode[] =
{
    "AddressRangeMemory",
    "AddressRangeReserved",
    "AddressRangeACPI",
    "AddressRangeNVS"
};

const char                      *AcpiGbl_RngDecode[] =
{
    "InvalidRanges",
    "NonISAOnlyRanges",
    "ISAOnlyRanges",
    "EntireRange"
};

const char                      *AcpiGbl_RwDecode[] =
{
    "ReadOnly",
    "ReadWrite"
};

const char                      *AcpiGbl_ShrDecode[] =
{
    "Exclusive",
    "Shared",
    "ExclusiveAndWake",         /* ACPI 5.0 */
    "SharedAndWake"             /* ACPI 5.0 */
};

const char                      *AcpiGbl_SizDecode[] =
{
    "Transfer8",
    "Transfer8_16",
    "Transfer16",
    "InvalidSize"
};

const char                      *AcpiGbl_TrsDecode[] =
{
    "DenseTranslation",
    "SparseTranslation"
};

const char                      *AcpiGbl_TtpDecode[] =
{
    "TypeStatic",
    "TypeTranslation"
};

const char                      *AcpiGbl_TypDecode[] =
{
    "Compatibility",
    "TypeA",
    "TypeB",
    "TypeF"
};

const char                      *AcpiGbl_PpcDecode[] =
{
    "PullDefault",
    "PullUp",
    "PullDown",
    "PullNone"
};

const char                      *AcpiGbl_IorDecode[] =
{
    "IoRestrictionNone",
    "IoRestrictionInputOnly",
    "IoRestrictionOutputOnly",
    "IoRestrictionNoneAndPreserve"
};

const char                      *AcpiGbl_DtsDecode[] =
{
    "Width8bit",
    "Width16bit",
    "Width32bit",
    "Width64bit",
    "Width128bit",
    "Width256bit",
};

/* GPIO connection type */

const char                      *AcpiGbl_CtDecode[] =
{
    "Interrupt",
    "I/O"
};

/* Serial bus type */

const char                      *AcpiGbl_SbtDecode[] =
{
    "/* UNKNOWN serial bus type */",
    "I2C",
    "SPI",
    "UART"
};

/* I2C serial bus access mode */

const char                      *AcpiGbl_AmDecode[] =
{
    "AddressingMode7Bit",
    "AddressingMode10Bit"
};

/* I2C serial bus slave mode */

const char                      *AcpiGbl_SmDecode[] =
{
    "ControllerInitiated",
    "DeviceInitiated"
};

/* SPI serial bus wire mode */

const char                      *AcpiGbl_WmDecode[] =
{
    "FourWireMode",
    "ThreeWireMode"
};

/* SPI serial clock phase */

const char                      *AcpiGbl_CphDecode[] =
{
    "ClockPhaseFirst",
    "ClockPhaseSecond"
};

/* SPI serial bus clock polarity */

const char                      *AcpiGbl_CpoDecode[] =
{
    "ClockPolarityLow",
    "ClockPolarityHigh"
};

/* SPI serial bus device polarity */

const char                      *AcpiGbl_DpDecode[] =
{
    "PolarityLow",
    "PolarityHigh"
};

/* UART serial bus endian */

const char                      *AcpiGbl_EdDecode[] =
{
    "LittleEndian",
    "BigEndian"
};

/* UART serial bus bits per byte */

const char                      *AcpiGbl_BpbDecode[] =
{
    "DataBitsFive",
    "DataBitsSix",
    "DataBitsSeven",
    "DataBitsEight",
    "DataBitsNine",
    "/* UNKNOWN Bits per byte */",
    "/* UNKNOWN Bits per byte */",
    "/* UNKNOWN Bits per byte */"
};

/* UART serial bus stop bits */

const char                      *AcpiGbl_SbDecode[] =
{
    "StopBitsZero",
    "StopBitsOne",
    "StopBitsOnePlusHalf",
    "StopBitsTwo"
};

/* UART serial bus flow control */

const char                      *AcpiGbl_FcDecode[] =
{
    "FlowControlNone",
    "FlowControlHardware",
    "FlowControlXON",
    "/* UNKNOWN flow control keyword */"
};

/* UART serial bus parity type */

const char                      *AcpiGbl_PtDecode[] =
{
    "ParityTypeNone",
    "ParityTypeEven",
    "ParityTypeOdd",
    "ParityTypeMark",
    "ParityTypeSpace",
    "/* UNKNOWN parity keyword */",
    "/* UNKNOWN parity keyword */",
    "/* UNKNOWN parity keyword */"
};

/* PinConfig type */

const char                      *AcpiGbl_PtypDecode[] =
{
    "Default",
    "Bias Pull-up",
    "Bias Pull-down",
    "Bias Default",
    "Bias Disable",
    "Bias High Impedance",
    "Bias Bus Hold",
    "Drive Open Drain",
    "Drive Open Source",
    "Drive Push Pull",
    "Drive Strength",
    "Slew Rate",
    "Input Debounce",
    "Input Schmitt Trigger",
};

#endif
