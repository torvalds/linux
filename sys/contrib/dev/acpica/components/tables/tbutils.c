/******************************************************************************
 *
 * Module Name: tbutils - ACPI Table utilities
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
        ACPI_MODULE_NAME    ("tbutils")


/* Local prototypes */

static ACPI_PHYSICAL_ADDRESS
AcpiTbGetRootTableEntry (
    UINT8                   *TableEntry,
    UINT32                  TableEntrySize);


#if (!ACPI_REDUCED_HARDWARE)
/*******************************************************************************
 *
 * FUNCTION:    AcpiTbInitializeFacs
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a permanent mapping for the FADT and save it in a global
 *              for accessing the Global Lock and Firmware Waking Vector
 *
 ******************************************************************************/

ACPI_STATUS
AcpiTbInitializeFacs (
    void)
{
    ACPI_TABLE_FACS         *Facs;


    /* If Hardware Reduced flag is set, there is no FACS */

    if (AcpiGbl_ReducedHardware)
    {
        AcpiGbl_FACS = NULL;
        return (AE_OK);
    }
    else if (AcpiGbl_FADT.XFacs &&
         (!AcpiGbl_FADT.Facs || !AcpiGbl_Use32BitFacsAddresses))
    {
        (void) AcpiGetTableByIndex (AcpiGbl_XFacsIndex,
            ACPI_CAST_INDIRECT_PTR (ACPI_TABLE_HEADER, &Facs));
        AcpiGbl_FACS = Facs;
    }
    else if (AcpiGbl_FADT.Facs)
    {
        (void) AcpiGetTableByIndex (AcpiGbl_FacsIndex,
            ACPI_CAST_INDIRECT_PTR (ACPI_TABLE_HEADER, &Facs));
        AcpiGbl_FACS = Facs;
    }

    /* If there is no FACS, just continue. There was already an error msg */

    return (AE_OK);
}
#endif /* !ACPI_REDUCED_HARDWARE */


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbCheckDsdtHeader
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Quick compare to check validity of the DSDT. This will detect
 *              if the DSDT has been replaced from outside the OS and/or if
 *              the DSDT header has been corrupted.
 *
 ******************************************************************************/

void
AcpiTbCheckDsdtHeader (
    void)
{

    /* Compare original length and checksum to current values */

    if (AcpiGbl_OriginalDsdtHeader.Length != AcpiGbl_DSDT->Length ||
        AcpiGbl_OriginalDsdtHeader.Checksum != AcpiGbl_DSDT->Checksum)
    {
        ACPI_BIOS_ERROR ((AE_INFO,
            "The DSDT has been corrupted or replaced - "
            "old, new headers below"));

        AcpiTbPrintTableHeader (0, &AcpiGbl_OriginalDsdtHeader);
        AcpiTbPrintTableHeader (0, AcpiGbl_DSDT);

        /* Disable further error messages */

        AcpiGbl_OriginalDsdtHeader.Length = AcpiGbl_DSDT->Length;
        AcpiGbl_OriginalDsdtHeader.Checksum = AcpiGbl_DSDT->Checksum;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbCopyDsdt
 *
 * PARAMETERS:  TableIndex          - Index of installed table to copy
 *
 * RETURN:      The copied DSDT
 *
 * DESCRIPTION: Implements a subsystem option to copy the DSDT to local memory.
 *              Some very bad BIOSs are known to either corrupt the DSDT or
 *              install a new, bad DSDT. This copy works around the problem.
 *
 ******************************************************************************/

ACPI_TABLE_HEADER *
AcpiTbCopyDsdt (
    UINT32                  TableIndex)
{
    ACPI_TABLE_HEADER       *NewTable;
    ACPI_TABLE_DESC         *TableDesc;


    TableDesc = &AcpiGbl_RootTableList.Tables[TableIndex];

    NewTable = ACPI_ALLOCATE (TableDesc->Length);
    if (!NewTable)
    {
        ACPI_ERROR ((AE_INFO, "Could not copy DSDT of length 0x%X",
            TableDesc->Length));
        return (NULL);
    }

    memcpy (NewTable, TableDesc->Pointer, TableDesc->Length);
    AcpiTbUninstallTable (TableDesc);

    AcpiTbInitTableDescriptor (
        &AcpiGbl_RootTableList.Tables[AcpiGbl_DsdtIndex],
        ACPI_PTR_TO_PHYSADDR (NewTable),
        ACPI_TABLE_ORIGIN_INTERNAL_VIRTUAL, NewTable);

    ACPI_INFO ((
        "Forced DSDT copy: length 0x%05X copied locally, original unmapped",
        NewTable->Length));

    return (NewTable);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbGetRootTableEntry
 *
 * PARAMETERS:  TableEntry          - Pointer to the RSDT/XSDT table entry
 *              TableEntrySize      - sizeof 32 or 64 (RSDT or XSDT)
 *
 * RETURN:      Physical address extracted from the root table
 *
 * DESCRIPTION: Get one root table entry. Handles 32-bit and 64-bit cases on
 *              both 32-bit and 64-bit platforms
 *
 * NOTE:        ACPI_PHYSICAL_ADDRESS is 32-bit on 32-bit platforms, 64-bit on
 *              64-bit platforms.
 *
 ******************************************************************************/

static ACPI_PHYSICAL_ADDRESS
AcpiTbGetRootTableEntry (
    UINT8                   *TableEntry,
    UINT32                  TableEntrySize)
{
    UINT64                  Address64;


    /*
     * Get the table physical address (32-bit for RSDT, 64-bit for XSDT):
     * Note: Addresses are 32-bit aligned (not 64) in both RSDT and XSDT
     */
    if (TableEntrySize == ACPI_RSDT_ENTRY_SIZE)
    {
        /*
         * 32-bit platform, RSDT: Return 32-bit table entry
         * 64-bit platform, RSDT: Expand 32-bit to 64-bit and return
         */
        return ((ACPI_PHYSICAL_ADDRESS) (*ACPI_CAST_PTR (
            UINT32, TableEntry)));
    }
    else
    {
        /*
         * 32-bit platform, XSDT: Truncate 64-bit to 32-bit and return
         * 64-bit platform, XSDT: Move (unaligned) 64-bit to local,
         *  return 64-bit
         */
        ACPI_MOVE_64_TO_64 (&Address64, TableEntry);

#if ACPI_MACHINE_WIDTH == 32
        if (Address64 > ACPI_UINT32_MAX)
        {
            /* Will truncate 64-bit address to 32 bits, issue warning */

            ACPI_BIOS_WARNING ((AE_INFO,
                "64-bit Physical Address in XSDT is too large (0x%8.8X%8.8X),"
                " truncating",
                ACPI_FORMAT_UINT64 (Address64)));
        }
#endif
        return ((ACPI_PHYSICAL_ADDRESS) (Address64));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbParseRootTable
 *
 * PARAMETERS:  RsdpAddress         - Pointer to the RSDP
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to parse the Root System Description
 *              Table (RSDT or XSDT)
 *
 * NOTE:        Tables are mapped (not copied) for efficiency. The FACS must
 *              be mapped and cannot be copied because it contains the actual
 *              memory location of the ACPI Global Lock.
 *
 ******************************************************************************/

ACPI_STATUS ACPI_INIT_FUNCTION
AcpiTbParseRootTable (
    ACPI_PHYSICAL_ADDRESS   RsdpAddress)
{
    ACPI_TABLE_RSDP         *Rsdp;
    UINT32                  TableEntrySize;
    UINT32                  i;
    UINT32                  TableCount;
    ACPI_TABLE_HEADER       *Table;
    ACPI_PHYSICAL_ADDRESS   Address;
    UINT32                  Length;
    UINT8                   *TableEntry;
    ACPI_STATUS             Status;
    UINT32                  TableIndex;


    ACPI_FUNCTION_TRACE (TbParseRootTable);


    /* Map the entire RSDP and extract the address of the RSDT or XSDT */

    Rsdp = AcpiOsMapMemory (RsdpAddress, sizeof (ACPI_TABLE_RSDP));
    if (!Rsdp)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    AcpiTbPrintTableHeader (RsdpAddress,
        ACPI_CAST_PTR (ACPI_TABLE_HEADER, Rsdp));

    /* Use XSDT if present and not overridden. Otherwise, use RSDT */

    if ((Rsdp->Revision > 1) &&
        Rsdp->XsdtPhysicalAddress &&
        !AcpiGbl_DoNotUseXsdt)
    {
        /*
         * RSDP contains an XSDT (64-bit physical addresses). We must use
         * the XSDT if the revision is > 1 and the XSDT pointer is present,
         * as per the ACPI specification.
         */
        Address = (ACPI_PHYSICAL_ADDRESS) Rsdp->XsdtPhysicalAddress;
        TableEntrySize = ACPI_XSDT_ENTRY_SIZE;
    }
    else
    {
        /* Root table is an RSDT (32-bit physical addresses) */

        Address = (ACPI_PHYSICAL_ADDRESS) Rsdp->RsdtPhysicalAddress;
        TableEntrySize = ACPI_RSDT_ENTRY_SIZE;
    }

    /*
     * It is not possible to map more than one entry in some environments,
     * so unmap the RSDP here before mapping other tables
     */
    AcpiOsUnmapMemory (Rsdp, sizeof (ACPI_TABLE_RSDP));

    /* Map the RSDT/XSDT table header to get the full table length */

    Table = AcpiOsMapMemory (Address, sizeof (ACPI_TABLE_HEADER));
    if (!Table)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    AcpiTbPrintTableHeader (Address, Table);

    /*
     * Validate length of the table, and map entire table.
     * Minimum length table must contain at least one entry.
     */
    Length = Table->Length;
    AcpiOsUnmapMemory (Table, sizeof (ACPI_TABLE_HEADER));

    if (Length < (sizeof (ACPI_TABLE_HEADER) + TableEntrySize))
    {
        ACPI_BIOS_ERROR ((AE_INFO,
            "Invalid table length 0x%X in RSDT/XSDT", Length));
        return_ACPI_STATUS (AE_INVALID_TABLE_LENGTH);
    }

    Table = AcpiOsMapMemory (Address, Length);
    if (!Table)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Validate the root table checksum */

    Status = AcpiTbVerifyChecksum (Table, Length);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsUnmapMemory (Table, Length);
        return_ACPI_STATUS (Status);
    }

    /* Get the number of entries and pointer to first entry */

    TableCount = (UINT32) ((Table->Length - sizeof (ACPI_TABLE_HEADER)) /
        TableEntrySize);
    TableEntry = ACPI_ADD_PTR (UINT8, Table, sizeof (ACPI_TABLE_HEADER));

    /* Initialize the root table array from the RSDT/XSDT */

    for (i = 0; i < TableCount; i++)
    {
        /* Get the table physical address (32-bit for RSDT, 64-bit for XSDT) */

        Address = AcpiTbGetRootTableEntry (TableEntry, TableEntrySize);

        /* Skip NULL entries in RSDT/XSDT */

        if (!Address)
        {
            goto NextTable;
        }

        Status = AcpiTbInstallStandardTable (Address,
            ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL, FALSE, TRUE, &TableIndex);

        if (ACPI_SUCCESS (Status) &&
            ACPI_COMPARE_NAME (
                &AcpiGbl_RootTableList.Tables[TableIndex].Signature,
                ACPI_SIG_FADT))
        {
            AcpiGbl_FadtIndex = TableIndex;
            AcpiTbParseFadt ();
        }

NextTable:

        TableEntry += TableEntrySize;
    }

    AcpiOsUnmapMemory (Table, Length);
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbGetTable
 *
 * PARAMETERS:  TableDesc           - Table descriptor
 *              OutTable            - Where the pointer to the table is returned
 *
 * RETURN:      Status and pointer to the requested table
 *
 * DESCRIPTION: Increase a reference to a table descriptor and return the
 *              validated table pointer.
 *              If the table descriptor is an entry of the root table list,
 *              this API must be invoked with ACPI_MTX_TABLES acquired.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiTbGetTable (
    ACPI_TABLE_DESC        *TableDesc,
    ACPI_TABLE_HEADER      **OutTable)
{
    ACPI_STATUS            Status;


    ACPI_FUNCTION_TRACE (AcpiTbGetTable);


    if (TableDesc->ValidationCount == 0)
    {
        /* Table need to be "VALIDATED" */

        Status = AcpiTbValidateTable (TableDesc);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }

    if (TableDesc->ValidationCount < ACPI_MAX_TABLE_VALIDATIONS)
    {
        TableDesc->ValidationCount++;

        /*
         * Detect ValidationCount overflows to ensure that the warning
         * message will only be printed once.
         */
        if (TableDesc->ValidationCount >= ACPI_MAX_TABLE_VALIDATIONS)
        {
            ACPI_WARNING((AE_INFO,
                "Table %p, Validation count overflows\n", TableDesc));
        }
    }

    *OutTable = TableDesc->Pointer;
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbPutTable
 *
 * PARAMETERS:  TableDesc           - Table descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decrease a reference to a table descriptor and release the
 *              validated table pointer if no references.
 *              If the table descriptor is an entry of the root table list,
 *              this API must be invoked with ACPI_MTX_TABLES acquired.
 *
 ******************************************************************************/

void
AcpiTbPutTable (
    ACPI_TABLE_DESC        *TableDesc)
{

    ACPI_FUNCTION_TRACE (AcpiTbPutTable);


    if (TableDesc->ValidationCount < ACPI_MAX_TABLE_VALIDATIONS)
    {
        TableDesc->ValidationCount--;

        /*
         * Detect ValidationCount underflows to ensure that the warning
         * message will only be printed once.
         */
        if (TableDesc->ValidationCount >= ACPI_MAX_TABLE_VALIDATIONS)
        {
            ACPI_WARNING ((AE_INFO,
                "Table %p, Validation count underflows\n", TableDesc));
            return_VOID;
        }
    }

    if (TableDesc->ValidationCount == 0)
    {
        /* Table need to be "INVALIDATED" */

        AcpiTbInvalidateTable (TableDesc);
    }

    return_VOID;
}
