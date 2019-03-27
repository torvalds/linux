/*******************************************************************************
 *
 * Module Name: rsio - IO and DMA resource descriptors
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
        ACPI_MODULE_NAME    ("rsio")


/*******************************************************************************
 *
 * AcpiRsConvertIo
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertIo[5] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_IO,
                        ACPI_RS_SIZE (ACPI_RESOURCE_IO),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertIo)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_IO,
                        sizeof (AML_RESOURCE_IO),
                        0},

    /* Decode flag */

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.Io.IoDecode),
                        AML_OFFSET (Io.Flags),
                        0},
    /*
     * These fields are contiguous in both the source and destination:
     * Address Alignment
     * Length
     * Minimum Base Address
     * Maximum Base Address
     */
    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.Io.Alignment),
                        AML_OFFSET (Io.Alignment),
                        2},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.Io.Minimum),
                        AML_OFFSET (Io.Minimum),
                        2}
};


/*******************************************************************************
 *
 * AcpiRsConvertFixedIo
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertFixedIo[4] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_FIXED_IO,
                        ACPI_RS_SIZE (ACPI_RESOURCE_FIXED_IO),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertFixedIo)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_FIXED_IO,
                        sizeof (AML_RESOURCE_FIXED_IO),
                        0},
    /*
     * These fields are contiguous in both the source and destination:
     * Base Address
     * Length
     */
    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.FixedIo.AddressLength),
                        AML_OFFSET (FixedIo.AddressLength),
                        1},

    {ACPI_RSC_MOVE16,   ACPI_RS_OFFSET (Data.FixedIo.Address),
                        AML_OFFSET (FixedIo.Address),
                        1}
};


/*******************************************************************************
 *
 * AcpiRsConvertGenericReg
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO     AcpiRsConvertGenericReg[4] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_GENERIC_REGISTER,
                        ACPI_RS_SIZE (ACPI_RESOURCE_GENERIC_REGISTER),
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertGenericReg)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_GENERIC_REGISTER,
                        sizeof (AML_RESOURCE_GENERIC_REGISTER),
                        0},
    /*
     * These fields are contiguous in both the source and destination:
     * Address Space ID
     * Register Bit Width
     * Register Bit Offset
     * Access Size
     */
    {ACPI_RSC_MOVE8,    ACPI_RS_OFFSET (Data.GenericReg.SpaceId),
                        AML_OFFSET (GenericReg.AddressSpaceId),
                        4},

    /* Get the Register Address */

    {ACPI_RSC_MOVE64,   ACPI_RS_OFFSET (Data.GenericReg.Address),
                        AML_OFFSET (GenericReg.Address),
                        1}
};


/*******************************************************************************
 *
 * AcpiRsConvertEndDpf
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO   AcpiRsConvertEndDpf[2] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_END_DEPENDENT,
                        ACPI_RS_SIZE_MIN,
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertEndDpf)},

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_END_DEPENDENT,
                        sizeof (AML_RESOURCE_END_DEPENDENT),
                        0}
};


/*******************************************************************************
 *
 * AcpiRsConvertEndTag
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO   AcpiRsConvertEndTag[2] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_END_TAG,
                        ACPI_RS_SIZE_MIN,
                        ACPI_RSC_TABLE_SIZE (AcpiRsConvertEndTag)},

    /*
     * Note: The checksum field is set to zero, meaning that the resource
     * data is treated as if the checksum operation succeeded.
     * (ACPI Spec 1.0b Section 6.4.2.8)
     */
    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_END_TAG,
                        sizeof (AML_RESOURCE_END_TAG),
                        0}
};


/*******************************************************************************
 *
 * AcpiRsGetStartDpf
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO   AcpiRsGetStartDpf[6] =
{
    {ACPI_RSC_INITGET,  ACPI_RESOURCE_TYPE_START_DEPENDENT,
                        ACPI_RS_SIZE (ACPI_RESOURCE_START_DEPENDENT),
                        ACPI_RSC_TABLE_SIZE (AcpiRsGetStartDpf)},

    /* Defaults for Compatibility and Performance priorities */

    {ACPI_RSC_SET8,     ACPI_RS_OFFSET (Data.StartDpf.CompatibilityPriority),
                        ACPI_ACCEPTABLE_CONFIGURATION,
                        2},

    /* Get the descriptor length (0 or 1 for Start Dpf descriptor) */

    {ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET (Data.StartDpf.DescriptorLength),
                        AML_OFFSET (StartDpf.DescriptorType),
                        0},

    /* All done if there is no flag byte present in the descriptor */

    {ACPI_RSC_EXIT_NE,  ACPI_RSC_COMPARE_AML_LENGTH, 0, 1},

    /* Flag byte is present, get the flags */

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.StartDpf.CompatibilityPriority),
                        AML_OFFSET (StartDpf.Flags),
                        0},

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.StartDpf.PerformanceRobustness),
                        AML_OFFSET (StartDpf.Flags),
                        2}
};


/*******************************************************************************
 *
 * AcpiRsSetStartDpf
 *
 ******************************************************************************/

ACPI_RSCONVERT_INFO   AcpiRsSetStartDpf[10] =
{
    /* Start with a default descriptor of length 1 */

    {ACPI_RSC_INITSET,  ACPI_RESOURCE_NAME_START_DEPENDENT,
                        sizeof (AML_RESOURCE_START_DEPENDENT),
                        ACPI_RSC_TABLE_SIZE (AcpiRsSetStartDpf)},

    /* Set the default flag values */

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.StartDpf.CompatibilityPriority),
                        AML_OFFSET (StartDpf.Flags),
                        0},

    {ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET (Data.StartDpf.PerformanceRobustness),
                        AML_OFFSET (StartDpf.Flags),
                        2},
    /*
     * All done if the output descriptor length is required to be 1
     * (i.e., optimization to 0 bytes cannot be attempted)
     */
    {ACPI_RSC_EXIT_EQ,  ACPI_RSC_COMPARE_VALUE,
                        ACPI_RS_OFFSET(Data.StartDpf.DescriptorLength),
                        1},

    /* Set length to 0 bytes (no flags byte) */

    {ACPI_RSC_LENGTH,   0, 0, sizeof (AML_RESOURCE_START_DEPENDENT_NOPRIO)},

    /*
     * All done if the output descriptor length is required to be 0.
     *
     * TBD: Perhaps we should check for error if input flags are not
     * compatible with a 0-byte descriptor.
     */
    {ACPI_RSC_EXIT_EQ,  ACPI_RSC_COMPARE_VALUE,
                        ACPI_RS_OFFSET(Data.StartDpf.DescriptorLength),
                        0},

    /* Reset length to 1 byte (descriptor with flags byte) */

    {ACPI_RSC_LENGTH,   0, 0, sizeof (AML_RESOURCE_START_DEPENDENT)},


    /*
     * All done if flags byte is necessary -- if either priority value
     * is not ACPI_ACCEPTABLE_CONFIGURATION
     */
    {ACPI_RSC_EXIT_NE,  ACPI_RSC_COMPARE_VALUE,
                        ACPI_RS_OFFSET (Data.StartDpf.CompatibilityPriority),
                        ACPI_ACCEPTABLE_CONFIGURATION},

    {ACPI_RSC_EXIT_NE,  ACPI_RSC_COMPARE_VALUE,
                        ACPI_RS_OFFSET (Data.StartDpf.PerformanceRobustness),
                        ACPI_ACCEPTABLE_CONFIGURATION},

    /* Flag byte is not necessary */

    {ACPI_RSC_LENGTH,   0, 0, sizeof (AML_RESOURCE_START_DEPENDENT_NOPRIO)}
};
