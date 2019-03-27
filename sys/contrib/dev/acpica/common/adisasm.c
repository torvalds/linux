/******************************************************************************
 *
 * Module Name: adisasm - Application-level disassembler routines
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

#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acdisasm.h>
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acapps.h>


#define _COMPONENT          ACPI_TOOLS
        ACPI_MODULE_NAME    ("adisasm")

/* Local prototypes */

static ACPI_STATUS
AdDoExternalFileList (
    char                    *Filename);

static ACPI_STATUS
AdDisassembleOneTable (
    ACPI_TABLE_HEADER       *Table,
    FILE                    *File,
    char                    *Filename,
    char                    *DisasmFilename);

static ACPI_STATUS
AdReparseOneTable (
    ACPI_TABLE_HEADER       *Table,
    FILE                    *File,
    ACPI_OWNER_ID           OwnerId);


ACPI_TABLE_DESC             LocalTables[1];
ACPI_PARSE_OBJECT           *AcpiGbl_ParseOpRoot;


/* Stubs for everything except ASL compiler */

#ifndef ACPI_ASL_COMPILER
BOOLEAN
AcpiDsIsResultUsed (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState)
{
    return (TRUE);
}

ACPI_STATUS
AcpiDsMethodError (
    ACPI_STATUS             Status,
    ACPI_WALK_STATE         *WalkState)
{
    return (Status);
}
#endif


/*******************************************************************************
 *
 * FUNCTION:    AdInitialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: ACPICA and local initialization
 *
 ******************************************************************************/

ACPI_STATUS
AdInitialize (
    void)
{
    ACPI_STATUS             Status;


    /* ACPICA subsystem initialization */

    Status = AcpiOsInitialize ();
    if (ACPI_FAILURE (Status))
    {
        fprintf (stderr, "Could not initialize ACPICA subsystem: %s\n",
            AcpiFormatException (Status));

        return (Status);
    }

    Status = AcpiUtInitGlobals ();
    if (ACPI_FAILURE (Status))
    {
        fprintf (stderr, "Could not initialize ACPICA globals: %s\n",
            AcpiFormatException (Status));

        return (Status);
    }

    Status = AcpiUtMutexInitialize ();
    if (ACPI_FAILURE (Status))
    {
        fprintf (stderr, "Could not initialize ACPICA mutex objects: %s\n",
            AcpiFormatException (Status));

        return (Status);
    }

    Status = AcpiNsRootInitialize ();
    if (ACPI_FAILURE (Status))
    {
        fprintf (stderr, "Could not initialize ACPICA namespace: %s\n",
            AcpiFormatException (Status));

        return (Status);
    }

    /* Setup the Table Manager (cheat - there is no RSDT) */

    AcpiGbl_RootTableList.MaxTableCount = 1;
    AcpiGbl_RootTableList.CurrentTableCount = 0;
    AcpiGbl_RootTableList.Tables = LocalTables;

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AdAmlDisassemble
 *
 * PARAMETERS:  Filename            - AML input filename
 *              OutToFile           - TRUE if output should go to a file
 *              Prefix              - Path prefix for output
 *              OutFilename         - where the filename is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disassembler entry point. Disassemble an entire ACPI table.
 *
 *****************************************************************************/

ACPI_STATUS
AdAmlDisassemble (
    BOOLEAN                 OutToFile,
    char                    *Filename,
    char                    *Prefix,
    char                    **OutFilename)
{
    ACPI_STATUS             Status;
    char                    *DisasmFilename = NULL;
    FILE                    *File = NULL;
    ACPI_TABLE_HEADER       *Table = NULL;
    ACPI_NEW_TABLE_DESC     *ListHead = NULL;


    /*
     * Input: AML code from either a file or via GetTables (memory or
     * registry)
     */
    if (Filename)
    {
        /* Get the list of all AML tables in the file */

        Status = AcGetAllTablesFromFile (Filename,
            ACPI_GET_ALL_TABLES, &ListHead);
        if (ACPI_FAILURE (Status))
        {
            AcpiOsPrintf ("Could not get ACPI tables from %s, %s\n",
                Filename, AcpiFormatException (Status));
            return (Status);
        }

        /* Process any user-specified files for external objects */

        Status = AdDoExternalFileList (Filename);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }
    else
    {
        Status = AdGetLocalTables ();
        if (ACPI_FAILURE (Status))
        {
            AcpiOsPrintf ("Could not get ACPI tables, %s\n",
                AcpiFormatException (Status));
            return (Status);
        }

        if (!AcpiGbl_DmOpt_Disasm)
        {
            return (AE_OK);
        }

        /* Obtained the local tables, just disassemble the DSDT */

        Status = AcpiGetTable (ACPI_SIG_DSDT, 0, &Table);
        if (ACPI_FAILURE (Status))
        {
            AcpiOsPrintf ("Could not get DSDT, %s\n",
                AcpiFormatException (Status));
            return (Status);
        }

        AcpiOsPrintf ("\nDisassembly of DSDT\n");
        Prefix = AdGenerateFilename ("dsdt", Table->OemTableId);
    }

    /*
     * Output: ASL code. Redirect to a file if requested
     */
    if (OutToFile)
    {
        /* Create/Open a disassembly output file */

        DisasmFilename = FlGenerateFilename (Prefix, FILE_SUFFIX_DISASSEMBLY);
        if (!DisasmFilename)
        {
            fprintf (stderr, "Could not generate output filename\n");
            Status = AE_ERROR;
            goto Cleanup;
        }

        File = fopen (DisasmFilename, "w+");
        if (!File)
        {
            fprintf (stderr, "Could not open output file %s\n",
                DisasmFilename);
            Status = AE_ERROR;
            goto Cleanup;
        }

        AcpiOsRedirectOutput (File);
    }

    *OutFilename = DisasmFilename;

    /* Disassemble all AML tables within the file */

    while (ListHead)
    {
        Status = AdDisassembleOneTable (ListHead->Table,
            File, Filename, DisasmFilename);
        if (ACPI_FAILURE (Status))
        {
            break;
        }

        ListHead = ListHead->Next;
    }

Cleanup:

    if (Table &&
        !AcpiGbl_ForceAmlDisassembly &&
        !AcpiUtIsAmlTable (Table))
    {
        ACPI_FREE (Table);
    }

    AcDeleteTableList (ListHead);

    if (File)
    {
        fclose (File);
        AcpiOsRedirectOutput (stdout);
    }

    AcpiPsDeleteParseTree (AcpiGbl_ParseOpRoot);
    AcpiGbl_ParseOpRoot = NULL;
    return (Status);
}


/******************************************************************************
 *
 * FUNCTION:    AdDisassembleOneTable
 *
 * PARAMETERS:  Table               - Raw AML table
 *              File                - Pointer for the input file
 *              Filename            - AML input filename
 *              DisasmFilename      - Output filename
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disassemble a single ACPI table. AML or data table.
 *
 *****************************************************************************/

static ACPI_STATUS
AdDisassembleOneTable (
    ACPI_TABLE_HEADER       *Table,
    FILE                    *File,
    char                    *Filename,
    char                    *DisasmFilename)
{
    ACPI_STATUS             Status;
    ACPI_OWNER_ID           OwnerId;


#ifdef ACPI_ASL_COMPILER

    /*
     * For ASL-/ASL+ converter: replace the temporary "XXXX"
     * table signature with the original. This "XXXX" makes
     * it harder for the AML interpreter to run the badaml
     * (.xxx) file produced from the converter in case if
     * it fails to get deleted.
     */
    if (AcpiGbl_CaptureComments)
    {
        strncpy (Table->Signature, AcpiGbl_TableSig, ACPI_NAME_SIZE);
    }
#endif

    /* ForceAmlDisassembly means to assume the table contains valid AML */

    if (!AcpiGbl_ForceAmlDisassembly && !AcpiUtIsAmlTable (Table))
    {
        AdDisassemblerHeader (Filename, ACPI_IS_DATA_TABLE);

        /* This is a "Data Table" (non-AML table) */

        AcpiOsPrintf (" * ACPI Data Table [%4.4s]\n *\n",
            Table->Signature);
        AcpiOsPrintf (" * Format: [HexOffset DecimalOffset ByteLength]  "
            "FieldName : FieldValue\n */\n\n");

        AcpiDmDumpDataTable (Table);
        fprintf (stderr, "Acpi Data Table [%4.4s] decoded\n",
            Table->Signature);

        if (File)
        {
            fprintf (stderr, "Formatted output:  %s - %u bytes\n",
                DisasmFilename, CmGetFileSize (File));
        }

        return (AE_OK);
    }

    /*
     * This is an AML table (DSDT or SSDT).
     * Always parse the tables, only option is what to display
     */
    Status = AdParseTable (Table, &OwnerId, TRUE, FALSE);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could not parse ACPI tables, %s\n",
            AcpiFormatException (Status));
        return (Status);
    }

    /* Debug output, namespace and parse tree */

    if (AslCompilerdebug && File)
    {
        AcpiOsPrintf ("/**** Before second load\n");

        NsSetupNamespaceListing (File);
        NsDisplayNamespace ();

        AcpiOsPrintf ("*****/\n");
    }

    /* Load namespace from names created within control methods */

    AcpiDmFinishNamespaceLoad (AcpiGbl_ParseOpRoot,
        AcpiGbl_RootNode, OwnerId);

    /*
     * Cross reference the namespace here, in order to
     * generate External() statements
     */
    AcpiDmCrossReferenceNamespace (AcpiGbl_ParseOpRoot,
        AcpiGbl_RootNode, OwnerId);

    if (AslCompilerdebug)
    {
        AcpiDmDumpTree (AcpiGbl_ParseOpRoot);
    }

    /* Find possible calls to external control methods */

    AcpiDmFindOrphanMethods (AcpiGbl_ParseOpRoot);

    /*
     * If we found any external control methods, we must reparse
     * the entire tree with the new information (namely, the
     * number of arguments per method)
     */
    if (AcpiDmGetUnresolvedExternalMethodCount ())
    {
        Status = AdReparseOneTable (Table, File, OwnerId);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }

    /*
     * Now that the namespace is finalized, we can perform namespace
     * transforms.
     *
     * 1) Convert fixed-offset references to resource descriptors
     *    to symbolic references (Note: modifies namespace)
     */
    AcpiDmConvertParseObjects (AcpiGbl_ParseOpRoot, AcpiGbl_RootNode);

    /* Optional displays */

    if (AcpiGbl_DmOpt_Disasm)
    {
        /* This is the real disassembly */

        AdDisplayTables (Filename, Table);

        /* Dump hex table if requested (-vt) */

        AcpiDmDumpDataTable (Table);

        fprintf (stderr, "Disassembly completed\n");
        if (File)
        {
            fprintf (stderr, "ASL Output:    %s - %u bytes\n",
                DisasmFilename, CmGetFileSize (File));
        }

        if (AslGbl_MapfileFlag)
        {
            fprintf (stderr, "%14s %s - %u bytes\n",
                AslGbl_Files[ASL_FILE_MAP_OUTPUT].ShortDescription,
                AslGbl_Files[ASL_FILE_MAP_OUTPUT].Filename,
                FlGetFileSize (ASL_FILE_MAP_OUTPUT));
        }
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AdReparseOneTable
 *
 * PARAMETERS:  Table               - Raw AML table
 *              File                - Pointer for the input file
 *              OwnerId             - ID for this table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Reparse a table that has already been loaded. Used to
 *              integrate information about external control methods.
 *              These methods may have been previously parsed incorrectly.
 *
 *****************************************************************************/

static ACPI_STATUS
AdReparseOneTable (
    ACPI_TABLE_HEADER       *Table,
    FILE                    *File,
    ACPI_OWNER_ID           OwnerId)
{
    ACPI_STATUS             Status;
    ACPI_COMMENT_ADDR_NODE  *AddrListHead;


    fprintf (stderr,
        "\nFound %u external control methods, "
        "reparsing with new information\n",
        AcpiDmGetUnresolvedExternalMethodCount ());

    /* Reparse, rebuild namespace */

    AcpiPsDeleteParseTree (AcpiGbl_ParseOpRoot);
    AcpiGbl_ParseOpRoot = NULL;
    AcpiNsDeleteNamespaceSubtree (AcpiGbl_RootNode);

    AcpiGbl_RootNode                    = NULL;
    AcpiGbl_RootNodeStruct.Name.Integer = ACPI_ROOT_NAME;
    AcpiGbl_RootNodeStruct.DescriptorType = ACPI_DESC_TYPE_NAMED;
    AcpiGbl_RootNodeStruct.Type         = ACPI_TYPE_DEVICE;
    AcpiGbl_RootNodeStruct.Parent       = NULL;
    AcpiGbl_RootNodeStruct.Child        = NULL;
    AcpiGbl_RootNodeStruct.Peer         = NULL;
    AcpiGbl_RootNodeStruct.Object       = NULL;
    AcpiGbl_RootNodeStruct.Flags        = 0;

    Status = AcpiNsRootInitialize ();
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* New namespace, add the external definitions first */

    AcpiDmAddExternalListToNamespace ();

    /* For -ca option: clear the list of comment addresses. */

    while (AcpiGbl_CommentAddrListHead)
    {
        AddrListHead= AcpiGbl_CommentAddrListHead;
        AcpiGbl_CommentAddrListHead = AcpiGbl_CommentAddrListHead->Next;
        AcpiOsFree(AddrListHead);
    }

    /* Parse the table again. No need to reload it, however */

    Status = AdParseTable (Table, NULL, FALSE, FALSE);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could not parse ACPI tables, %s\n",
            AcpiFormatException (Status));
        return (Status);
    }

    /* Cross reference the namespace again */

    AcpiDmFinishNamespaceLoad (AcpiGbl_ParseOpRoot,
        AcpiGbl_RootNode, OwnerId);

    AcpiDmCrossReferenceNamespace (AcpiGbl_ParseOpRoot,
        AcpiGbl_RootNode, OwnerId);

    /* Debug output - namespace and parse tree */

    if (AslCompilerdebug)
    {
        AcpiOsPrintf ("/**** After second load and resource conversion\n");
        if (File)
        {
            NsSetupNamespaceListing (File);
            NsDisplayNamespace ();
        }

        AcpiOsPrintf ("*****/\n");
        AcpiDmDumpTree (AcpiGbl_ParseOpRoot);
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    AdDoExternalFileList
 *
 * PARAMETERS:  Filename            - Input file for the table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Process all tables found in the -e external files list
 *
 *****************************************************************************/

static ACPI_STATUS
AdDoExternalFileList (
    char                    *Filename)
{
    ACPI_EXTERNAL_FILE      *ExternalFileList;
    char                    *ExternalFilename;
    ACPI_NEW_TABLE_DESC     *ExternalListHead = NULL;
    ACPI_STATUS             Status;
    ACPI_STATUS             GlobalStatus = AE_OK;
    ACPI_OWNER_ID           OwnerId;


    /*
     * External filenames are specified on the command line like this:
     * Example: iasl -e file1,file2,file3 -d xxx.aml
     */
    ExternalFileList = AcpiGbl_ExternalFileList;

    /* Process each external file */

    while (ExternalFileList)
    {
        ExternalFilename = ExternalFileList->Path;
        if (!strcmp (ExternalFilename, Filename))
        {
            /* Next external file */

            ExternalFileList = ExternalFileList->Next;
            continue;
        }

        AcpiOsPrintf ("External object resolution file %16s\n",
            ExternalFilename);

        Status = AcGetAllTablesFromFile (
            ExternalFilename, ACPI_GET_ONLY_AML_TABLES, &ExternalListHead);
        if (ACPI_FAILURE (Status))
        {
            if (Status == AE_TYPE)
            {
                ExternalFileList = ExternalFileList->Next;
                GlobalStatus = AE_TYPE;
                Status = AE_OK;
                continue;
            }

            AcDeleteTableList (ExternalListHead);
            return (Status);
        }

        /* Load external tables for symbol resolution */

        while (ExternalListHead)
        {
            Status = AdParseTable (
                ExternalListHead->Table, &OwnerId, TRUE, TRUE);
            if (ACPI_FAILURE (Status))
            {
                AcpiOsPrintf ("Could not parse external ACPI tables, %s\n",
                    AcpiFormatException (Status));
                AcDeleteTableList (ExternalListHead);
                return (Status);
            }

            /*
             * Load namespace from names created within control methods
             * Set owner id of nodes in external table
             */
            AcpiDmFinishNamespaceLoad (AcpiGbl_ParseOpRoot,
                AcpiGbl_RootNode, OwnerId);
            AcpiPsDeleteParseTree (AcpiGbl_ParseOpRoot);

            ExternalListHead = ExternalListHead->Next;
        }

        /* Next external file */

        ExternalFileList = ExternalFileList->Next;
    }

    AcDeleteTableList (ExternalListHead);

    if (ACPI_FAILURE (GlobalStatus))
    {
        return (GlobalStatus);
    }

    /* Clear external list generated by Scope in external tables */

    if (AcpiGbl_ExternalFileList)
    {
        AcpiDmClearExternalList ();
    }

    /* Load any externals defined in the optional external ref file */

    AcpiDmGetExternalsFromFile ();
    return (AE_OK);
}
