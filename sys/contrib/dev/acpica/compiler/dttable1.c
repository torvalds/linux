/******************************************************************************
 *
 * Module Name: dttable1.c - handling for specific ACPI tables
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

/* Compile all complex data tables, signatures starting with A-I */

#include <contrib/dev/acpica/compiler/aslcompiler.h>

#define _COMPONENT          DT_COMPILER
        ACPI_MODULE_NAME    ("dttable1")


static ACPI_DMTABLE_INFO           TableInfoAsfAddress[] =
{
    {ACPI_DMT_BUFFER,   0,               "Addresses", 0},
    {ACPI_DMT_EXIT,     0,               NULL, 0}
};

static ACPI_DMTABLE_INFO           TableInfoDmarPciPath[] =
{
    {ACPI_DMT_PCI_PATH, 0,               "PCI Path", 0},
    {ACPI_DMT_EXIT,     0,               NULL, 0}
};


/******************************************************************************
 *
 * FUNCTION:    DtCompileAsf
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile ASF!.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileAsf (
    void                    **List)
{
    ACPI_ASF_INFO           *AsfTable;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    ACPI_DMTABLE_INFO       *InfoTable;
    ACPI_DMTABLE_INFO       *DataInfoTable = NULL;
    UINT32                  DataCount = 0;
    ACPI_STATUS             Status;
    UINT32                  i;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    DT_FIELD                *SubtableStart;


    while (*PFieldList)
    {
        SubtableStart = *PFieldList;
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoAsfHdr,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);
        DtPushSubtable (Subtable);

        AsfTable = ACPI_CAST_PTR (ACPI_ASF_INFO, Subtable->Buffer);

        switch (AsfTable->Header.Type & 0x7F) /* Mask off top bit */
        {
        case ACPI_ASF_TYPE_INFO:

            InfoTable = AcpiDmTableInfoAsf0;
            break;

        case ACPI_ASF_TYPE_ALERT:

            InfoTable = AcpiDmTableInfoAsf1;
            break;

        case ACPI_ASF_TYPE_CONTROL:

            InfoTable = AcpiDmTableInfoAsf2;
            break;

        case ACPI_ASF_TYPE_BOOT:

            InfoTable = AcpiDmTableInfoAsf3;
            break;

        case ACPI_ASF_TYPE_ADDRESS:

            InfoTable = AcpiDmTableInfoAsf4;
            break;

        default:

            DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart, "ASF!");
            return (AE_ERROR);
        }

        Status = DtCompileTable (PFieldList, InfoTable, &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);

        switch (AsfTable->Header.Type & 0x7F) /* Mask off top bit */
        {
        case ACPI_ASF_TYPE_INFO:

            DataInfoTable = NULL;
            break;

        case ACPI_ASF_TYPE_ALERT:

            DataInfoTable = AcpiDmTableInfoAsf1a;
            DataCount = ACPI_CAST_PTR (ACPI_ASF_ALERT,
                ACPI_SUB_PTR (UINT8, Subtable->Buffer,
                    sizeof (ACPI_ASF_HEADER)))->Alerts;
            break;

        case ACPI_ASF_TYPE_CONTROL:

            DataInfoTable = AcpiDmTableInfoAsf2a;
            DataCount = ACPI_CAST_PTR (ACPI_ASF_REMOTE,
                ACPI_SUB_PTR (UINT8, Subtable->Buffer,
                    sizeof (ACPI_ASF_HEADER)))->Controls;
            break;

        case ACPI_ASF_TYPE_BOOT:

            DataInfoTable = NULL;
            break;

        case ACPI_ASF_TYPE_ADDRESS:

            DataInfoTable = TableInfoAsfAddress;
            DataCount = ACPI_CAST_PTR (ACPI_ASF_ADDRESS,
                ACPI_SUB_PTR (UINT8, Subtable->Buffer,
                    sizeof (ACPI_ASF_HEADER)))->Devices;
            break;

        default:

            DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart, "ASF!");
            return (AE_ERROR);
        }

        if (DataInfoTable)
        {
            switch (AsfTable->Header.Type & 0x7F)
            {
            case ACPI_ASF_TYPE_ADDRESS:

                while (DataCount > 0)
                {
                    Status = DtCompileTable (PFieldList, DataInfoTable,
                        &Subtable);
                    if (ACPI_FAILURE (Status))
                    {
                        return (Status);
                    }

                    DtInsertSubtable (ParentTable, Subtable);
                    DataCount = DataCount - Subtable->Length;
                }
                break;

            default:

                for (i = 0; i < DataCount; i++)
                {
                    Status = DtCompileTable (PFieldList, DataInfoTable,
                        &Subtable);
                    if (ACPI_FAILURE (Status))
                    {
                        return (Status);
                    }

                    DtInsertSubtable (ParentTable, Subtable);
                }
                break;
            }
        }

        DtPopSubtable ();
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileCpep
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile CPEP.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileCpep (
    void                    **List)
{
    ACPI_STATUS             Status;


    Status = DtCompileTwoSubtables (List,
        AcpiDmTableInfoCpep, AcpiDmTableInfoCpep0);
    return (Status);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileCsrt
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile CSRT.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileCsrt (
    void                    **List)
{
    ACPI_STATUS             Status = AE_OK;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    UINT32                  DescriptorCount;
    UINT32                  GroupLength;


    /* Subtables (Resource Groups) */

    ParentTable = DtPeekSubtable ();
    while (*PFieldList)
    {
        /* Resource group subtable */

        Status = DtCompileTable (PFieldList, AcpiDmTableInfoCsrt0,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        /* Compute the number of resource descriptors */

        GroupLength =
            (ACPI_CAST_PTR (ACPI_CSRT_GROUP,
                Subtable->Buffer))->Length -
            (ACPI_CAST_PTR (ACPI_CSRT_GROUP,
                Subtable->Buffer))->SharedInfoLength -
            sizeof (ACPI_CSRT_GROUP);

        DescriptorCount = (GroupLength  /
            sizeof (ACPI_CSRT_DESCRIPTOR));

        DtInsertSubtable (ParentTable, Subtable);
        DtPushSubtable (Subtable);
        ParentTable = DtPeekSubtable ();

        /* Shared info subtable (One per resource group) */

        Status = DtCompileTable (PFieldList, AcpiDmTableInfoCsrt1,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        DtInsertSubtable (ParentTable, Subtable);

        /* Sub-Subtables (Resource Descriptors) */

        while (*PFieldList && DescriptorCount)
        {

            Status = DtCompileTable (PFieldList, AcpiDmTableInfoCsrt2,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            DtInsertSubtable (ParentTable, Subtable);

            DtPushSubtable (Subtable);
            ParentTable = DtPeekSubtable ();
            if (*PFieldList)
            {
                Status = DtCompileTable (PFieldList, AcpiDmTableInfoCsrt2a,
                    &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
                if (Subtable)
                {
                    DtInsertSubtable (ParentTable, Subtable);
                }
            }

            DtPopSubtable ();
            ParentTable = DtPeekSubtable ();
            DescriptorCount--;
        }

        DtPopSubtable ();
        ParentTable = DtPeekSubtable ();
    }

    return (Status);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileDbg2
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile DBG2.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileDbg2 (
    void                    **List)
{
    ACPI_STATUS             Status;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    UINT32                  SubtableCount;
    ACPI_DBG2_HEADER        *Dbg2Header;
    ACPI_DBG2_DEVICE        *DeviceInfo;
    UINT16                  CurrentOffset;
    UINT32                  i;


    /* Main table */

    Status = DtCompileTable (PFieldList, AcpiDmTableInfoDbg2, &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    ParentTable = DtPeekSubtable ();
    DtInsertSubtable (ParentTable, Subtable);

    /* Main table fields */

    Dbg2Header = ACPI_CAST_PTR (ACPI_DBG2_HEADER, Subtable->Buffer);
    Dbg2Header->InfoOffset = sizeof (ACPI_TABLE_HEADER) + ACPI_PTR_DIFF (
        ACPI_ADD_PTR (UINT8, Dbg2Header, sizeof (ACPI_DBG2_HEADER)), Dbg2Header);

    SubtableCount = Dbg2Header->InfoCount;
    DtPushSubtable (Subtable);

    /* Process all Device Information subtables (Count = InfoCount) */

    while (*PFieldList && SubtableCount)
    {
        /* Subtable: Debug Device Information */

        Status = DtCompileTable (PFieldList, AcpiDmTableInfoDbg2Device,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        DeviceInfo = ACPI_CAST_PTR (ACPI_DBG2_DEVICE, Subtable->Buffer);
        CurrentOffset = (UINT16) sizeof (ACPI_DBG2_DEVICE);

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);
        DtPushSubtable (Subtable);

        ParentTable = DtPeekSubtable ();

        /* BaseAddressRegister GAS array (Required, size is RegisterCount) */

        DeviceInfo->BaseAddressOffset = CurrentOffset;
        for (i = 0; *PFieldList && (i < DeviceInfo->RegisterCount); i++)
        {
            Status = DtCompileTable (PFieldList, AcpiDmTableInfoDbg2Addr,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            CurrentOffset += (UINT16) sizeof (ACPI_GENERIC_ADDRESS);
            DtInsertSubtable (ParentTable, Subtable);
        }

        /* AddressSize array (Required, size = RegisterCount) */

        DeviceInfo->AddressSizeOffset = CurrentOffset;
        for (i = 0; *PFieldList && (i < DeviceInfo->RegisterCount); i++)
        {
            Status = DtCompileTable (PFieldList, AcpiDmTableInfoDbg2Size,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            CurrentOffset += (UINT16) sizeof (UINT32);
            DtInsertSubtable (ParentTable, Subtable);
        }

        /* NamespaceString device identifier (Required, size = NamePathLength) */

        DeviceInfo->NamepathOffset = CurrentOffset;
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoDbg2Name,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        /* Update the device info header */

        DeviceInfo->NamepathLength = (UINT16) Subtable->Length;
        CurrentOffset += (UINT16) DeviceInfo->NamepathLength;
        DtInsertSubtable (ParentTable, Subtable);

        /* OemData - Variable-length data (Optional, size = OemDataLength) */

        Status = DtCompileTable (PFieldList, AcpiDmTableInfoDbg2OemData,
            &Subtable);
        if (Status == AE_END_OF_TABLE)
        {
            /* optional field was not found and we're at the end of the file */

            goto subtableDone;
        }
        else if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        /* Update the device info header (zeros if no OEM data present) */

        DeviceInfo->OemDataOffset = 0;
        DeviceInfo->OemDataLength = 0;

        /* Optional subtable (OemData) */

        if (Subtable && Subtable->Length)
        {
            DeviceInfo->OemDataOffset = CurrentOffset;
            DeviceInfo->OemDataLength = (UINT16) Subtable->Length;

            DtInsertSubtable (ParentTable, Subtable);
        }
subtableDone:
        SubtableCount--;
        DtPopSubtable (); /* Get next Device Information subtable */
    }

    DtPopSubtable ();
    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileDmar
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile DMAR.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileDmar (
    void                    **List)
{
    ACPI_STATUS             Status;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    DT_FIELD                *SubtableStart;
    ACPI_DMTABLE_INFO       *InfoTable;
    ACPI_DMAR_HEADER        *DmarHeader;
    ACPI_DMAR_DEVICE_SCOPE  *DmarDeviceScope;
    UINT32                  DeviceScopeLength;
    UINT32                  PciPathLength;


    Status = DtCompileTable (PFieldList, AcpiDmTableInfoDmar, &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    ParentTable = DtPeekSubtable ();
    DtInsertSubtable (ParentTable, Subtable);
    DtPushSubtable (Subtable);

    while (*PFieldList)
    {
        /* DMAR Header */

        SubtableStart = *PFieldList;
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoDmarHdr,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);
        DtPushSubtable (Subtable);

        DmarHeader = ACPI_CAST_PTR (ACPI_DMAR_HEADER, Subtable->Buffer);

        switch (DmarHeader->Type)
        {
        case ACPI_DMAR_TYPE_HARDWARE_UNIT:

            InfoTable = AcpiDmTableInfoDmar0;
            break;

        case ACPI_DMAR_TYPE_RESERVED_MEMORY:

            InfoTable = AcpiDmTableInfoDmar1;
            break;

        case ACPI_DMAR_TYPE_ROOT_ATS:

            InfoTable = AcpiDmTableInfoDmar2;
            break;

        case ACPI_DMAR_TYPE_HARDWARE_AFFINITY:

            InfoTable = AcpiDmTableInfoDmar3;
            break;

        case ACPI_DMAR_TYPE_NAMESPACE:

            InfoTable = AcpiDmTableInfoDmar4;
            break;

        default:

            DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart, "DMAR");
            return (AE_ERROR);
        }

        /* DMAR Subtable */

        Status = DtCompileTable (PFieldList, InfoTable, &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);

        /*
         * Optional Device Scope subtables
         */
        if ((DmarHeader->Type == ACPI_DMAR_TYPE_HARDWARE_AFFINITY) ||
            (DmarHeader->Type == ACPI_DMAR_TYPE_NAMESPACE))
        {
            /* These types do not support device scopes */

            DtPopSubtable ();
            continue;
        }

        DtPushSubtable (Subtable);
        DeviceScopeLength = DmarHeader->Length - Subtable->Length -
            ParentTable->Length;
        while (DeviceScopeLength)
        {
            Status = DtCompileTable (PFieldList, AcpiDmTableInfoDmarScope,
                &Subtable);
            if (Status == AE_NOT_FOUND)
            {
                break;
            }

            ParentTable = DtPeekSubtable ();
            DtInsertSubtable (ParentTable, Subtable);
            DtPushSubtable (Subtable);

            DmarDeviceScope = ACPI_CAST_PTR (ACPI_DMAR_DEVICE_SCOPE, Subtable->Buffer);

            /* Optional PCI Paths */

            PciPathLength = DmarDeviceScope->Length - Subtable->Length;
            while (PciPathLength)
            {
                Status = DtCompileTable (PFieldList, TableInfoDmarPciPath,
                    &Subtable);
                if (Status == AE_NOT_FOUND)
                {
                    DtPopSubtable ();
                    break;
                }

                ParentTable = DtPeekSubtable ();
                DtInsertSubtable (ParentTable, Subtable);
                PciPathLength -= Subtable->Length;
            }

            DtPopSubtable ();
            DeviceScopeLength -= DmarDeviceScope->Length;
        }

        DtPopSubtable ();
        DtPopSubtable ();
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileDrtm
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile DRTM.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileDrtm (
    void                    **List)
{
    ACPI_STATUS             Status;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    UINT32                  Count;
    /* ACPI_TABLE_DRTM         *Drtm; */
    ACPI_DRTM_VTABLE_LIST   *DrtmVtl;
    ACPI_DRTM_RESOURCE_LIST *DrtmRl;
    /* ACPI_DRTM_DPS_ID        *DrtmDps; */


    ParentTable = DtPeekSubtable ();

    /* Compile DRTM header */

    Status = DtCompileTable (PFieldList, AcpiDmTableInfoDrtm,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }
    DtInsertSubtable (ParentTable, Subtable);

    /*
     * Using ACPI_SUB_PTR, We needn't define a separate structure. Care
     * should be taken to avoid accessing ACPI_TABLE_HADER fields.
     */
#if 0
    Drtm = ACPI_SUB_PTR (ACPI_TABLE_DRTM,
        Subtable->Buffer, sizeof (ACPI_TABLE_HEADER));
#endif
    /* Compile VTL */

    Status = DtCompileTable (PFieldList, AcpiDmTableInfoDrtm0,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    DtInsertSubtable (ParentTable, Subtable);
    DrtmVtl = ACPI_CAST_PTR (ACPI_DRTM_VTABLE_LIST, Subtable->Buffer);

    DtPushSubtable (Subtable);
    ParentTable = DtPeekSubtable ();
    Count = 0;

    while (*PFieldList)
    {
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoDrtm0a,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        if (!Subtable)
        {
            break;
        }
        DtInsertSubtable (ParentTable, Subtable);
        Count++;
    }

    DrtmVtl->ValidatedTableCount = Count;
    DtPopSubtable ();
    ParentTable = DtPeekSubtable ();

    /* Compile RL */

    Status = DtCompileTable (PFieldList, AcpiDmTableInfoDrtm1,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    DtInsertSubtable (ParentTable, Subtable);
    DrtmRl = ACPI_CAST_PTR (ACPI_DRTM_RESOURCE_LIST, Subtable->Buffer);

    DtPushSubtable (Subtable);
    ParentTable = DtPeekSubtable ();
    Count = 0;

    while (*PFieldList)
    {
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoDrtm1a,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        if (!Subtable)
        {
            break;
        }

        DtInsertSubtable (ParentTable, Subtable);
        Count++;
    }

    DrtmRl->ResourceCount = Count;
    DtPopSubtable ();
    ParentTable = DtPeekSubtable ();

    /* Compile DPS */

    Status = DtCompileTable (PFieldList, AcpiDmTableInfoDrtm2,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }
    DtInsertSubtable (ParentTable, Subtable);
    /* DrtmDps = ACPI_CAST_PTR (ACPI_DRTM_DPS_ID, Subtable->Buffer);*/


    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileEinj
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile EINJ.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileEinj (
    void                    **List)
{
    ACPI_STATUS             Status;


    Status = DtCompileTwoSubtables (List,
        AcpiDmTableInfoEinj, AcpiDmTableInfoEinj0);
    return (Status);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileErst
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile ERST.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileErst (
    void                    **List)
{
    ACPI_STATUS             Status;


    Status = DtCompileTwoSubtables (List,
        AcpiDmTableInfoErst, AcpiDmTableInfoEinj0);
    return (Status);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileGtdt
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile GTDT.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileGtdt (
    void                    **List)
{
    ACPI_STATUS             Status;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    DT_FIELD                *SubtableStart;
    ACPI_SUBTABLE_HEADER    *GtdtHeader;
    ACPI_DMTABLE_INFO       *InfoTable;
    UINT32                  GtCount;
    ACPI_TABLE_HEADER       *Header;


    ParentTable = DtPeekSubtable ();

    Header = ACPI_CAST_PTR (ACPI_TABLE_HEADER, ParentTable->Buffer);

    /* Compile the main table */

    Status = DtCompileTable (PFieldList, AcpiDmTableInfoGtdt,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* GTDT revision 3 later contains 2 extra fields before subtables */

    if (Header->Revision > 2)
    {
        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);

        Status = DtCompileTable (PFieldList,
            AcpiDmTableInfoGtdtEl2, &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }

    ParentTable = DtPeekSubtable ();
    DtInsertSubtable (ParentTable, Subtable);

    while (*PFieldList)
    {
        SubtableStart = *PFieldList;
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoGtdtHdr,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);
        DtPushSubtable (Subtable);

        GtdtHeader = ACPI_CAST_PTR (ACPI_SUBTABLE_HEADER, Subtable->Buffer);

        switch (GtdtHeader->Type)
        {
        case ACPI_GTDT_TYPE_TIMER_BLOCK:

            InfoTable = AcpiDmTableInfoGtdt0;
            break;

        case ACPI_GTDT_TYPE_WATCHDOG:

            InfoTable = AcpiDmTableInfoGtdt1;
            break;

        default:

            DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart, "GTDT");
            return (AE_ERROR);
        }

        Status = DtCompileTable (PFieldList, InfoTable, &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);

        /*
         * Additional GT block subtable data
         */

        switch (GtdtHeader->Type)
        {
        case ACPI_GTDT_TYPE_TIMER_BLOCK:

            DtPushSubtable (Subtable);
            ParentTable = DtPeekSubtable ();

            GtCount = (ACPI_CAST_PTR (ACPI_GTDT_TIMER_BLOCK,
                Subtable->Buffer - sizeof(ACPI_GTDT_HEADER)))->TimerCount;

            while (GtCount)
            {
                Status = DtCompileTable (PFieldList, AcpiDmTableInfoGtdt0a,
                    &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }

                DtInsertSubtable (ParentTable, Subtable);
                GtCount--;
            }

            DtPopSubtable ();
            break;

        default:

            break;
        }

        DtPopSubtable ();
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileFpdt
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile FPDT.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileFpdt (
    void                    **List)
{
    ACPI_STATUS             Status;
    ACPI_FPDT_HEADER        *FpdtHeader;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    ACPI_DMTABLE_INFO       *InfoTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    DT_FIELD                *SubtableStart;


    while (*PFieldList)
    {
        SubtableStart = *PFieldList;
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoFpdtHdr,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);
        DtPushSubtable (Subtable);

        FpdtHeader = ACPI_CAST_PTR (ACPI_FPDT_HEADER, Subtable->Buffer);

        switch (FpdtHeader->Type)
        {
        case ACPI_FPDT_TYPE_BOOT:

            InfoTable = AcpiDmTableInfoFpdt0;
            break;

        case ACPI_FPDT_TYPE_S3PERF:

            InfoTable = AcpiDmTableInfoFpdt1;
            break;

        default:

            DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart, "FPDT");
            return (AE_ERROR);
            break;
        }

        Status = DtCompileTable (PFieldList, InfoTable, &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);
        DtPopSubtable ();
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileHest
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile HEST.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileHest (
    void                    **List)
{
    ACPI_STATUS             Status;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    DT_FIELD                *SubtableStart;
    ACPI_DMTABLE_INFO       *InfoTable;
    UINT16                  Type;
    UINT32                  BankCount;


    Status = DtCompileTable (PFieldList, AcpiDmTableInfoHest,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    ParentTable = DtPeekSubtable ();
    DtInsertSubtable (ParentTable, Subtable);

    while (*PFieldList)
    {
        /* Get subtable type */

        SubtableStart = *PFieldList;
        DtCompileInteger ((UINT8 *) &Type, *PFieldList, 2, 0);

        switch (Type)
        {
        case ACPI_HEST_TYPE_IA32_CHECK:

            InfoTable = AcpiDmTableInfoHest0;
            break;

        case ACPI_HEST_TYPE_IA32_CORRECTED_CHECK:

            InfoTable = AcpiDmTableInfoHest1;
            break;

        case ACPI_HEST_TYPE_IA32_NMI:

            InfoTable = AcpiDmTableInfoHest2;
            break;

        case ACPI_HEST_TYPE_AER_ROOT_PORT:

            InfoTable = AcpiDmTableInfoHest6;
            break;

        case ACPI_HEST_TYPE_AER_ENDPOINT:

            InfoTable = AcpiDmTableInfoHest7;
            break;

        case ACPI_HEST_TYPE_AER_BRIDGE:

            InfoTable = AcpiDmTableInfoHest8;
            break;

        case ACPI_HEST_TYPE_GENERIC_ERROR:

            InfoTable = AcpiDmTableInfoHest9;
            break;

        case ACPI_HEST_TYPE_GENERIC_ERROR_V2:

            InfoTable = AcpiDmTableInfoHest10;
            break;

        case ACPI_HEST_TYPE_IA32_DEFERRED_CHECK:

            InfoTable = AcpiDmTableInfoHest11;
            break;

        default:

            /* Cannot continue on unknown type */

            DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart, "HEST");
            return (AE_ERROR);
        }

        Status = DtCompileTable (PFieldList, InfoTable, &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        DtInsertSubtable (ParentTable, Subtable);

        /*
         * Additional subtable data - IA32 Error Bank(s)
         */
        BankCount = 0;
        switch (Type)
        {
        case ACPI_HEST_TYPE_IA32_CHECK:

            BankCount = (ACPI_CAST_PTR (ACPI_HEST_IA_MACHINE_CHECK,
                Subtable->Buffer))->NumHardwareBanks;
            break;

        case ACPI_HEST_TYPE_IA32_CORRECTED_CHECK:

            BankCount = (ACPI_CAST_PTR (ACPI_HEST_IA_CORRECTED,
                Subtable->Buffer))->NumHardwareBanks;
            break;

        case ACPI_HEST_TYPE_IA32_DEFERRED_CHECK:

            BankCount = (ACPI_CAST_PTR (ACPI_HEST_IA_DEFERRED_CHECK,
                Subtable->Buffer))->NumHardwareBanks;
            break;

        default:

            break;
        }

        while (BankCount)
        {
            Status = DtCompileTable (PFieldList, AcpiDmTableInfoHestBank,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            DtInsertSubtable (ParentTable, Subtable);
            BankCount--;
        }
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileHmat
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile HMAT.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileHmat (
    void                    **List)
{
    ACPI_STATUS             Status;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    DT_FIELD                *SubtableStart;
    DT_FIELD                *EntryStart;
    ACPI_HMAT_STRUCTURE     *HmatStruct;
    ACPI_HMAT_LOCALITY      *HmatLocality;
    ACPI_HMAT_CACHE         *HmatCache;
    ACPI_DMTABLE_INFO       *InfoTable;
    UINT32                  IntPDNumber;
    UINT32                  TgtPDNumber;
    UINT64                  EntryNumber;
    UINT16                  SMBIOSHandleNumber;


    ParentTable = DtPeekSubtable ();

    Status = DtCompileTable (PFieldList, AcpiDmTableInfoHmat,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }
    DtInsertSubtable (ParentTable, Subtable);

    while (*PFieldList)
    {
        /* Compile HMAT structure header */

        SubtableStart = *PFieldList;
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoHmatHdr,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        DtInsertSubtable (ParentTable, Subtable);

        HmatStruct = ACPI_CAST_PTR (ACPI_HMAT_STRUCTURE, Subtable->Buffer);
        HmatStruct->Length = Subtable->Length;

        /* Compile HMAT structure body */

        switch (HmatStruct->Type)
        {
        case ACPI_HMAT_TYPE_ADDRESS_RANGE:

            InfoTable = AcpiDmTableInfoHmat0;
            break;

        case ACPI_HMAT_TYPE_LOCALITY:

            InfoTable = AcpiDmTableInfoHmat1;
            break;

        case ACPI_HMAT_TYPE_CACHE:

            InfoTable = AcpiDmTableInfoHmat2;
            break;

        default:

            DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart, "HMAT");
            return (AE_ERROR);
        }

        Status = DtCompileTable (PFieldList, InfoTable, &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        DtInsertSubtable (ParentTable, Subtable);
        HmatStruct->Length += Subtable->Length;

        /* Compile HMAT structure additionals */

        switch (HmatStruct->Type)
        {
        case ACPI_HMAT_TYPE_LOCALITY:

            HmatLocality = ACPI_SUB_PTR (ACPI_HMAT_LOCALITY,
                Subtable->Buffer, sizeof (ACPI_HMAT_STRUCTURE));

            /* Compile initiator proximity domain list */

            IntPDNumber = 0;
            while (*PFieldList)
            {
                Status = DtCompileTable (PFieldList,
                    AcpiDmTableInfoHmat1a, &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
                if (!Subtable)
                {
                    break;
                }
                DtInsertSubtable (ParentTable, Subtable);
                HmatStruct->Length += Subtable->Length;
                IntPDNumber++;
            }
            HmatLocality->NumberOfInitiatorPDs = IntPDNumber;

            /* Compile target proximity domain list */

            TgtPDNumber = 0;
            while (*PFieldList)
            {
                Status = DtCompileTable (PFieldList,
                    AcpiDmTableInfoHmat1b, &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
                if (!Subtable)
                {
                    break;
                }
                DtInsertSubtable (ParentTable, Subtable);
                HmatStruct->Length += Subtable->Length;
                TgtPDNumber++;
            }
            HmatLocality->NumberOfTargetPDs = TgtPDNumber;

            /* Save start of the entries for reporting errors */

            EntryStart = *PFieldList;

            /* Compile latency/bandwidth entries */

            EntryNumber = 0;
            while (*PFieldList)
            {
                Status = DtCompileTable (PFieldList,
                    AcpiDmTableInfoHmat1c, &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
                if (!Subtable)
                {
                    break;
                }
                DtInsertSubtable (ParentTable, Subtable);
                HmatStruct->Length += Subtable->Length;
                EntryNumber++;
            }

            /* Validate number of entries */

            if (EntryNumber !=
                ((UINT64)IntPDNumber * (UINT64)TgtPDNumber))
            {
                DtFatal (ASL_MSG_INVALID_EXPRESSION, EntryStart, "HMAT");
                return (AE_ERROR);
            }
            break;

        case ACPI_HMAT_TYPE_CACHE:

            /* Compile SMBIOS handles */

            HmatCache = ACPI_SUB_PTR (ACPI_HMAT_CACHE,
                Subtable->Buffer, sizeof (ACPI_HMAT_STRUCTURE));
            SMBIOSHandleNumber = 0;
            while (*PFieldList)
            {
                Status = DtCompileTable (PFieldList,
                    AcpiDmTableInfoHmat2a, &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
                if (!Subtable)
                {
                    break;
                }
                DtInsertSubtable (ParentTable, Subtable);
                HmatStruct->Length += Subtable->Length;
                SMBIOSHandleNumber++;
            }
            HmatCache->NumberOfSMBIOSHandles = SMBIOSHandleNumber;
            break;

        default:

            break;
        }
    }

    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileIort
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile IORT.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileIort (
    void                    **List)
{
    ACPI_STATUS             Status;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    DT_FIELD                *SubtableStart;
    ACPI_TABLE_IORT         *Iort;
    ACPI_IORT_NODE          *IortNode;
    ACPI_IORT_ITS_GROUP     *IortItsGroup;
    ACPI_IORT_SMMU          *IortSmmu;
    UINT32                  NodeNumber;
    UINT32                  NodeLength;
    UINT32                  IdMappingNumber;
    UINT32                  ItsNumber;
    UINT32                  ContextIrptNumber;
    UINT32                  PmuIrptNumber;
    UINT32                  PaddingLength;


    ParentTable = DtPeekSubtable ();

    Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }
    DtInsertSubtable (ParentTable, Subtable);

    /*
     * Using ACPI_SUB_PTR, We needn't define a separate structure. Care
     * should be taken to avoid accessing ACPI_TABLE_HEADER fields.
     */
    Iort = ACPI_SUB_PTR (ACPI_TABLE_IORT,
        Subtable->Buffer, sizeof (ACPI_TABLE_HEADER));

    /*
     * OptionalPadding - Variable-length data
     * (Optional, size = OffsetToNodes - sizeof (ACPI_TABLE_IORT))
     * Optionally allows the generic data types to be used for filling
     * this field.
     */
    Iort->NodeOffset = sizeof (ACPI_TABLE_IORT);
    Status = DtCompileTable (PFieldList, AcpiDmTableInfoIortPad,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }
    if (Subtable)
    {
        DtInsertSubtable (ParentTable, Subtable);
        Iort->NodeOffset += Subtable->Length;
    }
    else
    {
        Status = DtCompileGeneric (ACPI_CAST_PTR (void *, PFieldList),
            AcpiDmTableInfoIortHdr[0].Name, &PaddingLength);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
        Iort->NodeOffset += PaddingLength;
    }

    NodeNumber = 0;
    while (*PFieldList)
    {
        SubtableStart = *PFieldList;
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoIortHdr,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        DtInsertSubtable (ParentTable, Subtable);
        IortNode = ACPI_CAST_PTR (ACPI_IORT_NODE, Subtable->Buffer);
        NodeLength = ACPI_OFFSET (ACPI_IORT_NODE, NodeData);

        DtPushSubtable (Subtable);
        ParentTable = DtPeekSubtable ();

        switch (IortNode->Type)
        {
        case ACPI_IORT_NODE_ITS_GROUP:

            Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort0,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            DtInsertSubtable (ParentTable, Subtable);
            IortItsGroup = ACPI_CAST_PTR (ACPI_IORT_ITS_GROUP, Subtable->Buffer);
            NodeLength += Subtable->Length;

            ItsNumber = 0;
            while (*PFieldList)
            {
                Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort0a,
                    &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }
                if (!Subtable)
                {
                    break;
                }

                DtInsertSubtable (ParentTable, Subtable);
                NodeLength += Subtable->Length;
                ItsNumber++;
            }

            IortItsGroup->ItsCount = ItsNumber;
            break;

        case ACPI_IORT_NODE_NAMED_COMPONENT:

            Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort1,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            DtInsertSubtable (ParentTable, Subtable);
            NodeLength += Subtable->Length;

            /*
             * Padding - Variable-length data
             * Optionally allows the offset of the ID mappings to be used
             * for filling this field.
             */
            Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort1a,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            if (Subtable)
            {
                DtInsertSubtable (ParentTable, Subtable);
                NodeLength += Subtable->Length;
            }
            else
            {
                if (NodeLength > IortNode->MappingOffset)
                {
                    return (AE_BAD_DATA);
                }

                if (NodeLength < IortNode->MappingOffset)
                {
                    Status = DtCompilePadding (
                        IortNode->MappingOffset - NodeLength,
                        &Subtable);
                    if (ACPI_FAILURE (Status))
                    {
                        return (Status);
                    }

                    DtInsertSubtable (ParentTable, Subtable);
                    NodeLength = IortNode->MappingOffset;
                }
            }
            break;

        case ACPI_IORT_NODE_PCI_ROOT_COMPLEX:

            Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort2,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            DtInsertSubtable (ParentTable, Subtable);
            NodeLength += Subtable->Length;
            break;

        case ACPI_IORT_NODE_SMMU:

            Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort3,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            DtInsertSubtable (ParentTable, Subtable);
            IortSmmu = ACPI_CAST_PTR (ACPI_IORT_SMMU, Subtable->Buffer);
            NodeLength += Subtable->Length;

            /* Compile global interrupt array */

            IortSmmu->GlobalInterruptOffset = NodeLength;
            Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort3a,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            DtInsertSubtable (ParentTable, Subtable);
            NodeLength += Subtable->Length;

            /* Compile context interrupt array */

            ContextIrptNumber = 0;
            IortSmmu->ContextInterruptOffset = NodeLength;
            while (*PFieldList)
            {
                Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort3b,
                    &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }

                if (!Subtable)
                {
                    break;
                }

                DtInsertSubtable (ParentTable, Subtable);
                NodeLength += Subtable->Length;
                ContextIrptNumber++;
            }

            IortSmmu->ContextInterruptCount = ContextIrptNumber;

            /* Compile PMU interrupt array */

            PmuIrptNumber = 0;
            IortSmmu->PmuInterruptOffset = NodeLength;
            while (*PFieldList)
            {
                Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort3c,
                    &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }

                if (!Subtable)
                {
                    break;
                }

                DtInsertSubtable (ParentTable, Subtable);
                NodeLength += Subtable->Length;
                PmuIrptNumber++;
            }

            IortSmmu->PmuInterruptCount = PmuIrptNumber;
            break;

        case ACPI_IORT_NODE_SMMU_V3:

            Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort4,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            DtInsertSubtable (ParentTable, Subtable);
            NodeLength += Subtable->Length;
            break;

        case ACPI_IORT_NODE_PMCG:

            Status = DtCompileTable (PFieldList, AcpiDmTableInfoIort5,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            DtInsertSubtable (ParentTable, Subtable);
            NodeLength += Subtable->Length;
            break;

        default:

            DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart, "IORT");
            return (AE_ERROR);
        }

        /* Compile Array of ID mappings */

        IortNode->MappingOffset = NodeLength;
        IdMappingNumber = 0;
        while (*PFieldList)
        {
            Status = DtCompileTable (PFieldList, AcpiDmTableInfoIortMap,
                &Subtable);
            if (ACPI_FAILURE (Status))
            {
                return (Status);
            }

            if (!Subtable)
            {
                break;
            }

            DtInsertSubtable (ParentTable, Subtable);
            NodeLength += sizeof (ACPI_IORT_ID_MAPPING);
            IdMappingNumber++;
        }

        IortNode->MappingCount = IdMappingNumber;
        if (!IdMappingNumber)
        {
            IortNode->MappingOffset = 0;
        }

        /*
         * Node length can be determined by DT_LENGTH option
         * IortNode->Length = NodeLength;
         */
        DtPopSubtable ();
        ParentTable = DtPeekSubtable ();
        NodeNumber++;
    }

    Iort->NodeCount = NodeNumber;
    return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    DtCompileIvrs
 *
 * PARAMETERS:  List                - Current field list pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compile IVRS.
 *
 *****************************************************************************/

ACPI_STATUS
DtCompileIvrs (
    void                    **List)
{
    ACPI_STATUS             Status;
    DT_SUBTABLE             *Subtable;
    DT_SUBTABLE             *ParentTable;
    DT_FIELD                **PFieldList = (DT_FIELD **) List;
    DT_FIELD                *SubtableStart;
    ACPI_DMTABLE_INFO       *InfoTable;
    ACPI_IVRS_HEADER        *IvrsHeader;
    UINT8                   EntryType;


    Status = DtCompileTable (PFieldList, AcpiDmTableInfoIvrs,
        &Subtable);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    ParentTable = DtPeekSubtable ();
    DtInsertSubtable (ParentTable, Subtable);

    while (*PFieldList)
    {
        SubtableStart = *PFieldList;
        Status = DtCompileTable (PFieldList, AcpiDmTableInfoIvrsHdr,
            &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);
        DtPushSubtable (Subtable);

        IvrsHeader = ACPI_CAST_PTR (ACPI_IVRS_HEADER, Subtable->Buffer);

        switch (IvrsHeader->Type)
        {
        case ACPI_IVRS_TYPE_HARDWARE:

            InfoTable = AcpiDmTableInfoIvrs0;
            break;

        case ACPI_IVRS_TYPE_MEMORY1:
        case ACPI_IVRS_TYPE_MEMORY2:
        case ACPI_IVRS_TYPE_MEMORY3:

            InfoTable = AcpiDmTableInfoIvrs1;
            break;

        default:

            DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart, "IVRS");
            return (AE_ERROR);
        }

        Status = DtCompileTable (PFieldList, InfoTable, &Subtable);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        ParentTable = DtPeekSubtable ();
        DtInsertSubtable (ParentTable, Subtable);

        if (IvrsHeader->Type == ACPI_IVRS_TYPE_HARDWARE)
        {
            while (*PFieldList &&
                !strcmp ((*PFieldList)->Name, "Entry Type"))
            {
                SubtableStart = *PFieldList;
                DtCompileInteger (&EntryType, *PFieldList, 1, 0);

                switch (EntryType)
                {
                /* 4-byte device entries */

                case ACPI_IVRS_TYPE_PAD4:
                case ACPI_IVRS_TYPE_ALL:
                case ACPI_IVRS_TYPE_SELECT:
                case ACPI_IVRS_TYPE_START:
                case ACPI_IVRS_TYPE_END:

                    InfoTable = AcpiDmTableInfoIvrs4;
                    break;

                /* 8-byte entries, type A */

                case ACPI_IVRS_TYPE_ALIAS_SELECT:
                case ACPI_IVRS_TYPE_ALIAS_START:

                    InfoTable = AcpiDmTableInfoIvrs8a;
                    break;

                /* 8-byte entries, type B */

                case ACPI_IVRS_TYPE_PAD8:
                case ACPI_IVRS_TYPE_EXT_SELECT:
                case ACPI_IVRS_TYPE_EXT_START:

                    InfoTable = AcpiDmTableInfoIvrs8b;
                    break;

                /* 8-byte entries, type C */

                case ACPI_IVRS_TYPE_SPECIAL:

                    InfoTable = AcpiDmTableInfoIvrs8c;
                    break;

                default:

                    DtFatal (ASL_MSG_UNKNOWN_SUBTABLE, SubtableStart,
                        "IVRS Device Entry");
                    return (AE_ERROR);
                }

                Status = DtCompileTable (PFieldList, InfoTable,
                    &Subtable);
                if (ACPI_FAILURE (Status))
                {
                    return (Status);
                }

                DtInsertSubtable (ParentTable, Subtable);
            }
        }

        DtPopSubtable ();
    }

    return (AE_OK);
}
