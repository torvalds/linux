/******************************************************************************
 *
 * Module Name: nsparse - namespace interface to AML parser
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/actables.h>
#include <contrib/dev/acpica/include/acinterp.h>


#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nsparse")


/*******************************************************************************
 *
 * FUNCTION:    NsExecuteTable
 *
 * PARAMETERS:  TableDesc       - An ACPI table descriptor for table to parse
 *              StartNode       - Where to enter the table into the namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load ACPI/AML table by executing the entire table as a single
 *              large control method.
 *
 * NOTE: The point of this is to execute any module-level code in-place
 * as the table is parsed. Some AML code depends on this behavior.
 *
 * It is a run-time option at this time, but will eventually become
 * the default.
 *
 * Note: This causes the table to only have a single-pass parse.
 * However, this is compatible with other ACPI implementations.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsExecuteTable (
    UINT32                  TableIndex,
    ACPI_NAMESPACE_NODE     *StartNode)
{
    ACPI_STATUS             Status;
    ACPI_TABLE_HEADER       *Table;
    ACPI_OWNER_ID           OwnerId;
    ACPI_EVALUATE_INFO      *Info = NULL;
    UINT32                  AmlLength;
    UINT8                   *AmlStart;
    ACPI_OPERAND_OBJECT     *MethodObj = NULL;


    ACPI_FUNCTION_TRACE (NsExecuteTable);


    Status = AcpiGetTableByIndex (TableIndex, &Table);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Table must consist of at least a complete header */

    if (Table->Length < sizeof (ACPI_TABLE_HEADER))
    {
        return_ACPI_STATUS (AE_BAD_HEADER);
    }

    AmlStart = (UINT8 *) Table + sizeof (ACPI_TABLE_HEADER);
    AmlLength = Table->Length - sizeof (ACPI_TABLE_HEADER);

    Status = AcpiTbGetOwnerId (TableIndex, &OwnerId);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Create, initialize, and link a new temporary method object */

    MethodObj = AcpiUtCreateInternalObject (ACPI_TYPE_METHOD);
    if (!MethodObj)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Allocate the evaluation information block */

    Info = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_EVALUATE_INFO));
    if (!Info)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_PARSE,
        "%s: Create table pseudo-method for [%4.4s] @%p, method %p\n",
        ACPI_GET_FUNCTION_NAME, Table->Signature, Table, MethodObj));

    MethodObj->Method.AmlStart = AmlStart;
    MethodObj->Method.AmlLength = AmlLength;
    MethodObj->Method.OwnerId = OwnerId;
    MethodObj->Method.InfoFlags |= ACPI_METHOD_MODULE_LEVEL;

    Info->PassNumber = ACPI_IMODE_EXECUTE;
    Info->Node = StartNode;
    Info->ObjDesc = MethodObj;
    Info->NodeFlags = Info->Node->Flags;
    Info->FullPathname = AcpiNsGetNormalizedPathname (Info->Node, TRUE);
    if (!Info->FullPathname)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /* Optional object evaluation log */

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EVALUATION,
        "%-26s:  (Definition Block level)\n", "Module-level evaluation"));

    Status = AcpiPsExecuteTable (Info);

    /* Optional object evaluation log */

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EVALUATION,
        "%-26s:  (Definition Block level)\n", "Module-level complete"));

Cleanup:
    if (Info)
    {
        ACPI_FREE (Info->FullPathname);
        Info->FullPathname = NULL;
    }
    ACPI_FREE (Info);
    AcpiUtRemoveReference (MethodObj);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    NsOneCompleteParse
 *
 * PARAMETERS:  PassNumber              - 1 or 2
 *              TableDesc               - The table to be parsed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform one complete parse of an ACPI/AML table.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsOneCompleteParse (
    UINT32                  PassNumber,
    UINT32                  TableIndex,
    ACPI_NAMESPACE_NODE     *StartNode)
{
    ACPI_PARSE_OBJECT       *ParseRoot;
    ACPI_STATUS             Status;
    UINT32                  AmlLength;
    UINT8                   *AmlStart;
    ACPI_WALK_STATE         *WalkState;
    ACPI_TABLE_HEADER       *Table;
    ACPI_OWNER_ID           OwnerId;


    ACPI_FUNCTION_TRACE (NsOneCompleteParse);


    Status = AcpiGetTableByIndex (TableIndex, &Table);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Table must consist of at least a complete header */

    if (Table->Length < sizeof (ACPI_TABLE_HEADER))
    {
        return_ACPI_STATUS (AE_BAD_HEADER);
    }

    AmlStart = (UINT8 *) Table + sizeof (ACPI_TABLE_HEADER);
    AmlLength = Table->Length - sizeof (ACPI_TABLE_HEADER);

    Status = AcpiTbGetOwnerId (TableIndex, &OwnerId);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Create and init a Root Node */

    ParseRoot = AcpiPsCreateScopeOp (AmlStart);
    if (!ParseRoot)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Create and initialize a new walk state */

    WalkState = AcpiDsCreateWalkState (OwnerId, NULL, NULL, NULL);
    if (!WalkState)
    {
        AcpiPsFreeOp (ParseRoot);
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    Status = AcpiDsInitAmlWalk (WalkState, ParseRoot, NULL,
        AmlStart, AmlLength, NULL, (UINT8) PassNumber);
    if (ACPI_FAILURE (Status))
    {
        AcpiDsDeleteWalkState (WalkState);
        goto Cleanup;
    }

    /* Found OSDT table, enable the namespace override feature */

    if (ACPI_COMPARE_NAME(Table->Signature, ACPI_SIG_OSDT) &&
        PassNumber == ACPI_IMODE_LOAD_PASS1)
    {
        WalkState->NamespaceOverride = TRUE;
    }

    /* StartNode is the default location to load the table  */

    if (StartNode && StartNode != AcpiGbl_RootNode)
    {
        Status = AcpiDsScopeStackPush (
            StartNode, ACPI_TYPE_METHOD, WalkState);
        if (ACPI_FAILURE (Status))
        {
            AcpiDsDeleteWalkState (WalkState);
            goto Cleanup;
        }
    }

    /* Parse the AML */

    ACPI_DEBUG_PRINT ((ACPI_DB_PARSE,
        "*PARSE* pass %u parse\n", PassNumber));
    AcpiExEnterInterpreter ();
    Status = AcpiPsParseAml (WalkState);
    AcpiExExitInterpreter ();

Cleanup:
    AcpiPsDeleteParseTree (ParseRoot);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsParseTable
 *
 * PARAMETERS:  TableDesc       - An ACPI table descriptor for table to parse
 *              StartNode       - Where to enter the table into the namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse AML within an ACPI table and return a tree of ops
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsParseTable (
    UINT32                  TableIndex,
    ACPI_NAMESPACE_NODE     *StartNode)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (NsParseTable);


    /*
     * Executes the AML table as one large control method.
     * The point of this is to execute any module-level code in-place
     * as the table is parsed. Some AML code depends on this behavior.
     *
     * Note: This causes the table to only have a single-pass parse.
     * However, this is compatible with other ACPI implementations.
     */
    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_PARSE,
        "%s: **** Start table execution pass\n", ACPI_GET_FUNCTION_NAME));

    Status = AcpiNsExecuteTable (TableIndex, StartNode);

    return_ACPI_STATUS (Status);
}
