/*******************************************************************************
 *
 * Module Name: rsaddr - Address resource descriptors (16/32/64)
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
        ACPI_MODULE_NAME    ("rsaddr")


/*******************************************************************************
 *
 * AcpiRsConvertAddress16 - All WORD (16-bit) address resources
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertAddress16[5] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_ADDRESS16,
                        ACPI_RS_SIZE (ACPI_RESOURCE_ADDRESS16),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertAddress16)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_ADDRESS16,
                        sizeof (AML_RESOURCE_ADDRESS16),
                        0},

    /* Resource Type, General Flags, and Type-Specific Flags */

    {ACPI_RSC_ADDRESS,  0, 0, 0},

    /*
     * These fields are contiguous in both the source and destination:
     * Address Granularity
     * Address Range Minimum
     * Address Range Maximum
     * Address Translation Offset
     * Address Length
     */
    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.Address16.Address.Granularity),
                        AML_OFFSET (Address16.Granularity),
                        5},

    /* Optional ResourceSource (Index and String) */

    {ACPI_RSC_SOURCE,   ACPI_RS_OFFSET (Data.Address16.ResourceSource),
                        0,
                        sizeof (AML_RESOURCE_ADDRESS16)}
};


/*******************************************************************************
 *
 * AcpiRsConvertAddress32 - All DWORD (32-bit) address resources
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertAddress32[5] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_ADDRESS32,
                        ACPI_RS_SIZE (ACPI_RESOURCE_ADDRESS32),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertAddress32)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_ADDRESS32,
                        sizeof (AML_RESOURCE_ADDRESS32),
                        0},

    /* Resource Type, General Flags, and Type-Specific Flags */

    {ACPI_RSC_ADDRESS,  0, 0, 0},

    /*
     * These fields are contiguous in both the source and destination:
     * Address Granularity
     * Address Range Minimum
     * Address Range Maximum
     * Address Translation Offset
     * Address Length
     */
    {ACPI_RSC_MOVE32,   ACPI_RS_OFFSET (Data.Address32.Address.Granularity),
                        AML_OFFSET (Address32.Granularity),
                        5},

    /* Optional ResourceSource (Index and String) */

    {ACPI_RSC_SOURCE,   ACPI_RS_OFFSET (Data.Address32.ResourceSource),
                        0,
                        sizeof (AML_RESOURCE_ADDRESS32)}
};


/*******************************************************************************
 *
 * AcpiRsConvertAddress64 - All QWORD (64-bit) address resources
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertAddress64[5] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_ADDRESS64,
                        ACPI_RS_SIZE (ACPI_RESOURCE_ADDRESS64),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertAddress64)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_ADDRESS64,
                        sizeof (AML_RESOURCE_ADDRESS64),
                        0},

    /* Resource Type, General Flags, and Type-Specific Flags */

    {ACPI_RSC_ADDRESS,  0, 0, 0},

    /*
     * These fields are contiguous in both the source and destination:
     * Address Granularity
     * Address Range Minimum
     * Address Range Maximum
     * Address Translation Offset
     * Address Length
     */
    {ACPI_RSC_MOVE64,   ACPI_RS_OFFSET (Data.Address64.Address.Granularity),
                        AML_OFFSET (Address64.Granularity),
                        5},

    /* Optional ResourceSource (Index and String) */

    {ACPI_RSC_SOURCE,   ACPI_RS_OFFSET (Data.Address64.ResourceSource),
                        0,
                        sizeof (AML_RESOURCE_ADDRESS64)}
};


/*******************************************************************************
 *
 * AcpiRsConvertExtAddress64 - All Extended (64-bit) address resources
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertExtAddress64[5] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64,
                        ACPI_RS_SIZE (ACPI_RESOURCE_EXTENDED_ADDRESS64),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertExtAddress64)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_EXTENDED_ADDRESS64,
                        sizeof (AML_RESOURCE_EXTENDED_ADDRESS64),
                        0},

    /* Resource Type, General Flags, and Type-Specific Flags */

    {ACPI_RSC_ADDRESS,  0, 0, 0},

    /* Revision ID */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.ExtAddress64.RevisionID),
                        AML_OFFSET (ExtAddress64.RevisionID),
                        1},
    /*
     * These fields are contiguous in both the source and destination:
     * Address Granularity
     * Address Range Minimum
     * Address Range Maximum
     * Address Translation Offset
     * Address Length
     * Type-Specific Attribute
     */
    {ACPI_RSC_MOVE64,   ACPI_RS_OFFSET (Data.ExtAddress64.Address.Granularity),
                        AML_OFFSET (ExtAddress64.Granularity),
                        6}
};


/*******************************************************************************
 *
 * AcpiRsConvertGeneralFlags - Flags common to all address descriptors
 *
 ******************************************************************************/

static ACPI_RSCONVERT_INFO  AcpiRsConvertGeneralFlags[6] =
{
    {ACPI_RSC_FLAGINIT, 0, AML_OFFSET (Address.Flags),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertGeneralFlags)},

    /* Resource Type (Memory, Io, BusNumber, etc.) */

    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.Address.ResourceType),
                        AML_OFFSET (Address.ResourceType),
                        1},

    /* General Flags - Consume, Decode, MinFixed, MaxFixed */

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Address.ProducerConsumer),
                        AML_OFFSET (Address.Flags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Address.Decode),
                        AML_OFFSET (Address.Flags),
                        1},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Address.MinAddressFixed),
                        AML_OFFSET (Address.Flags),
                        2},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Address.MaxAddressFixed),
                        AML_OFFSET (Address.Flags),
                        3}
};


/*******************************************************************************
 *
 * AcpiRsConvertMemFlags - Flags common to Memory address descriptors
 *
 ******************************************************************************/

static ACPI_RSCONVERT_INFO  AcpiRsConvertMemFlags[5] =
{
    {ACPI_RSC_FLAGINIT, 0, AML_OFFSET (Address.SpecificFlags),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertMemFlags)},

    /* Memory-specific flags */

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Address.Info.Mem.WriteProtect),
                        AML_OFFSET (Address.SpecificFlags),
                        0},

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.Address.Info.Mem.Caching),
                        AML_OFFSET (Address.SpecificFlags),
                        1},

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.Address.Info.Mem.RangeType),
                        AML_OFFSET (Address.SpecificFlags),
                        3},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Address.Info.Mem.Translation),
                        AML_OFFSET (Address.SpecificFlags),
                        5}
};


/*******************************************************************************
 *
 * AcpiRsConvertIoFlags - Flags common to I/O address descriptors
 *
 ******************************************************************************/

static ACPI_RSCONVERT_INFO  AcpiRsConvertIoFlags[4] =
{
    {ACPI_RSC_FLAGINIT, 0, AML_OFFSET (Address.SpecificFlags),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertIoFlags)},

    /* I/O-specific flags */

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.Address.Info.Io.RangeType),
                        AML_OFFSET (Address.SpecificFlags),
                        0},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Address.Info.Io.Translation),
                        AML_OFFSET (Address.SpecificFlags),
                        4},

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Address.Info.Io.TranslationType),
                        AML_OFFSET (Address.SpecificFlags),
                        5}
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsGetAddressCommon
 *
 * PARAMETERS:  Resource            - Pointer to the internal resource struct
 *              Aml                 - Pointer to the AML resource descriptor
 *
 * RETURN:      TRUE if the ResourceType field is OK, FALSE otherwise
 *
 * DESCRIPTION: Convert common flag fields from a raw AML resource descriptor
 *              to an internal resource descriptor
 *
 ******************************************************************************/

BOOLEAN
AcpiRsGetAddressCommon (
    ACPI_RESOURCE           *Resource,
    AML_RESOURCE            *Aml)
{
    ACPI_FUNCTION_ENTRY ();


    /* Validate the Resource Type */

    if ((Aml->Address.ResourceType > 2) &&
        (Aml->Address.ResourceType < 0xC0))
    {
        return (FALSE);
    }

    /* Get the Resource Type and General Flags */

    (void) AcpiRsConvertAmlToResource (
        Resource, Aml, AcpiRsConvertGeneralFlags);

    /* Get the Type-Specific Flags (Memory and I/O descriptors only) */

    if (Resource->Data.Address.ResourceType == ACPI_MEMORY_RANGE)
    {
        (void) AcpiRsConvertAmlToResource (
            Resource, Aml, AcpiRsConvertMemFlags);
    }
    else if (Resource->Data.Address.ResourceType == ACPI_IO_RANGE)
    {
        (void) AcpiRsConvertAmlToResource (
            Resource, Aml, AcpiRsConvertIoFlags);
    }
    else
    {
        /* Generic resource type, just grab the TypeSpecific byte */

        Resource->Data.Address.Info.TypeSpecific =
            Aml->Address.SpecificFlags;
    }

    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiRsSetAddressCommon
 *
 * PARAMETERS:  Aml                 - Pointer to the AML resource descriptor
 *              Resource            - Pointer to the internal resource struct
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert common flag fields from a resource descriptor to an
 *              AML descriptor
 *
 ******************************************************************************/

void
AcpiRsSetAddressCommon (
    AML_RESOURCE            *Aml,
    ACPI_RESOURCE           *Resource)
{
    ACPI_FUNCTION_ENTRY ();


    /* Set the Resource Type and General Flags */

    (void) AcpiRsConvertResourceToAml (
        Resource, Aml, AcpiRsConvertGeneralFlags);

    /* Set the Type-Specific Flags (Memory and I/O descriptors only) */

    if (Resource->Data.Address.ResourceType == ACPI_MEMORY_RANGE)
    {
        (void) AcpiRsConvertResourceToAml (
            Resource, Aml, AcpiRsConvertMemFlags);
    }
    else if (Resource->Data.Address.ResourceType == ACPI_IO_RANGE)
    {
        (void) AcpiRsConvertResourceToAml (
            Resource, Aml, AcpiRsConvertIoFlags);
    }
    else
    {
        /* Generic resource type, just copy the TypeSpecific byte */

        Aml->Address.SpecificFlags =
            Resource->Data.Address.Info.TypeSpecific;
    }
}
