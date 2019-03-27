/*******************************************************************************
 *
 * Module Name: rsirq - IRQ resource descriptors
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
        ACPI_MODULE_NAME    ("rsirq")


/*******************************************************************************
 *
 * AcpiRsGetIrq
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsGetIrq[9] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_IRQ,
                        ACPI_RS_SIZE (ACPI_RESOURCE_IRQ),
                        ACPI_RSC_TABLE_SIZE (AcpiRsGetIrq)},

    /* Get the IRQ mask (bytes 1:2) */

    {ACPI_RSC_BITMASK16,ACPI_RS_OFFSET (Data.Irq.Interrupts[0]),
                        AML_OFFSET (Irq.IrqMask),
                        ACPI_RS_OFFSET (Data.Irq.InterruptCount)},

    /* Set default flags (others are zero) */

    {ACPI_RSC_SET8,     ACPI_RS_OFFSET (Data.Irq.Triggering),
                        ACPI_EDGE_SENSITIVE,
                        1},

    /* Get the descriptor length (2 or 3 for IRQ descriptor) */

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.Irq.DescriptorLength),
                        AML_OFFSET (Irq.DescriptorType),
                        0},

    /* All done if no flag byte present in descriptor */

    {ACPI_RSC_EXIT_NE,  ACPI_RSC_COMPARE_AML_LENGTH, 0, 3},

    /* Get flags: Triggering[0], Polarity[3], Sharing[4], Wake[5] */

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Irq.Triggering),
                        AML_OFFSET (Irq.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Irq.Polarity),
                        AML_OFFSET (Irq.Flags),
                        3},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Irq.Shareable),
                        AML_OFFSET (Irq.Flags),
                        4},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Irq.WakeCapable),
                        AML_OFFSET (Irq.Flags),
                        5}
};


/*******************************************************************************
 *
 * AcpiRsSetIrq
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsSetIrq[14] =
{
    /* Start with a default descriptor of length 3 */

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_IRQ,
                        sizeof (AML_RESOURCE_IRQ),
                        ACPI_RSC_TABLE_SIZE (AcpiRsSetIrq)},

    /* Convert interrupt list to 16-bit IRQ bitmask */

    {ACPI_RSC_BITMASK16,ACPI_RS_OFFSET (Data.Irq.Interrupts[0]),
                        AML_OFFSET (Irq.IrqMask),
                        ACPI_RS_OFFSET (Data.Irq.InterruptCount)},

    /* Set flags: Triggering[0], Polarity[3], Sharing[4], Wake[5] */

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Irq.Triggering),
                        AML_OFFSET (Irq.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Irq.Polarity),
                        AML_OFFSET (Irq.Flags),
                        3},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Irq.Shareable),
                        AML_OFFSET (Irq.Flags),
                        4},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Irq.WakeCapable),
                        AML_OFFSET (Irq.Flags),
                        5},

    /*
     * All done if the output descriptor length is required to be 3
     * (i.e., optimization to 2 bytes cannot be attempted)
     */
    {ACPI_RSC_EXIT_EQ,  ACPI_RSC_COMPARE_VALUE,
                        ACPI_RS_OFFSET(Data.Irq.DescriptorLength),
                        3},

    /* Set length to 2 bytes (no flags byte) */

    {ACPI_RSC_LENGTH,   0, 0, sizeof (AML_RESOURCE_IRQ_NOFLAGS)},

    /*
     * All done if the output descriptor length is required to be 2.
     *
     * TBD: Perhaps we should check for error if input flags are not
     * compatible with a 2-byte descriptor.
     */
    {ACPI_RSC_EXIT_EQ,  ACPI_RSC_COMPARE_VALUE,
                        ACPI_RS_OFFSET(Data.Irq.DescriptorLength),
                        2},

    /* Reset length to 3 bytes (descriptor with flags byte) */

    {ACPI_RSC_LENGTH,   0, 0, sizeof (AML_RESOURCE_IRQ)},

    /*
     * Check if the flags byte is necessary. Not needed if the flags are:
     * ACPI_EDGE_SENSITIVE, ACPI_ACTIVE_HIGH, ACPI_EXCLUSIVE
     */
    {ACPI_RSC_EXIT_NE,  ACPI_RSC_COMPARE_VALUE,
                        ACPI_RS_OFFSET (Data.Irq.Triggering),
                        ACPI_EDGE_SENSITIVE},

    {ACPI_RSC_EXIT_NE,  ACPI_RSC_COMPARE_VALUE,
                        ACPI_RS_OFFSET (Data.Irq.Polarity),
                        ACPI_ACTIVE_HIGH},

    {ACPI_RSC_EXIT_NE,  ACPI_RSC_COMPARE_VALUE,
                        ACPI_RS_OFFSET (Data.Irq.Shareable),
                        ACPI_EXCLUSIVE},

    /* We can optimize to a 2-byte IrqNoFlags() descriptor */

    {ACPI_RSC_LENGTH,   0, 0, sizeof (AML_RESOURCE_IRQ_NOFLAGS)}
};


/*******************************************************************************
 *
 * AcpiRsConvertExtIrq
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertExtIrq[10] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_EXTENDED_IRQ,
                        ACPI_RS_SIZE (ACPI_RESOURCE_EXTENDED_IRQ),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertExtIrq)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_EXTENDED_IRQ,
                        sizeof (AML_RESOURCE_EXTENDED_IRQ),
                        0},

    /*
     * Flags: Producer/Consumer[0], Triggering[1], Polarity[2],
     *        Sharing[3], Wake[4]
     */
    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.ExtendedIrq.ProducerConsumer),
                        AML_OFFSET (ExtendedIrq.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.ExtendedIrq.Triggering),
                        AML_OFFSET (ExtendedIrq.Flags),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.ExtendedIrq.Polarity),
                        AML_OFFSET (ExtendedIrq.Flags),
                        2},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.ExtendedIrq.Shareable),
                        AML_OFFSET (ExtendedIrq.Flags),
                        3},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.ExtendedIrq.WakeCapable),
                        AML_OFFSET (ExtendedIrq.Flags),
                        4},

    /* IRQ Table length (Byte4) */

    {ACPI_RSC_COUNT,    ACPI_RS_OFFSET (Data.ExtendedIrq.InterruptCount),
                        AML_OFFSET (ExtendedIrq.InterruptCount),
                        sizeof (UINT32)},

    /* Copy every IRQ in the table, each is 32 bits */

    {ACPI_RSC_MOVE32,   ACPI_RS_OFFSET (Data.ExtendedIrq.Interrupts[0]),
                        AML_OFFSET (ExtendedIrq.Interrupts[0]),
                        0},

    /* Optional ResourceSource (Index and String) */

    {ACPI_RSC_SOURCEX,  ACPI_RS_OFFSET (Data.ExtendedIrq.ResourceSource),
                        ACPI_RS_OFFSET (Data.ExtendedIrq.Interrupts[0]),
                        sizeof (AML_RESOURCE_EXTENDED_IRQ)}
};


/*******************************************************************************
 *
 * AcpiRsConvertDma
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertDma[6] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_DMA,
                        ACPI_RS_SIZE (ACPI_RESOURCE_DMA),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertDma)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_DMA,
                        sizeof (AML_RESOURCE_DMA),
                        0},

    /* Flags: transfer preference, bus mastering, channel speed */

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.Dma.Transfer),
                        AML_OFFSET (Dma.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Dma.BusMaster),
                        AML_OFFSET (Dma.Flags),
                        2},

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.Dma.Type),
                        AML_OFFSET (Dma.Flags),
                        5},

    /* DMA channel mask bits */

    {ACPI_RSC_BITMASK,  ACPI_RS_OFFSET (Data.Dma.Channels[0]),
                        AML_OFFSET (Dma.DmaChannelMask),
                        ACPI_RS_OFFSET (Data.Dma.ChannelCount)}
};


/*******************************************************************************
 *
 * AcpiRsConvertFixedDma
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertFixedDma[4] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_FIXED_DMA,
                        ACPI_RS_SIZE (ACPI_RESOURCE_FIXED_DMA),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertFixedDma)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_FIXED_DMA,
                        sizeof (AML_RESOURCE_FIXED_DMA),
                        0},

    /*
     * These fields are contiguous in both the source and destination:
     * RequestLines
     * Channels
     */
    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.FixedDma.RequestLines),
                        AML_OFFSET (FixedDma.RequestLines),
                        2},

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.FixedDma.Width),
                        AML_OFFSET (FixedDma.Width),
                        1},
};
