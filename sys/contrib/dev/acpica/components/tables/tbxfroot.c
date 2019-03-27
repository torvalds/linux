/******************************************************************************
 *
 * Module Name: tbxfroot - Find the root ACPI table (RSDT)
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
#include <contrib/dev/acpica/include/actables.h>


#define _COMPONENT          ACPI_TABLES
        ACPI_MODULE_NAME    ("tbxfroot")


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbGetRsdpLength
 *
 * PARAMETERS:  Rsdp                - Pointer to RSDP
 *
 * RETURN:      Table length
 *
 * DESCRIPTION: Get the length of the RSDP
 *
 ******************************************************************************/

UINT32
AcpiTbGetRsdpLength (
    ACPI_TABLE_RSDP         *Rsdp)
{

    if (!ACPI_VALIDATE_RSDP_SIG (Rsdp->Signature))
    {
        /* BAD Signature */

        return (0);
    }

    /* "Length" field is available if table version >= 2 */

    if (Rsdp->Revision >= 2)
    {
        return (Rsdp->Length);
    }
    else
    {
        return (ACPI_RSDP_CHECKSUM_LENGTH);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbValidateRsdp
 *
 * PARAMETERS:  Rsdp                - Pointer to unvalidated RSDP
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Validate the RSDP (ptr)
 *
 ******************************************************************************/

ACPI_STATUS
AcpiTbValidateRsdp (
    ACPI_TABLE_RSDP         *Rsdp)
{

    /*
     * The signature and checksum must both be correct
     *
     * Note: Sometimes there exists more than one RSDP in memory; the valid
     * RSDP has a valid checksum, all others have an invalid checksum.
     */
    if (!ACPI_VALIDATE_RSDP_SIG (Rsdp->Signature))
    {
        /* Nope, BAD Signature */

        return (AE_BAD_SIGNATURE);
    }

    /* Check the standard checksum */

    if (AcpiTbChecksum ((UINT8 *) Rsdp, ACPI_RSDP_CHECKSUM_LENGTH) != 0)
    {
        return (AE_BAD_CHECKSUM);
    }

    /* Check extended checksum if table version >= 2 */

    if ((Rsdp->Revision >= 2) &&
        (AcpiTbChecksum ((UINT8 *) Rsdp, ACPI_RSDP_XCHECKSUM_LENGTH) != 0))
    {
        return (AE_BAD_CHECKSUM);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiFindRootPointer
 *
 * PARAMETERS:  TableAddress            - Where the table pointer is returned
 *
 * RETURN:      Status, RSDP physical address
 *
 * DESCRIPTION: Search lower 1Mbyte of memory for the root system descriptor
 *              pointer structure. If it is found, set *RSDP to point to it.
 *
 * NOTE1:       The RSDP must be either in the first 1K of the Extended
 *              BIOS Data Area or between E0000 and FFFFF (From ACPI Spec.)
 *              Only a 32-bit physical address is necessary.
 *
 * NOTE2:       This function is always available, regardless of the
 *              initialization state of the rest of ACPI.
 *
 ******************************************************************************/

ACPI_STATUS ACPI_INIT_FUNCTION
AcpiFindRootPointer (
    ACPI_PHYSICAL_ADDRESS   *TableAddress)
{
    UINT8                   *TablePtr;
    UINT8                   *MemRover;
    UINT32                  PhysicalAddress;


    ACPI_FUNCTION_TRACE (AcpiFindRootPointer);


    /* 1a) Get the location of the Extended BIOS Data Area (EBDA) */

    TablePtr = AcpiOsMapMemory (
        (ACPI_PHYSICAL_ADDRESS) ACPI_EBDA_PTR_LOCATION,
        ACPI_EBDA_PTR_LENGTH);
    if (!TablePtr)
    {
        ACPI_ERROR ((AE_INFO,
            "Could not map memory at 0x%8.8X for length %u",
            ACPI_EBDA_PTR_LOCATION, ACPI_EBDA_PTR_LENGTH));

        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    ACPI_MOVE_16_TO_32 (&PhysicalAddress, TablePtr);

    /* Convert segment part to physical address */

    PhysicalAddress <<= 4;
    AcpiOsUnmapMemory (TablePtr, ACPI_EBDA_PTR_LENGTH);

    /* EBDA present? */

    if (PhysicalAddress > 0x400)
    {
        /*
         * 1b) Search EBDA paragraphs (EBDA is required to be a
         *     minimum of 1K length)
         */
        TablePtr = AcpiOsMapMemory (
            (ACPI_PHYSICAL_ADDRESS) PhysicalAddress,
            ACPI_EBDA_WINDOW_SIZE);
        if (!TablePtr)
        {
            ACPI_ERROR ((AE_INFO,
                "Could not map memory at 0x%8.8X for length %u",
                PhysicalAddress, ACPI_EBDA_WINDOW_SIZE));

            return_ACPI_STATUS (AE_NO_MEMORY);
        }

        MemRover = AcpiTbScanMemoryForRsdp (
            TablePtr, ACPI_EBDA_WINDOW_SIZE);
        AcpiOsUnmapMemory (TablePtr, ACPI_EBDA_WINDOW_SIZE);

        if (MemRover)
        {
            /* Return the physical address */

            PhysicalAddress +=
                (UINT32) ACPI_PTR_DIFF (MemRover, TablePtr);

            *TableAddress = (ACPI_PHYSICAL_ADDRESS) PhysicalAddress;
            return_ACPI_STATUS (AE_OK);
        }
    }

    /*
     * 2) Search upper memory: 16-byte boundaries in E0000h-FFFFFh
     */
    TablePtr = AcpiOsMapMemory (
        (ACPI_PHYSICAL_ADDRESS) ACPI_HI_RSDP_WINDOW_BASE,
        ACPI_HI_RSDP_WINDOW_SIZE);

    if (!TablePtr)
    {
        ACPI_ERROR ((AE_INFO,
            "Could not map memory at 0x%8.8X for length %u",
            ACPI_HI_RSDP_WINDOW_BASE, ACPI_HI_RSDP_WINDOW_SIZE));

        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    MemRover = AcpiTbScanMemoryForRsdp (
        TablePtr, ACPI_HI_RSDP_WINDOW_SIZE);
    AcpiOsUnmapMemory (TablePtr, ACPI_HI_RSDP_WINDOW_SIZE);

    if (MemRover)
    {
        /* Return the physical address */

        PhysicalAddress = (UINT32)
            (ACPI_HI_RSDP_WINDOW_BASE + ACPI_PTR_DIFF (MemRover, TablePtr));

        *TableAddress = (ACPI_PHYSICAL_ADDRESS) PhysicalAddress;
        return_ACPI_STATUS (AE_OK);
    }

    /* A valid RSDP was not found */

    ACPI_BIOS_ERROR ((AE_INFO, "A valid RSDP was not found"));
    return_ACPI_STATUS (AE_NOT_FOUND);
}

ACPI_EXPORT_SYMBOL_INIT (AcpiFindRootPointer)


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbScanMemoryForRsdp
 *
 * PARAMETERS:  StartAddress        - Starting pointer for search
 *              Length              - Maximum length to search
 *
 * RETURN:      Pointer to the RSDP if found, otherwise NULL.
 *
 * DESCRIPTION: Search a block of memory for the RSDP signature
 *
 ******************************************************************************/

UINT8 *
AcpiTbScanMemoryForRsdp (
    UINT8                   *StartAddress,
    UINT32                  Length)
{
    ACPI_STATUS             Status;
    UINT8                   *MemRover;
    UINT8                   *EndAddress;


    ACPI_FUNCTION_TRACE (TbScanMemoryForRsdp);


    EndAddress = StartAddress + Length;

    /* Search from given start address for the requested length */

    for (MemRover = StartAddress; MemRover < EndAddress;
         MemRover += ACPI_RSDP_SCAN_STEP)
    {
        /* The RSDP signature and checksum must both be correct */

        Status = AcpiTbValidateRsdp (
            ACPI_CAST_PTR (ACPI_TABLE_RSDP, MemRover));
        if (ACPI_SUCCESS (Status))
        {
            /* Sig and checksum valid, we have found a real RSDP */

            ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
                "RSDP located at physical address %p\n", MemRover));
            return_PTR (MemRover);
        }

        /* No sig match or bad checksum, keep searching */
    }

    /* Searched entire block, no RSDP was found */

    ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
        "Searched entire block from %p, valid RSDP was not found\n",
        StartAddress));
    return_PTR (NULL);
}
