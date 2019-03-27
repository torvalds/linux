/******************************************************************************
 *
 * Module Name: dmtbdump2 - Dump ACPI data tables that contain no AML code
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
#include <contrib/dev/acpica/include/acdisasm.h>
#include <contrib/dev/acpica/include/actables.h>

/* This module used for application-level code only */

#define _COMPONENT          ACPI_CA_DISASSEMBLER
        ACPI_MODULE_NAME    ("dmtbdump2")


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpIort
 *
 * PARAMETERS:  Table               - A IORT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a IORT
 *
 ******************************************************************************/

void
AcpiDmDumpIort (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    ACPI_TABLE_IORT         *Iort;
    ACPI_IORT_NODE          *IortNode;
    ACPI_IORT_ITS_GROUP     *IortItsGroup = NULL;
    ACPI_IORT_SMMU          *IortSmmu = NULL;
    UINT32                  Offset;
    UINT32                  NodeOffset;
    UINT32                  Length;
    ACPI_DMTABLE_INFO       *InfoTable;
    char                    *String;
    UINT32                  i;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoIort);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    Iort = ACPI_CAST_PTR (ACPI_TABLE_IORT, Table);
    Offset = sizeof (ACPI_TABLE_IORT);

    /* Dump the OptionalPadding (optional) */

    if (Iort->NodeOffset > Offset)
    {
        Status = AcpiDmDumpTable (Table->Length, Offset, Table,
            Iort->NodeOffset - Offset, AcpiDmTableInfoIortPad);
        if (ACPI_FAILURE (Status))
        {
            return;
        }
    }

    Offset = Iort->NodeOffset;
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        IortNode = ACPI_ADD_PTR (ACPI_IORT_NODE, Table, Offset);
        AcpiOsPrintf ("\n");
        Length = ACPI_OFFSET (ACPI_IORT_NODE, NodeData);
        Status = AcpiDmDumpTable (Table->Length, Offset,
            IortNode, Length, AcpiDmTableInfoIortHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        NodeOffset = Length;

        switch (IortNode->Type)
        {
        case ACPI_IORT_NODE_ITS_GROUP:

            InfoTable = AcpiDmTableInfoIort0;
            Length = ACPI_OFFSET (ACPI_IORT_ITS_GROUP, Identifiers);
            IortItsGroup = ACPI_ADD_PTR (ACPI_IORT_ITS_GROUP, IortNode, NodeOffset);
            break;

        case ACPI_IORT_NODE_NAMED_COMPONENT:

            InfoTable = AcpiDmTableInfoIort1;
            Length = ACPI_OFFSET (ACPI_IORT_NAMED_COMPONENT, DeviceName);
            String = ACPI_ADD_PTR (char, IortNode, NodeOffset + Length);
            Length += strlen (String) + 1;
            break;

        case ACPI_IORT_NODE_PCI_ROOT_COMPLEX:

            InfoTable = AcpiDmTableInfoIort2;
            Length = IortNode->Length - NodeOffset;
            break;

        case ACPI_IORT_NODE_SMMU:

            InfoTable = AcpiDmTableInfoIort3;
            Length = ACPI_OFFSET (ACPI_IORT_SMMU, Interrupts);
            IortSmmu = ACPI_ADD_PTR (ACPI_IORT_SMMU, IortNode, NodeOffset);
            break;

        case ACPI_IORT_NODE_SMMU_V3:

            InfoTable = AcpiDmTableInfoIort4;
            Length = IortNode->Length - NodeOffset;
            break;

        case ACPI_IORT_NODE_PMCG:

            InfoTable = AcpiDmTableInfoIort5;
            Length = IortNode->Length - NodeOffset;
            break;

        default:

            AcpiOsPrintf ("\n**** Unknown IORT node type 0x%X\n",
                IortNode->Type);

            /* Attempt to continue */

            if (!IortNode->Length)
            {
                AcpiOsPrintf ("Invalid zero length IORT node\n");
                return;
            }
            goto NextSubtable;
        }

        /* Dump the node subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset + NodeOffset,
            ACPI_ADD_PTR (ACPI_IORT_NODE, IortNode, NodeOffset),
            Length, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        NodeOffset += Length;

        /* Dump the node specific data */

        switch (IortNode->Type)
        {
        case ACPI_IORT_NODE_ITS_GROUP:

            /* Validate IortItsGroup to avoid compiler warnings */

            if (IortItsGroup)
            {
                for (i = 0; i < IortItsGroup->ItsCount; i++)
                {
                    Status = AcpiDmDumpTable (Table->Length, Offset + NodeOffset,
                        ACPI_ADD_PTR (ACPI_IORT_NODE, IortNode, NodeOffset),
                        4, AcpiDmTableInfoIort0a);
                    NodeOffset += 4;
                }
            }
            break;

        case ACPI_IORT_NODE_NAMED_COMPONENT:

            /* Dump the Padding (optional) */

            if (IortNode->Length > NodeOffset)
            {
                Status = AcpiDmDumpTable (Table->Length, Offset + NodeOffset,
                    Table, IortNode->Length - NodeOffset,
                    AcpiDmTableInfoIort1a);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }
            }
            break;

        case ACPI_IORT_NODE_SMMU:

            AcpiOsPrintf ("\n");

            /* Validate IortSmmu to avoid compiler warnings */

            if (IortSmmu)
            {
                Length = 2 * sizeof (UINT64);
                NodeOffset = IortSmmu->GlobalInterruptOffset;
                Status = AcpiDmDumpTable (Table->Length, Offset + NodeOffset,
                    ACPI_ADD_PTR (ACPI_IORT_NODE, IortNode, NodeOffset),
                    Length, AcpiDmTableInfoIort3a);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }

                NodeOffset = IortSmmu->ContextInterruptOffset;
                for (i = 0; i < IortSmmu->ContextInterruptCount; i++)
                {
                    Status = AcpiDmDumpTable (Table->Length, Offset + NodeOffset,
                        ACPI_ADD_PTR (ACPI_IORT_NODE, IortNode, NodeOffset),
                        8, AcpiDmTableInfoIort3b);
                    if (ACPI_FAILURE (Status))
                    {
                        return;
                    }

                    NodeOffset += 8;
                }

                NodeOffset = IortSmmu->PmuInterruptOffset;
                for (i = 0; i < IortSmmu->PmuInterruptCount; i++)
                {
                    Status = AcpiDmDumpTable (Table->Length, Offset + NodeOffset,
                        ACPI_ADD_PTR (ACPI_IORT_NODE, IortNode, NodeOffset),
                        8, AcpiDmTableInfoIort3c);
                    if (ACPI_FAILURE (Status))
                    {
                        return;
                    }

                    NodeOffset += 8;
                }
            }
            break;

        default:

            break;
        }

        /* Dump the ID mappings */

        NodeOffset = IortNode->MappingOffset;
        for (i = 0; i < IortNode->MappingCount; i++)
        {
            AcpiOsPrintf ("\n");
            Length = sizeof (ACPI_IORT_ID_MAPPING);
            Status = AcpiDmDumpTable (Table->Length, Offset + NodeOffset,
                ACPI_ADD_PTR (ACPI_IORT_NODE, IortNode, NodeOffset),
                Length, AcpiDmTableInfoIortMap);
            if (ACPI_FAILURE (Status))
            {
                return;
            }

            NodeOffset += Length;
        }

NextSubtable:
        /* Point to next node subtable */

        Offset += IortNode->Length;
        IortNode = ACPI_ADD_PTR (ACPI_IORT_NODE, IortNode, IortNode->Length);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpIvrs
 *
 * PARAMETERS:  Table               - A IVRS table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a IVRS
 *
 ******************************************************************************/

static UINT8 EntrySizes[] = {4,8,16,32};

void
AcpiDmDumpIvrs (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_IVRS);
    UINT32                  EntryOffset;
    UINT32                  EntryLength;
    UINT32                  EntryType;
    ACPI_IVRS_DE_HEADER     *DeviceEntry;
    ACPI_IVRS_HEADER        *Subtable;
    ACPI_DMTABLE_INFO       *InfoTable;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoIvrs);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_IVRS_HEADER, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, AcpiDmTableInfoIvrsHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        switch (Subtable->Type)
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

            AcpiOsPrintf ("\n**** Unknown IVRS subtable type 0x%X\n",
                Subtable->Type);

            /* Attempt to continue */

            if (!Subtable->Length)
            {
                AcpiOsPrintf ("Invalid zero length subtable\n");
                return;
            }
            goto NextSubtable;
        }

        /* Dump the subtable */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* The hardware subtable can contain multiple device entries */

        if (Subtable->Type == ACPI_IVRS_TYPE_HARDWARE)
        {
            EntryOffset = Offset + sizeof (ACPI_IVRS_HARDWARE);
            DeviceEntry = ACPI_ADD_PTR (ACPI_IVRS_DE_HEADER, Subtable,
                sizeof (ACPI_IVRS_HARDWARE));

            while (EntryOffset < (Offset + Subtable->Length))
            {
                AcpiOsPrintf ("\n");
                /*
                 * Upper 2 bits of Type encode the length of the device entry
                 *
                 * 00 = 4 byte
                 * 01 = 8 byte
                 * 10 = 16 byte - currently no entries defined
                 * 11 = 32 byte - currently no entries defined
                 */
                EntryType = DeviceEntry->Type;
                EntryLength = EntrySizes [EntryType >> 6];

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
                    InfoTable = AcpiDmTableInfoIvrs4;
                    AcpiOsPrintf (
                        "\n**** Unknown IVRS device entry type/length: "
                        "0x%.2X/0x%X at offset 0x%.4X: (header below)\n",
                        EntryType, EntryLength, EntryOffset);
                    break;
                }

                /* Dump the Device Entry */

                Status = AcpiDmDumpTable (Table->Length, EntryOffset,
                    DeviceEntry, EntryLength, InfoTable);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }

                EntryOffset += EntryLength;
                DeviceEntry = ACPI_ADD_PTR (ACPI_IVRS_DE_HEADER, DeviceEntry,
                    EntryLength);
            }
        }

NextSubtable:
        /* Point to next subtable */

        Offset += Subtable->Length;
        Subtable = ACPI_ADD_PTR (ACPI_IVRS_HEADER, Subtable, Subtable->Length);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpLpit
 *
 * PARAMETERS:  Table               - A LPIT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a LPIT. This table type consists
 *              of an open-ended number of subtables. Note: There are no
 *              entries in the main table. An LPIT consists of the table
 *              header and then subtables only.
 *
 ******************************************************************************/

void
AcpiDmDumpLpit (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    ACPI_LPIT_HEADER        *Subtable;
    UINT32                  Length = Table->Length;
    UINT32                  Offset = sizeof (ACPI_TABLE_LPIT);
    ACPI_DMTABLE_INFO       *InfoTable;
    UINT32                  SubtableLength;


    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_LPIT_HEADER, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        Status = AcpiDmDumpTable (Length, Offset, Subtable,
            sizeof (ACPI_LPIT_HEADER), AcpiDmTableInfoLpitHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        switch (Subtable->Type)
        {
        case ACPI_LPIT_TYPE_NATIVE_CSTATE:

            InfoTable = AcpiDmTableInfoLpit0;
            SubtableLength = sizeof (ACPI_LPIT_NATIVE);
            break;

        default:

            /* Cannot continue on unknown type - no length */

            AcpiOsPrintf ("\n**** Unknown LPIT subtable type 0x%X\n",
                Subtable->Type);
            return;
        }

        Status = AcpiDmDumpTable (Length, Offset, Subtable,
            SubtableLength, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        AcpiOsPrintf ("\n");

        /* Point to next subtable */

        Offset += SubtableLength;
        Subtable = ACPI_ADD_PTR (ACPI_LPIT_HEADER, Subtable, SubtableLength);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpMadt
 *
 * PARAMETERS:  Table               - A MADT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a MADT. This table type consists
 *              of an open-ended number of subtables.
 *
 ******************************************************************************/

void
AcpiDmDumpMadt (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    ACPI_SUBTABLE_HEADER    *Subtable;
    UINT32                  Length = Table->Length;
    UINT32                  Offset = sizeof (ACPI_TABLE_MADT);
    ACPI_DMTABLE_INFO       *InfoTable;


    /* Main table */

    Status = AcpiDmDumpTable (Length, 0, Table, 0, AcpiDmTableInfoMadt);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_SUBTABLE_HEADER, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Length, Offset, Subtable,
            Subtable->Length, AcpiDmTableInfoMadtHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        switch (Subtable->Type)
        {
        case ACPI_MADT_TYPE_LOCAL_APIC:

            InfoTable = AcpiDmTableInfoMadt0;
            break;

        case ACPI_MADT_TYPE_IO_APIC:

            InfoTable = AcpiDmTableInfoMadt1;
            break;

        case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:

            InfoTable = AcpiDmTableInfoMadt2;
            break;

        case ACPI_MADT_TYPE_NMI_SOURCE:

            InfoTable = AcpiDmTableInfoMadt3;
            break;

        case ACPI_MADT_TYPE_LOCAL_APIC_NMI:

            InfoTable = AcpiDmTableInfoMadt4;
            break;

        case ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE:

            InfoTable = AcpiDmTableInfoMadt5;
            break;

        case ACPI_MADT_TYPE_IO_SAPIC:

            InfoTable = AcpiDmTableInfoMadt6;
            break;

        case ACPI_MADT_TYPE_LOCAL_SAPIC:

            InfoTable = AcpiDmTableInfoMadt7;
            break;

        case ACPI_MADT_TYPE_INTERRUPT_SOURCE:

            InfoTable = AcpiDmTableInfoMadt8;
            break;

        case ACPI_MADT_TYPE_LOCAL_X2APIC:

            InfoTable = AcpiDmTableInfoMadt9;
            break;

        case ACPI_MADT_TYPE_LOCAL_X2APIC_NMI:

            InfoTable = AcpiDmTableInfoMadt10;
            break;

        case ACPI_MADT_TYPE_GENERIC_INTERRUPT:

            InfoTable = AcpiDmTableInfoMadt11;
            break;

        case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:

            InfoTable = AcpiDmTableInfoMadt12;
            break;

        case ACPI_MADT_TYPE_GENERIC_MSI_FRAME:

            InfoTable = AcpiDmTableInfoMadt13;
            break;

        case ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR:

            InfoTable = AcpiDmTableInfoMadt14;
            break;

        case ACPI_MADT_TYPE_GENERIC_TRANSLATOR:

            InfoTable = AcpiDmTableInfoMadt15;
            break;

        default:

            AcpiOsPrintf ("\n**** Unknown MADT subtable type 0x%X\n\n",
                Subtable->Type);

            /* Attempt to continue */

            if (!Subtable->Length)
            {
                AcpiOsPrintf ("Invalid zero length subtable\n");
                return;
            }

            goto NextSubtable;
        }

        Status = AcpiDmDumpTable (Length, Offset, Subtable,
            Subtable->Length, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

NextSubtable:
        /* Point to next subtable */

        Offset += Subtable->Length;
        Subtable = ACPI_ADD_PTR (ACPI_SUBTABLE_HEADER, Subtable,
            Subtable->Length);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpMcfg
 *
 * PARAMETERS:  Table               - A MCFG Table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a MCFG table
 *
 ******************************************************************************/

void
AcpiDmDumpMcfg (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_MCFG);
    ACPI_MCFG_ALLOCATION    *Subtable;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoMcfg);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_MCFG_ALLOCATION, Table, Offset);
    while (Offset < Table->Length)
    {
        if (Offset + sizeof (ACPI_MCFG_ALLOCATION) > Table->Length)
        {
            AcpiOsPrintf ("Warning: there are %u invalid trailing bytes\n",
                sizeof (ACPI_MCFG_ALLOCATION) - (Offset - Table->Length));
            return;
        }

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            sizeof (ACPI_MCFG_ALLOCATION), AcpiDmTableInfoMcfg0);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Point to next subtable (each subtable is of fixed length) */

        Offset += sizeof (ACPI_MCFG_ALLOCATION);
        Subtable = ACPI_ADD_PTR (ACPI_MCFG_ALLOCATION, Subtable,
            sizeof (ACPI_MCFG_ALLOCATION));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpMpst
 *
 * PARAMETERS:  Table               - A MPST Table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a MPST table
 *
 ******************************************************************************/

void
AcpiDmDumpMpst (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_MPST);
    ACPI_MPST_POWER_NODE    *Subtable0;
    ACPI_MPST_POWER_STATE   *Subtable0A;
    ACPI_MPST_COMPONENT     *Subtable0B;
    ACPI_MPST_DATA_HDR      *Subtable1;
    ACPI_MPST_POWER_DATA    *Subtable2;
    UINT16                  SubtableCount;
    UINT32                  PowerStateCount;
    UINT32                  ComponentCount;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoMpst);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtable: Memory Power Node(s) */

    SubtableCount = (ACPI_CAST_PTR (ACPI_TABLE_MPST, Table))->PowerNodeCount;
    Subtable0 = ACPI_ADD_PTR (ACPI_MPST_POWER_NODE, Table, Offset);

    while ((Offset < Table->Length) && SubtableCount)
    {
        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable0,
            sizeof (ACPI_MPST_POWER_NODE), AcpiDmTableInfoMpst0);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Extract the sub-subtable counts */

        PowerStateCount = Subtable0->NumPowerStates;
        ComponentCount = Subtable0->NumPhysicalComponents;
        Offset += sizeof (ACPI_MPST_POWER_NODE);

        /* Sub-subtables - Memory Power State Structure(s) */

        Subtable0A = ACPI_ADD_PTR (ACPI_MPST_POWER_STATE, Subtable0,
            sizeof (ACPI_MPST_POWER_NODE));

        while (PowerStateCount)
        {
            AcpiOsPrintf ("\n");
            Status = AcpiDmDumpTable (Table->Length, Offset, Subtable0A,
                sizeof (ACPI_MPST_POWER_STATE), AcpiDmTableInfoMpst0A);
            if (ACPI_FAILURE (Status))
            {
                return;
            }

            Subtable0A++;
            PowerStateCount--;
            Offset += sizeof (ACPI_MPST_POWER_STATE);
       }

        /* Sub-subtables - Physical Component ID Structure(s) */

        Subtable0B = ACPI_CAST_PTR (ACPI_MPST_COMPONENT, Subtable0A);

        if (ComponentCount)
        {
            AcpiOsPrintf ("\n");
        }

        while (ComponentCount)
        {
            Status = AcpiDmDumpTable (Table->Length, Offset, Subtable0B,
                sizeof (ACPI_MPST_COMPONENT), AcpiDmTableInfoMpst0B);
            if (ACPI_FAILURE (Status))
            {
                return;
            }

            Subtable0B++;
            ComponentCount--;
            Offset += sizeof (ACPI_MPST_COMPONENT);
        }

        /* Point to next Memory Power Node subtable */

        SubtableCount--;
        Subtable0 = ACPI_ADD_PTR (ACPI_MPST_POWER_NODE, Subtable0,
            sizeof (ACPI_MPST_POWER_NODE) +
            (sizeof (ACPI_MPST_POWER_STATE) * Subtable0->NumPowerStates) +
            (sizeof (ACPI_MPST_COMPONENT) * Subtable0->NumPhysicalComponents));
    }

    /* Subtable: Count of Memory Power State Characteristic structures */

    AcpiOsPrintf ("\n");
    Subtable1 = ACPI_CAST_PTR (ACPI_MPST_DATA_HDR, Subtable0);
    Status = AcpiDmDumpTable (Table->Length, Offset, Subtable1,
        sizeof (ACPI_MPST_DATA_HDR), AcpiDmTableInfoMpst1);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    SubtableCount = Subtable1->CharacteristicsCount;
    Offset += sizeof (ACPI_MPST_DATA_HDR);

    /* Subtable: Memory Power State Characteristics structure(s) */

    Subtable2 = ACPI_ADD_PTR (ACPI_MPST_POWER_DATA, Subtable1,
        sizeof (ACPI_MPST_DATA_HDR));

    while ((Offset < Table->Length) && SubtableCount)
    {
        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable2,
            sizeof (ACPI_MPST_POWER_DATA), AcpiDmTableInfoMpst2);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        Subtable2++;
        SubtableCount--;
        Offset += sizeof (ACPI_MPST_POWER_DATA);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpMsct
 *
 * PARAMETERS:  Table               - A MSCT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a MSCT
 *
 ******************************************************************************/

void
AcpiDmDumpMsct (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_MSCT);
    ACPI_MSCT_PROXIMITY     *Subtable;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoMsct);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_MSCT_PROXIMITY, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            sizeof (ACPI_MSCT_PROXIMITY), AcpiDmTableInfoMsct0);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Point to next subtable */

        Offset += sizeof (ACPI_MSCT_PROXIMITY);
        Subtable = ACPI_ADD_PTR (ACPI_MSCT_PROXIMITY, Subtable,
            sizeof (ACPI_MSCT_PROXIMITY));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpMtmr
 *
 * PARAMETERS:  Table               - A MTMR table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a MTMR
 *
 ******************************************************************************/

void
AcpiDmDumpMtmr (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_MTMR);
    ACPI_MTMR_ENTRY         *Subtable;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoMtmr);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_MTMR_ENTRY, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            sizeof (ACPI_MTMR_ENTRY), AcpiDmTableInfoMtmr0);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Point to next subtable */

        Offset += sizeof (ACPI_MTMR_ENTRY);
        Subtable = ACPI_ADD_PTR (ACPI_MTMR_ENTRY, Subtable,
            sizeof (ACPI_MTMR_ENTRY));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpNfit
 *
 * PARAMETERS:  Table               - A NFIT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of an NFIT.
 *
 ******************************************************************************/

void
AcpiDmDumpNfit (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_NFIT);
    UINT32                  FieldOffset = 0;
    UINT32                  Length;
    ACPI_NFIT_HEADER        *Subtable;
    ACPI_DMTABLE_INFO       *InfoTable;
    ACPI_NFIT_INTERLEAVE    *Interleave = NULL;
    ACPI_NFIT_SMBIOS        *SmbiosInfo = NULL;
    ACPI_NFIT_FLUSH_ADDRESS *Hint = NULL;
    UINT32                  i;


    /* Main table */

    Status = AcpiDmDumpTable (Table->Length, 0, Table, 0, AcpiDmTableInfoNfit);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_NFIT_HEADER, Table, Offset);
    while (Offset < Table->Length)
    {
        /* NFIT subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, AcpiDmTableInfoNfitHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        switch (Subtable->Type)
        {
        case ACPI_NFIT_TYPE_SYSTEM_ADDRESS:

            InfoTable = AcpiDmTableInfoNfit0;
            break;

        case ACPI_NFIT_TYPE_MEMORY_MAP:

            InfoTable = AcpiDmTableInfoNfit1;
            break;

        case ACPI_NFIT_TYPE_INTERLEAVE:

            /* Has a variable number of 32-bit values at the end */

            InfoTable = AcpiDmTableInfoNfit2;
            Interleave = ACPI_CAST_PTR (ACPI_NFIT_INTERLEAVE, Subtable);
            FieldOffset = sizeof (ACPI_NFIT_INTERLEAVE);
            break;

        case ACPI_NFIT_TYPE_SMBIOS:

            SmbiosInfo = ACPI_CAST_PTR (ACPI_NFIT_SMBIOS, Subtable);
            InfoTable = AcpiDmTableInfoNfit3;
            break;

        case ACPI_NFIT_TYPE_CONTROL_REGION:

            InfoTable = AcpiDmTableInfoNfit4;
            break;

        case ACPI_NFIT_TYPE_DATA_REGION:

            InfoTable = AcpiDmTableInfoNfit5;
            break;

        case ACPI_NFIT_TYPE_FLUSH_ADDRESS:

            /* Has a variable number of 64-bit addresses at the end */

            InfoTable = AcpiDmTableInfoNfit6;
            Hint = ACPI_CAST_PTR (ACPI_NFIT_FLUSH_ADDRESS, Subtable);
            FieldOffset = sizeof (ACPI_NFIT_FLUSH_ADDRESS) - sizeof (UINT64);
            break;

        case ACPI_NFIT_TYPE_CAPABILITIES:    /* ACPI 6.0A */

            InfoTable = AcpiDmTableInfoNfit7;
            break;

        default:
            AcpiOsPrintf ("\n**** Unknown NFIT subtable type 0x%X\n",
                Subtable->Type);

            /* Attempt to continue */

            if (!Subtable->Length)
            {
                AcpiOsPrintf ("Invalid zero length subtable\n");
                return;
            }
            goto NextSubtable;
        }

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Per-subtable variable-length fields */

        switch (Subtable->Type)
        {
        case ACPI_NFIT_TYPE_INTERLEAVE:

            for (i = 0; i < Interleave->LineCount; i++)
            {
                Status = AcpiDmDumpTable (Table->Length, Offset + FieldOffset,
                    &Interleave->LineOffset[i],
                    sizeof (UINT32), AcpiDmTableInfoNfit2a);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }

                FieldOffset += sizeof (UINT32);
            }
            break;

        case ACPI_NFIT_TYPE_SMBIOS:

            Length = Subtable->Length -
                sizeof (ACPI_NFIT_SMBIOS) + sizeof (UINT8);

            if (Length)
            {
                Status = AcpiDmDumpTable (Table->Length,
                    sizeof (ACPI_NFIT_SMBIOS) - sizeof (UINT8),
                    SmbiosInfo,
                    Length, AcpiDmTableInfoNfit3a);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }
            }

            break;

        case ACPI_NFIT_TYPE_FLUSH_ADDRESS:

            for (i = 0; i < Hint->HintCount; i++)
            {
                Status = AcpiDmDumpTable (Table->Length, Offset + FieldOffset,
                    &Hint->HintAddress[i],
                    sizeof (UINT64), AcpiDmTableInfoNfit6a);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }

                FieldOffset += sizeof (UINT64);
            }
            break;

        default:
            break;
        }

NextSubtable:
        /* Point to next subtable */

        Offset += Subtable->Length;
        Subtable = ACPI_ADD_PTR (ACPI_NFIT_HEADER, Subtable, Subtable->Length);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpPcct
 *
 * PARAMETERS:  Table               - A PCCT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a PCCT. This table type consists
 *              of an open-ended number of subtables.
 *
 ******************************************************************************/

void
AcpiDmDumpPcct (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    ACPI_PCCT_SUBSPACE      *Subtable;
    ACPI_DMTABLE_INFO       *InfoTable;
    UINT32                  Length = Table->Length;
    UINT32                  Offset = sizeof (ACPI_TABLE_PCCT);


    /* Main table */

    Status = AcpiDmDumpTable (Length, 0, Table, 0, AcpiDmTableInfoPcct);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_PCCT_SUBSPACE, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Length, Offset, Subtable,
            Subtable->Header.Length, AcpiDmTableInfoPcctHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        switch (Subtable->Header.Type)
        {
        case ACPI_PCCT_TYPE_GENERIC_SUBSPACE:

            InfoTable = AcpiDmTableInfoPcct0;
            break;

        case ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE:

            InfoTable = AcpiDmTableInfoPcct1;
            break;

        case ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE_TYPE2:

            InfoTable = AcpiDmTableInfoPcct2;
            break;

        case ACPI_PCCT_TYPE_EXT_PCC_MASTER_SUBSPACE:

            InfoTable = AcpiDmTableInfoPcct3;
            break;

        case ACPI_PCCT_TYPE_EXT_PCC_SLAVE_SUBSPACE:

            InfoTable = AcpiDmTableInfoPcct4;
            break;

        default:

            AcpiOsPrintf (
                "\n**** Unexpected or unknown PCCT subtable type 0x%X\n\n",
                Subtable->Header.Type);
            return;
        }

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Length, Offset, Subtable,
            Subtable->Header.Length, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Point to next subtable */

        Offset += Subtable->Header.Length;
        Subtable = ACPI_ADD_PTR (ACPI_PCCT_SUBSPACE, Subtable,
            Subtable->Header.Length);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpPdtt
 *
 * PARAMETERS:  Table               - A PDTT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a Pdtt. This is a variable-length
 *              table that contains an open-ended number of IDs
 *              at the end of the table.
 *
 ******************************************************************************/

void
AcpiDmDumpPdtt (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    ACPI_PDTT_CHANNEL       *Subtable;
    UINT32                  Length = Table->Length;
    UINT32                  Offset = sizeof (ACPI_TABLE_PDTT);


    /* Main table */

    Status = AcpiDmDumpTable (Length, 0, Table, 0, AcpiDmTableInfoPdtt);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables. Currently there is only one type, but can be multiples */

    Subtable = ACPI_ADD_PTR (ACPI_PDTT_CHANNEL, Table, Offset);
    while (Offset < Table->Length)
    {
        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Length, Offset, Subtable,
            sizeof (ACPI_PDTT_CHANNEL), AcpiDmTableInfoPdtt0);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Point to next subtable */

        Offset += sizeof (ACPI_PDTT_CHANNEL);
        Subtable = ACPI_ADD_PTR (ACPI_PDTT_CHANNEL, Subtable,
            sizeof (ACPI_PDTT_CHANNEL));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpPmtt
 *
 * PARAMETERS:  Table               - A PMTT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a PMTT. This table type consists
 *              of an open-ended number of subtables.
 *
 ******************************************************************************/

void
AcpiDmDumpPmtt (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    ACPI_PMTT_HEADER        *Subtable;
    ACPI_PMTT_HEADER        *MemSubtable;
    ACPI_PMTT_HEADER        *DimmSubtable;
    ACPI_PMTT_DOMAIN        *DomainArray;
    UINT32                  Length = Table->Length;
    UINT32                  Offset = sizeof (ACPI_TABLE_PMTT);
    UINT32                  MemOffset;
    UINT32                  DimmOffset;
    UINT32                  DomainOffset;
    UINT32                  DomainCount;


    /* Main table */

    Status = AcpiDmDumpTable (Length, 0, Table, 0, AcpiDmTableInfoPmtt);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_PMTT_HEADER, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Length, Offset, Subtable,
            Subtable->Length, AcpiDmTableInfoPmttHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Only Socket subtables are expected at this level */

        if (Subtable->Type != ACPI_PMTT_TYPE_SOCKET)
        {
            AcpiOsPrintf (
                "\n**** Unexpected or unknown PMTT subtable type 0x%X\n\n",
                Subtable->Type);
            return;
        }

        /* Dump the fixed-length portion of the subtable */

        Status = AcpiDmDumpTable (Length, Offset, Subtable,
            Subtable->Length, AcpiDmTableInfoPmtt0);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        /* Walk the memory controller subtables */

        MemOffset = sizeof (ACPI_PMTT_SOCKET);
        MemSubtable = ACPI_ADD_PTR (ACPI_PMTT_HEADER, Subtable,
            sizeof (ACPI_PMTT_SOCKET));

        while (((Offset + MemOffset) < Table->Length) &&
            (MemOffset < Subtable->Length))
        {
            /* Common subtable header */

            AcpiOsPrintf ("\n");
            Status = AcpiDmDumpTable (Length,
                Offset + MemOffset, MemSubtable,
                MemSubtable->Length, AcpiDmTableInfoPmttHdr);
            if (ACPI_FAILURE (Status))
            {
                return;
            }

            /* Only memory controller subtables are expected at this level */

            if (MemSubtable->Type != ACPI_PMTT_TYPE_CONTROLLER)
            {
                AcpiOsPrintf (
                    "\n**** Unexpected or unknown PMTT subtable type 0x%X\n\n",
                    MemSubtable->Type);
                return;
            }

            /* Dump the fixed-length portion of the controller subtable */

            Status = AcpiDmDumpTable (Length,
                Offset + MemOffset, MemSubtable,
                MemSubtable->Length, AcpiDmTableInfoPmtt1);
            if (ACPI_FAILURE (Status))
            {
                return;
            }

            /* Walk the variable count of proximity domains */

            DomainCount = ((ACPI_PMTT_CONTROLLER *) MemSubtable)->DomainCount;
            DomainOffset = sizeof (ACPI_PMTT_CONTROLLER);
            DomainArray = ACPI_ADD_PTR (ACPI_PMTT_DOMAIN, MemSubtable,
                sizeof (ACPI_PMTT_CONTROLLER));

            while (((Offset + MemOffset + DomainOffset) < Table->Length) &&
                ((MemOffset + DomainOffset) < Subtable->Length) &&
                DomainCount)
            {
                Status = AcpiDmDumpTable (Length,
                    Offset + MemOffset + DomainOffset, DomainArray,
                    sizeof (ACPI_PMTT_DOMAIN), AcpiDmTableInfoPmtt1a);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }

                DomainOffset += sizeof (ACPI_PMTT_DOMAIN);
                DomainArray++;
                DomainCount--;
            }

            if (DomainCount)
            {
                AcpiOsPrintf (
                    "\n**** DomainCount exceeds subtable length\n\n");
            }

            /* Walk the physical component (DIMM) subtables */

            DimmOffset = DomainOffset;
            DimmSubtable = ACPI_ADD_PTR (ACPI_PMTT_HEADER, MemSubtable,
                DomainOffset);

            while (((Offset + MemOffset + DimmOffset) < Table->Length) &&
                (DimmOffset < MemSubtable->Length))
            {
                /* Common subtable header */

                AcpiOsPrintf ("\n");
                Status = AcpiDmDumpTable (Length,
                    Offset + MemOffset + DimmOffset, DimmSubtable,
                    DimmSubtable->Length, AcpiDmTableInfoPmttHdr);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }

                /* Only DIMM subtables are expected at this level */

                if (DimmSubtable->Type != ACPI_PMTT_TYPE_DIMM)
                {
                    AcpiOsPrintf (
                        "\n**** Unexpected or unknown PMTT subtable type 0x%X\n\n",
                        DimmSubtable->Type);
                    return;
                }

                /* Dump the fixed-length DIMM subtable */

                Status = AcpiDmDumpTable (Length,
                    Offset + MemOffset + DimmOffset, DimmSubtable,
                    DimmSubtable->Length, AcpiDmTableInfoPmtt2);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }

                /* Point to next DIMM subtable */

                DimmOffset += DimmSubtable->Length;
                DimmSubtable = ACPI_ADD_PTR (ACPI_PMTT_HEADER,
                    DimmSubtable, DimmSubtable->Length);
            }

            /* Point to next Controller subtable */

            MemOffset += MemSubtable->Length;
            MemSubtable = ACPI_ADD_PTR (ACPI_PMTT_HEADER,
                MemSubtable, MemSubtable->Length);
        }

        /* Point to next Socket subtable */

        Offset += Subtable->Length;
        Subtable = ACPI_ADD_PTR (ACPI_PMTT_HEADER,
            Subtable, Subtable->Length);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpPptt
 *
 * PARAMETERS:  Table               - A PMTT table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a PPTT. This table type consists
 *              of an open-ended number of subtables.
 *
 ******************************************************************************/

void
AcpiDmDumpPptt (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    ACPI_SUBTABLE_HEADER    *Subtable;
    ACPI_PPTT_PROCESSOR     *PpttProcessor;
    UINT8                   Length;
    UINT8                   SubtableOffset;
    UINT32                  Offset = sizeof (ACPI_TABLE_FPDT);
    ACPI_DMTABLE_INFO       *InfoTable;
    UINT32                  i;


    /* There is no main table (other than the standard ACPI header) */

    /* Subtables */

    Offset = sizeof (ACPI_TABLE_HEADER);
    while (Offset < Table->Length)
    {
        AcpiOsPrintf ("\n");

        /* Common subtable header */

        Subtable = ACPI_ADD_PTR (ACPI_SUBTABLE_HEADER, Table, Offset);
        if (Subtable->Length < sizeof (ACPI_SUBTABLE_HEADER))
        {
            AcpiOsPrintf ("Invalid subtable length\n");
            return;
        }
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, AcpiDmTableInfoPpttHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        switch (Subtable->Type)
        {
        case ACPI_PPTT_TYPE_PROCESSOR:

            InfoTable = AcpiDmTableInfoPptt0;
            Length = sizeof (ACPI_PPTT_PROCESSOR);
            break;

        case ACPI_PPTT_TYPE_CACHE:

            InfoTable = AcpiDmTableInfoPptt1;
            Length = sizeof (ACPI_PPTT_CACHE);
            break;

        case ACPI_PPTT_TYPE_ID:

            InfoTable = AcpiDmTableInfoPptt2;
            Length = sizeof (ACPI_PPTT_ID);
            break;

        default:

            AcpiOsPrintf ("\n**** Unknown PPTT subtable type 0x%X\n\n",
                Subtable->Type);

            /* Attempt to continue */

            goto NextSubtable;
        }

        if (Subtable->Length < Length)
        {
            AcpiOsPrintf ("Invalid subtable length\n");
            return;
        }
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return;
        }
        SubtableOffset = Length;

        switch (Subtable->Type)
        {
        case ACPI_PPTT_TYPE_PROCESSOR:

            PpttProcessor = ACPI_CAST_PTR (ACPI_PPTT_PROCESSOR, Subtable);

            /* Dump SMBIOS handles */

            if ((UINT8)(Subtable->Length - SubtableOffset) <
                (UINT8)(PpttProcessor->NumberOfPrivResources * 4))
            {
                AcpiOsPrintf ("Invalid private resource number\n");
                return;
            }
            for (i = 0; i < PpttProcessor->NumberOfPrivResources; i++)
            {
                Status = AcpiDmDumpTable (Table->Length, Offset + SubtableOffset,
                    ACPI_ADD_PTR (ACPI_SUBTABLE_HEADER, Subtable, SubtableOffset),
                    4, AcpiDmTableInfoPptt0a);
                SubtableOffset += 4;
            }
            break;

        default:

            break;
        }

NextSubtable:
        /* Point to next subtable */

        Offset += Subtable->Length;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpS3pt
 *
 * PARAMETERS:  Table               - A S3PT table
 *
 * RETURN:      Length of the table
 *
 * DESCRIPTION: Format the contents of a S3PT
 *
 ******************************************************************************/

UINT32
AcpiDmDumpS3pt (
    ACPI_TABLE_HEADER       *Tables)
{
    ACPI_STATUS             Status;
    UINT32                  Offset = sizeof (ACPI_TABLE_S3PT);
    ACPI_FPDT_HEADER        *Subtable;
    ACPI_DMTABLE_INFO       *InfoTable;
    ACPI_TABLE_S3PT         *S3ptTable = ACPI_CAST_PTR (ACPI_TABLE_S3PT, Tables);


    /* Main table */

    Status = AcpiDmDumpTable (Offset, 0, S3ptTable, 0, AcpiDmTableInfoS3pt);
    if (ACPI_FAILURE (Status))
    {
        return 0;
    }

    Subtable = ACPI_ADD_PTR (ACPI_FPDT_HEADER, S3ptTable, Offset);
    while (Offset < S3ptTable->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (S3ptTable->Length, Offset, Subtable,
            Subtable->Length, AcpiDmTableInfoS3ptHdr);
        if (ACPI_FAILURE (Status))
        {
            return 0;
        }

        switch (Subtable->Type)
        {
        case ACPI_S3PT_TYPE_RESUME:

            InfoTable = AcpiDmTableInfoS3pt0;
            break;

        case ACPI_S3PT_TYPE_SUSPEND:

            InfoTable = AcpiDmTableInfoS3pt1;
            break;

        default:

            AcpiOsPrintf ("\n**** Unknown S3PT subtable type 0x%X\n",
                Subtable->Type);

            /* Attempt to continue */

            if (!Subtable->Length)
            {
                AcpiOsPrintf ("Invalid zero length subtable\n");
                return 0;
            }
            goto NextSubtable;
        }

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (S3ptTable->Length, Offset, Subtable,
            Subtable->Length, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return 0;
        }

NextSubtable:
        /* Point to next subtable */

        Offset += Subtable->Length;
        Subtable = ACPI_ADD_PTR (ACPI_FPDT_HEADER, Subtable, Subtable->Length);
    }

    return (S3ptTable->Length);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDumpSdev
 *
 * PARAMETERS:  Table               - A SDEV table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Format the contents of a SDEV. This is a variable-length
 *              table that contains variable strings and vendor data.
 *
 ******************************************************************************/

void
AcpiDmDumpSdev (
    ACPI_TABLE_HEADER       *Table)
{
    ACPI_STATUS             Status;
    ACPI_SDEV_HEADER        *Subtable;
    ACPI_SDEV_PCIE          *Pcie;
    ACPI_SDEV_NAMESPACE     *Namesp;
    ACPI_DMTABLE_INFO       *InfoTable;
    UINT32                  Length = Table->Length;
    UINT32                  Offset = sizeof (ACPI_TABLE_SDEV);
    UINT16                  PathOffset;
    UINT16                  PathLength;
    UINT16                  VendorDataOffset;
    UINT16                  VendorDataLength;


    /* Main table */

    Status = AcpiDmDumpTable (Length, 0, Table, 0, AcpiDmTableInfoSdev);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* Subtables */

    Subtable = ACPI_ADD_PTR (ACPI_SDEV_HEADER, Table, Offset);
    while (Offset < Table->Length)
    {
        /* Common subtable header */

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, AcpiDmTableInfoSdevHdr);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        switch (Subtable->Type)
        {
        case ACPI_SDEV_TYPE_NAMESPACE_DEVICE:

            InfoTable = AcpiDmTableInfoSdev0;
            break;

        case ACPI_SDEV_TYPE_PCIE_ENDPOINT_DEVICE:

            InfoTable = AcpiDmTableInfoSdev1;
            break;

        default:
            goto NextSubtable;
        }

        AcpiOsPrintf ("\n");
        Status = AcpiDmDumpTable (Table->Length, Offset, Subtable,
            Subtable->Length, InfoTable);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        switch (Subtable->Type)
        {
        case ACPI_SDEV_TYPE_NAMESPACE_DEVICE:

            /* Dump the PCIe device ID(s) */

            Namesp = ACPI_CAST_PTR (ACPI_SDEV_NAMESPACE, Subtable);
            PathOffset = Namesp->DeviceIdOffset;
            PathLength = Namesp->DeviceIdLength;

            if (PathLength)
            {
                Status = AcpiDmDumpTable (Table->Length, 0,
                    ACPI_ADD_PTR (UINT8, Namesp, PathOffset),
                    PathLength, AcpiDmTableInfoSdev0a);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }
            }

            /* Dump the vendor-specific data */

            VendorDataLength =
                Namesp->VendorDataLength;
            VendorDataOffset =
                Namesp->DeviceIdOffset + Namesp->DeviceIdLength;

            if (VendorDataLength)
            {
                Status = AcpiDmDumpTable (Table->Length, 0,
                    ACPI_ADD_PTR (UINT8, Namesp, VendorDataOffset),
                    VendorDataLength, AcpiDmTableInfoSdev1b);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }
            }
            break;

        case ACPI_SDEV_TYPE_PCIE_ENDPOINT_DEVICE:

            /* PCI path substructures */

            Pcie = ACPI_CAST_PTR (ACPI_SDEV_PCIE, Subtable);
            PathOffset = Pcie->PathOffset;
            PathLength = Pcie->PathLength;

            while (PathLength)
            {
                Status = AcpiDmDumpTable (Table->Length,
                    PathOffset + Offset,
                    ACPI_ADD_PTR (UINT8, Pcie, PathOffset),
                    sizeof (ACPI_SDEV_PCIE_PATH), AcpiDmTableInfoSdev1a);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }

                PathOffset += sizeof (ACPI_SDEV_PCIE_PATH);
                PathLength -= sizeof (ACPI_SDEV_PCIE_PATH);
            }

            /* VendorData */

            VendorDataLength = Pcie->VendorDataLength;
            VendorDataOffset = Pcie->PathOffset + Pcie->PathLength;

            if (VendorDataLength)
            {
                Status = AcpiDmDumpTable (Table->Length, 0,
                    ACPI_ADD_PTR (UINT8, Pcie, VendorDataOffset),
                    VendorDataLength, AcpiDmTableInfoSdev1b);
            }
            break;

        default:
            goto NextSubtable;
        }

NextSubtable:
        /* Point to next subtable */

        Offset += Subtable->Length;
        Subtable = ACPI_ADD_PTR (ACPI_SDEV_HEADER, Subtable,
            Subtable->Length);
    }
}
