/******************************************************************************
 *
 * Module Name: utaddress - OpRegion address range check
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
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utaddress")


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtAddAddressRange
 *
 * PARAMETERS:  SpaceId             - Address space ID
 *              Address             - OpRegion start address
 *              Length              - OpRegion length
 *              RegionNode          - OpRegion namespace node
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add the Operation Region address range to the global list.
 *              The only supported Space IDs are Memory and I/O. Called when
 *              the OpRegion address/length operands are fully evaluated.
 *
 * MUTEX:       Locks the namespace
 *
 * NOTE: Because this interface is only called when an OpRegion argument
 * list is evaluated, there cannot be any duplicate RegionNodes.
 * Duplicate Address/Length values are allowed, however, so that multiple
 * address conflicts can be detected.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtAddAddressRange (
    ACPI_ADR_SPACE_TYPE     SpaceId,
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT32                  Length,
    ACPI_NAMESPACE_NODE     *RegionNode)
{
    ACPI_ADDRESS_RANGE      *RangeInfo;


    ACPI_FUNCTION_TRACE (UtAddAddressRange);


    if ((SpaceId != ACPI_ADR_SPACE_SYSTEM_MEMORY) &&
        (SpaceId != ACPI_ADR_SPACE_SYSTEM_IO))
    {
        return_ACPI_STATUS (AE_OK);
    }

    /* Allocate/init a new info block, add it to the appropriate list */

    RangeInfo = ACPI_ALLOCATE (sizeof (ACPI_ADDRESS_RANGE));
    if (!RangeInfo)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    RangeInfo->StartAddress = Address;
    RangeInfo->EndAddress = (Address + Length - 1);
    RangeInfo->RegionNode = RegionNode;

    RangeInfo->Next = AcpiGbl_AddressRangeList[SpaceId];
    AcpiGbl_AddressRangeList[SpaceId] = RangeInfo;

    ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
        "\nAdded [%4.4s] address range: 0x%8.8X%8.8X-0x%8.8X%8.8X\n",
        AcpiUtGetNodeName (RangeInfo->RegionNode),
        ACPI_FORMAT_UINT64 (Address),
        ACPI_FORMAT_UINT64 (RangeInfo->EndAddress)));

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtRemoveAddressRange
 *
 * PARAMETERS:  SpaceId             - Address space ID
 *              RegionNode          - OpRegion namespace node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Remove the Operation Region from the global list. The only
 *              supported Space IDs are Memory and I/O. Called when an
 *              OpRegion is deleted.
 *
 * MUTEX:       Assumes the namespace is locked
 *
 ******************************************************************************/

void
AcpiUtRemoveAddressRange (
    ACPI_ADR_SPACE_TYPE     SpaceId,
    ACPI_NAMESPACE_NODE     *RegionNode)
{
    ACPI_ADDRESS_RANGE      *RangeInfo;
    ACPI_ADDRESS_RANGE      *Prev;


    ACPI_FUNCTION_TRACE (UtRemoveAddressRange);


    if ((SpaceId != ACPI_ADR_SPACE_SYSTEM_MEMORY) &&
        (SpaceId != ACPI_ADR_SPACE_SYSTEM_IO))
    {
        return_VOID;
    }

    /* Get the appropriate list head and check the list */

    RangeInfo = Prev = AcpiGbl_AddressRangeList[SpaceId];
    while (RangeInfo)
    {
        if (RangeInfo->RegionNode == RegionNode)
        {
            if (RangeInfo == Prev) /* Found at list head */
            {
                AcpiGbl_AddressRangeList[SpaceId] = RangeInfo->Next;
            }
            else
            {
                Prev->Next = RangeInfo->Next;
            }

            ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
                "\nRemoved [%4.4s] address range: 0x%8.8X%8.8X-0x%8.8X%8.8X\n",
                AcpiUtGetNodeName (RangeInfo->RegionNode),
                ACPI_FORMAT_UINT64 (RangeInfo->StartAddress),
                ACPI_FORMAT_UINT64 (RangeInfo->EndAddress)));

            ACPI_FREE (RangeInfo);
            return_VOID;
        }

        Prev = RangeInfo;
        RangeInfo = RangeInfo->Next;
    }

    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtCheckAddressRange
 *
 * PARAMETERS:  SpaceId             - Address space ID
 *              Address             - Start address
 *              Length              - Length of address range
 *              Warn                - TRUE if warning on overlap desired
 *
 * RETURN:      Count of the number of conflicts detected. Zero is always
 *              returned for Space IDs other than Memory or I/O.
 *
 * DESCRIPTION: Check if the input address range overlaps any of the
 *              ASL operation region address ranges. The only supported
 *              Space IDs are Memory and I/O.
 *
 * MUTEX:       Assumes the namespace is locked.
 *
 ******************************************************************************/

UINT32
AcpiUtCheckAddressRange (
    ACPI_ADR_SPACE_TYPE     SpaceId,
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT32                  Length,
    BOOLEAN                 Warn)
{
    ACPI_ADDRESS_RANGE      *RangeInfo;
    ACPI_PHYSICAL_ADDRESS   EndAddress;
    char                    *Pathname;
    UINT32                  OverlapCount = 0;


    ACPI_FUNCTION_TRACE (UtCheckAddressRange);


    if ((SpaceId != ACPI_ADR_SPACE_SYSTEM_MEMORY) &&
        (SpaceId != ACPI_ADR_SPACE_SYSTEM_IO))
    {
        return_UINT32 (0);
    }

    RangeInfo = AcpiGbl_AddressRangeList[SpaceId];
    EndAddress = Address + Length - 1;

    /* Check entire list for all possible conflicts */

    while (RangeInfo)
    {
        /*
         * Check if the requested address/length overlaps this
         * address range. There are four cases to consider:
         *
         * 1) Input address/length is contained completely in the
         *    address range
         * 2) Input address/length overlaps range at the range start
         * 3) Input address/length overlaps range at the range end
         * 4) Input address/length completely encompasses the range
         */
        if ((Address <= RangeInfo->EndAddress) &&
            (EndAddress >= RangeInfo->StartAddress))
        {
            /* Found an address range overlap */

            OverlapCount++;
            if (Warn)   /* Optional warning message */
            {
                Pathname = AcpiNsGetNormalizedPathname (RangeInfo->RegionNode, TRUE);

                ACPI_WARNING ((AE_INFO,
                    "%s range 0x%8.8X%8.8X-0x%8.8X%8.8X conflicts with OpRegion 0x%8.8X%8.8X-0x%8.8X%8.8X (%s)",
                    AcpiUtGetRegionName (SpaceId),
                    ACPI_FORMAT_UINT64 (Address),
                    ACPI_FORMAT_UINT64 (EndAddress),
                    ACPI_FORMAT_UINT64 (RangeInfo->StartAddress),
                    ACPI_FORMAT_UINT64 (RangeInfo->EndAddress),
                    Pathname));
                ACPI_FREE (Pathname);
            }
        }

        RangeInfo = RangeInfo->Next;
    }

    return_UINT32 (OverlapCount);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDeleteAddressLists
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete all global address range lists (called during
 *              subsystem shutdown).
 *
 ******************************************************************************/

void
AcpiUtDeleteAddressLists (
    void)
{
    ACPI_ADDRESS_RANGE      *Next;
    ACPI_ADDRESS_RANGE      *RangeInfo;
    int                     i;


    /* Delete all elements in all address range lists */

    for (i = 0; i < ACPI_ADDRESS_RANGE_MAX; i++)
    {
        Next = AcpiGbl_AddressRangeList[i];

        while (Next)
        {
            RangeInfo = Next;
            Next = RangeInfo->Next;
            ACPI_FREE (RangeInfo);
        }

        AcpiGbl_AddressRangeList[i] = NULL;
    }
}
