/*******************************************************************************
 *
 * Module Name: dmresrcs.c - "Small" Resource Descriptor disassembly
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
        ACPI_MODULE_NAME    ("dbresrcs")


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmIrqDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a IRQ descriptor, either Irq() or IrqNoFlags()
 *
 ******************************************************************************/

void
AcpiDmIrqDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    AcpiDmIndent (Level);
    AcpiOsPrintf ("%s (",
        AcpiGbl_IrqDecode [ACPI_GET_1BIT_FLAG (Length)]);

    /* Decode flags byte if present */

    if (Length & 1)
    {
        AcpiOsPrintf ("%s, %s, %s, ",
            AcpiGbl_HeDecode [ACPI_GET_1BIT_FLAG (Resource->Irq.Flags)],
            AcpiGbl_LlDecode [ACPI_EXTRACT_1BIT_FLAG (Resource->Irq.Flags, 3)],
            AcpiGbl_ShrDecode [ACPI_EXTRACT_2BIT_FLAG (Resource->Irq.Flags, 4)]);
    }

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();
    AcpiOsPrintf (")\n");

    AcpiDmIndent (Level + 1);
    AcpiDmBitList (Resource->Irq.IrqMask);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDmaDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a DMA descriptor
 *
 ******************************************************************************/

void
AcpiDmDmaDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    AcpiDmIndent (Level);
    AcpiOsPrintf ("DMA (%s, %s, %s, ",
        AcpiGbl_TypDecode [ACPI_EXTRACT_2BIT_FLAG (Resource->Dma.Flags, 5)],
        AcpiGbl_BmDecode  [ACPI_EXTRACT_1BIT_FLAG (Resource->Dma.Flags, 2)],
        AcpiGbl_SizDecode [ACPI_GET_2BIT_FLAG (Resource->Dma.Flags)]);

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();
    AcpiOsPrintf (")\n");

    AcpiDmIndent (Level + 1);
    AcpiDmBitList (Resource->Dma.DmaChannelMask);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmFixedDmaDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a FixedDMA descriptor
 *
 ******************************************************************************/

void
AcpiDmFixedDmaDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    AcpiDmIndent (Level);
    AcpiOsPrintf ("FixedDMA (0x%4.4X, 0x%4.4X, ",
        Resource->FixedDma.RequestLines,
        Resource->FixedDma.Channels);

    if (Resource->FixedDma.Width <= 5)
    {
        AcpiOsPrintf ("%s, ",
            AcpiGbl_DtsDecode [Resource->FixedDma.Width]);
    }
    else
    {
        AcpiOsPrintf ("%X /* INVALID DMA WIDTH */, ",
            Resource->FixedDma.Width);
    }

    /* Insert a descriptor name */

    AcpiDmDescriptorName ();
    AcpiOsPrintf (")\n");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmIoDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode an IO descriptor
 *
 ******************************************************************************/

void
AcpiDmIoDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    AcpiDmIndent (Level);
    AcpiOsPrintf ("IO (%s,\n",
        AcpiGbl_IoDecode [ACPI_GET_1BIT_FLAG (Resource->Io.Flags)]);

    AcpiDmIndent (Level + 1);
    AcpiDmDumpInteger16 (Resource->Io.Minimum, "Range Minimum");

    AcpiDmIndent (Level + 1);
    AcpiDmDumpInteger16 (Resource->Io.Maximum, "Range Maximum");

    AcpiDmIndent (Level + 1);
    AcpiDmDumpInteger8 (Resource->Io.Alignment, "Alignment");

    AcpiDmIndent (Level + 1);
    AcpiDmDumpInteger8 (Resource->Io.AddressLength, "Length");

    /* Insert a descriptor name */

    AcpiDmIndent (Level + 1);
    AcpiDmDescriptorName ();
    AcpiOsPrintf (")\n");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmFixedIoDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a Fixed IO descriptor
 *
 ******************************************************************************/

void
AcpiDmFixedIoDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    AcpiDmIndent (Level);
    AcpiOsPrintf ("FixedIO (\n");

    AcpiDmIndent (Level + 1);
    AcpiDmDumpInteger16 (Resource->FixedIo.Address, "Address");

    AcpiDmIndent (Level + 1);
    AcpiDmDumpInteger8 (Resource->FixedIo.AddressLength, "Length");

    /* Insert a descriptor name */

    AcpiDmIndent (Level + 1);
    AcpiDmDescriptorName ();
    AcpiOsPrintf (")\n");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmStartDependentDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a Start Dependent functions descriptor
 *
 ******************************************************************************/

void
AcpiDmStartDependentDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    AcpiDmIndent (Level);

    if (Length & 1)
    {
        AcpiOsPrintf ("StartDependentFn (0x%2.2X, 0x%2.2X)\n",
            (UINT32) ACPI_GET_2BIT_FLAG (Resource->StartDpf.Flags),
            (UINT32) ACPI_EXTRACT_2BIT_FLAG (Resource->StartDpf.Flags, 2));
    }
    else
    {
        AcpiOsPrintf ("StartDependentFnNoPri ()\n");
    }

    AcpiDmIndent (Level);
    AcpiOsPrintf ("{\n");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmEndDependentDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode an End Dependent functions descriptor
 *
 ******************************************************************************/

void
AcpiDmEndDependentDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    AcpiDmIndent (Level);
    AcpiOsPrintf ("}\n");
    AcpiDmIndent (Level);
    AcpiOsPrintf ("EndDependentFn ()\n");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmVendorSmallDescriptor
 *
 * PARAMETERS:  Info                - Extra resource info
 *              Resource            - Pointer to the resource descriptor
 *              Length              - Length of the descriptor in bytes
 *              Level               - Current source code indentation level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decode a Vendor Small Descriptor
 *
 ******************************************************************************/

void
AcpiDmVendorSmallDescriptor (
    ACPI_OP_WALK_INFO       *Info,
    AML_RESOURCE            *Resource,
    UINT32                  Length,
    UINT32                  Level)
{

    AcpiDmVendorCommon ("Short",
        ACPI_ADD_PTR (UINT8, Resource, sizeof (AML_RESOURCE_SMALL_HEADER)),
        Length, Level);
}
