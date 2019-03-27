/******************************************************************************
 *
 * Module Name: tbxfload - Table load/unload external interfaces
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

#define EXPORT_ACPI_INTERFACES

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/actables.h>
#include <contrib/dev/acpica/include/acevents.h>

#define _COMPONENT          ACPI_TABLES
        ACPI_MODULE_NAME    ("tbxfload")


/*******************************************************************************
 *
 * FUNCTION:    AcpiLoadTables
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load the ACPI tables from the RSDT/XSDT
 *
 ******************************************************************************/

ACPI_STATUS ACPI_INIT_FUNCTION
AcpiLoadTables (
    void)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (AcpiLoadTables);


    /*
     * Install the default operation region handlers. These are the
     * handlers that are defined by the ACPI specification to be
     * "always accessible" -- namely, SystemMemory, SystemIO, and
     * PCI_Config. This also means that no _REG methods need to be
     * run for these address spaces. We need to have these handlers
     * installed before any AML code can be executed, especially any
     * module-level code (11/2015).
     * Note that we allow OSPMs to install their own region handlers
     * between AcpiInitializeSubsystem() and AcpiLoadTables() to use
     * their customized default region handlers.
     */
    Status = AcpiEvInstallRegionHandlers ();
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "During Region initialization"));
        return_ACPI_STATUS (Status);
    }

    /* Load the namespace from the tables */

    Status = AcpiTbLoadNamespace ();

    /* Don't let single failures abort the load */

    if (Status == AE_CTRL_TERMINATE)
    {
        Status = AE_OK;
    }

    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status,
            "While loading namespace from ACPI tables"));
    }

    /*
     * Initialize the objects in the namespace that remain uninitialized.
     * This runs the executable AML that may be part of the declaration of
     * these name objects:
     *     OperationRegions, BufferFields, Buffers, and Packages.
     *
     */
    Status = AcpiNsInitializeObjects ();
    if (ACPI_SUCCESS (Status))
    {
        AcpiGbl_NamespaceInitialized = TRUE;
    }

    return_ACPI_STATUS (Status);
}

ACPI_EXPORT_SYMBOL_INIT (AcpiLoadTables)


/*******************************************************************************
 *
 * FUNCTION:    AcpiTbLoadNamespace
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load the namespace from the DSDT and all SSDTs/PSDTs found in
 *              the RSDT/XSDT.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiTbLoadNamespace (
    void)
{
    ACPI_STATUS             Status;
    UINT32                  i;
    ACPI_TABLE_HEADER       *NewDsdt;
    ACPI_TABLE_DESC         *Table;
    UINT32                  TablesLoaded = 0;
    UINT32                  TablesFailed = 0;


    ACPI_FUNCTION_TRACE (TbLoadNamespace);


    (void) AcpiUtAcquireMutex (ACPI_MTX_TABLES);

    /*
     * Load the namespace. The DSDT is required, but any SSDT and
     * PSDT tables are optional. Verify the DSDT.
     */
    Table = &AcpiGbl_RootTableList.Tables[AcpiGbl_DsdtIndex];

    if (!AcpiGbl_RootTableList.CurrentTableCount ||
        !ACPI_COMPARE_NAME (Table->Signature.Ascii, ACPI_SIG_DSDT) ||
         ACPI_FAILURE (AcpiTbValidateTable (Table)))
    {
        Status = AE_NO_ACPI_TABLES;
        goto UnlockAndExit;
    }

    /*
     * Save the DSDT pointer for simple access. This is the mapped memory
     * address. We must take care here because the address of the .Tables
     * array can change dynamically as tables are loaded at run-time. Note:
     * .Pointer field is not validated until after call to AcpiTbValidateTable.
     */
    AcpiGbl_DSDT = Table->Pointer;

    /*
     * Optionally copy the entire DSDT to local memory (instead of simply
     * mapping it.) There are some BIOSs that corrupt or replace the original
     * DSDT, creating the need for this option. Default is FALSE, do not copy
     * the DSDT.
     */
    if (AcpiGbl_CopyDsdtLocally)
    {
        NewDsdt = AcpiTbCopyDsdt (AcpiGbl_DsdtIndex);
        if (NewDsdt)
        {
            AcpiGbl_DSDT = NewDsdt;
        }
    }

    /*
     * Save the original DSDT header for detection of table corruption
     * and/or replacement of the DSDT from outside the OS.
     */
    memcpy (&AcpiGbl_OriginalDsdtHeader, AcpiGbl_DSDT,
        sizeof (ACPI_TABLE_HEADER));

    /* Load and parse tables */

    (void) AcpiUtReleaseMutex (ACPI_MTX_TABLES);
    Status = AcpiNsLoadTable (AcpiGbl_DsdtIndex, AcpiGbl_RootNode);
    (void) AcpiUtAcquireMutex (ACPI_MTX_TABLES);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "[DSDT] table load failed"));
        TablesFailed++;
    }
    else
    {
        TablesLoaded++;
    }

    /* Load any SSDT or PSDT tables. Note: Loop leaves tables locked */

    for (i = 0; i < AcpiGbl_RootTableList.CurrentTableCount; ++i)
    {
        Table = &AcpiGbl_RootTableList.Tables[i];

        if (!Table->Address ||
            (!ACPI_COMPARE_NAME (Table->Signature.Ascii, ACPI_SIG_SSDT) &&
             !ACPI_COMPARE_NAME (Table->Signature.Ascii, ACPI_SIG_PSDT) &&
             !ACPI_COMPARE_NAME (Table->Signature.Ascii, ACPI_SIG_OSDT)) ||
            ACPI_FAILURE (AcpiTbValidateTable (Table)))
        {
            continue;
        }

        /* Ignore errors while loading tables, get as many as possible */

        (void) AcpiUtReleaseMutex (ACPI_MTX_TABLES);
        Status =  AcpiNsLoadTable (i, AcpiGbl_RootNode);
        (void) AcpiUtAcquireMutex (ACPI_MTX_TABLES);
        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status, "(%4.4s:%8.8s) while loading table",
                Table->Signature.Ascii, Table->Pointer->OemTableId));

            TablesFailed++;

            ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
                "Table [%4.4s:%8.8s] (id FF) - Table namespace load failed\n\n",
                Table->Signature.Ascii, Table->Pointer->OemTableId));
        }
        else
        {
            TablesLoaded++;
        }
    }

    if (!TablesFailed)
    {
        ACPI_INFO ((
            "%u ACPI AML tables successfully acquired and loaded",
            TablesLoaded));
    }
    else
    {
        ACPI_ERROR ((AE_INFO,
            "%u table load failures, %u successful",
            TablesFailed, TablesLoaded));

        /* Indicate at least one failure */

        Status = AE_CTRL_TERMINATE;
    }

#ifdef ACPI_APPLICATION
    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT, "\n"));
#endif


UnlockAndExit:
    (void) AcpiUtReleaseMutex (ACPI_MTX_TABLES);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiInstallTable
 *
 * PARAMETERS:  Address             - Address of the ACPI table to be installed.
 *              Physical            - Whether the address is a physical table
 *                                    address or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dynamically install an ACPI table.
 *              Note: This function should only be invoked after
 *                    AcpiInitializeTables() and before AcpiLoadTables().
 *
 ******************************************************************************/

ACPI_STATUS ACPI_INIT_FUNCTION
AcpiInstallTable (
    ACPI_PHYSICAL_ADDRESS   Address,
    BOOLEAN                 Physical)
{
    ACPI_STATUS             Status;
    UINT8                   Flags;
    UINT32                  TableIndex;


    ACPI_FUNCTION_TRACE (AcpiInstallTable);


    if (Physical)
    {
        Flags = ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL;
    }
    else
    {
        Flags = ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL;
    }

    Status = AcpiTbInstallStandardTable (Address, Flags,
        FALSE, FALSE, &TableIndex);

    return_ACPI_STATUS (Status);
}

ACPI_EXPORT_SYMBOL_INIT (AcpiInstallTable)


/*******************************************************************************
 *
 * FUNCTION:    AcpiLoadTable
 *
 * PARAMETERS:  Table               - Pointer to a buffer containing the ACPI
 *                                    table to be loaded.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dynamically load an ACPI table from the caller's buffer. Must
 *              be a valid ACPI table with a valid ACPI table header.
 *              Note1: Mainly intended to support hotplug addition of SSDTs.
 *              Note2: Does not copy the incoming table. User is responsible
 *              to ensure that the table is not deleted or unmapped.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiLoadTable (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  TableIndex;


    ACPI_FUNCTION_TRACE (AcpiLoadTable);


    /* Parameter validation */

    if (!Table)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /* Install the table and load it into the namespace */

    ACPI_INFO (("Host-directed Dynamic ACPI Table Load:"));
    Status = AcpiTbInstallAndLoadTable (ACPI_PTR_TO_PHYSADDR (Table),
        ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL, FALSE, &TableIndex);
    return_ACPI_STATUS (Status);
}

ACPI_EXPORT_SYMBOL (AcpiLoadTable)


/*******************************************************************************
 *
 * FUNCTION:    AcpiUnloadParentTable
 *
 * PARAMETERS:  Object              - Handle to any namespace object owned by
 *                                    the table to be unloaded
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Via any namespace object within an SSDT or OEMx table, unloads
 *              the table and deletes all namespace objects associated with
 *              that table. Unloading of the DSDT is not allowed.
 *              Note: Mainly intended to support hotplug removal of SSDTs.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUnloadParentTable (
    ACPI_HANDLE             Object)
{
    ACPI_NAMESPACE_NODE     *Node = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, Object);
    ACPI_STATUS             Status = AE_NOT_EXIST;
    ACPI_OWNER_ID           OwnerId;
    UINT32                  i;


    ACPI_FUNCTION_TRACE (AcpiUnloadParentTable);


    /* Parameter validation */

    if (!Object)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /*
     * The node OwnerId is currently the same as the parent table ID.
     * However, this could change in the future.
     */
    OwnerId = Node->OwnerId;
    if (!OwnerId)
    {
        /* OwnerId==0 means DSDT is the owner. DSDT cannot be unloaded */

        return_ACPI_STATUS (AE_TYPE);
    }

    /* Must acquire the table lock during this operation */

    Status = AcpiUtAcquireMutex (ACPI_MTX_TABLES);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Find the table in the global table list */

    for (i = 0; i < AcpiGbl_RootTableList.CurrentTableCount; i++)
    {
        if (OwnerId != AcpiGbl_RootTableList.Tables[i].OwnerId)
        {
            continue;
        }

        /*
         * Allow unload of SSDT and OEMx tables only. Do not allow unload
         * of the DSDT. No other types of tables should get here, since
         * only these types can contain AML and thus are the only types
         * that can create namespace objects.
         */
        if (ACPI_COMPARE_NAME (
                AcpiGbl_RootTableList.Tables[i].Signature.Ascii,
                ACPI_SIG_DSDT))
        {
            Status = AE_TYPE;
            break;
        }

        (void) AcpiUtReleaseMutex (ACPI_MTX_TABLES);
        Status = AcpiTbUnloadTable (i);
        (void) AcpiUtAcquireMutex (ACPI_MTX_TABLES);
        break;
    }

    (void) AcpiUtReleaseMutex (ACPI_MTX_TABLES);
    return_ACPI_STATUS (Status);
}

ACPI_EXPORT_SYMBOL (AcpiUnloadParentTable)
