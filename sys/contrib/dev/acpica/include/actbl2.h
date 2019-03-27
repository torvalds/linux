/******************************************************************************
 *
 * Name: actbl2.h - ACPI Table Definitions (tables not in ACPI spec)
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

#ifndef __ACTBL2_H__
#define __ACTBL2_H__


/*******************************************************************************
 *
 * Additional ACPI Tables (2)
 *
 * These tables are not consumed directly by the ACPICA subsystem, but are
 * included here to support device drivers and the AML disassembler.
 *
 ******************************************************************************/


/*
 * Values for description table header signatures for tables defined in this
 * file. Useful because they make it more difficult to inadvertently type in
 * the wrong signature.
 */
#define ACPI_SIG_IORT           "IORT"      /* IO Remapping Table */
#define ACPI_SIG_IVRS           "IVRS"      /* I/O Virtualization Reporting Structure */
#define ACPI_SIG_LPIT           "LPIT"      /* Low Power Idle Table */
#define ACPI_SIG_MADT           "APIC"      /* Multiple APIC Description Table */
#define ACPI_SIG_MCFG           "MCFG"      /* PCI Memory Mapped Configuration table */
#define ACPI_SIG_MCHI           "MCHI"      /* Management Controller Host Interface table */
#define ACPI_SIG_MPST           "MPST"      /* Memory Power State Table */
#define ACPI_SIG_MSCT           "MSCT"      /* Maximum System Characteristics Table */
#define ACPI_SIG_MSDM           "MSDM"      /* Microsoft Data Management Table */
#define ACPI_SIG_MTMR           "MTMR"      /* MID Timer table */
#define ACPI_SIG_NFIT           "NFIT"      /* NVDIMM Firmware Interface Table */
#define ACPI_SIG_PCCT           "PCCT"      /* Platform Communications Channel Table */
#define ACPI_SIG_PDTT           "PDTT"      /* Platform Debug Trigger Table */
#define ACPI_SIG_PMTT           "PMTT"      /* Platform Memory Topology Table */
#define ACPI_SIG_PPTT           "PPTT"      /* Processor Properties Topology Table */
#define ACPI_SIG_RASF           "RASF"      /* RAS Feature table */
#define ACPI_SIG_SBST           "SBST"      /* Smart Battery Specification Table */
#define ACPI_SIG_SDEI           "SDEI"      /* Software Delegated Exception Interface Table */
#define ACPI_SIG_SDEV           "SDEV"      /* Secure Devices table */


/*
 * All tables must be byte-packed to match the ACPI specification, since
 * the tables are provided by the system BIOS.
 */
#pragma pack(1)

/*
 * Note: C bitfields are not used for this reason:
 *
 * "Bitfields are great and easy to read, but unfortunately the C language
 * does not specify the layout of bitfields in memory, which means they are
 * essentially useless for dealing with packed data in on-disk formats or
 * binary wire protocols." (Or ACPI tables and buffers.) "If you ask me,
 * this decision was a design error in C. Ritchie could have picked an order
 * and stuck with it." Norman Ramsey.
 * See http://stackoverflow.com/a/1053662/41661
 */


/*******************************************************************************
 *
 * IORT - IO Remapping Table
 *
 * Conforms to "IO Remapping Table System Software on ARM Platforms",
 * Document number: ARM DEN 0049D, March 2018
 *
 ******************************************************************************/

typedef struct acpi_table_iort
{
    ACPI_TABLE_HEADER       Header;
    UINT32                  NodeCount;
    UINT32                  NodeOffset;
    UINT32                  Reserved;

} ACPI_TABLE_IORT;


/*
 * IORT subtables
 */
typedef struct acpi_iort_node
{
    UINT8                   Type;
    UINT16                  Length;
    UINT8                   Revision;
    UINT32                  Reserved;
    UINT32                  MappingCount;
    UINT32                  MappingOffset;
    char                    NodeData[1];

} ACPI_IORT_NODE;

/* Values for subtable Type above */

enum AcpiIortNodeType
{
    ACPI_IORT_NODE_ITS_GROUP            = 0x00,
    ACPI_IORT_NODE_NAMED_COMPONENT      = 0x01,
    ACPI_IORT_NODE_PCI_ROOT_COMPLEX     = 0x02,
    ACPI_IORT_NODE_SMMU                 = 0x03,
    ACPI_IORT_NODE_SMMU_V3              = 0x04,
    ACPI_IORT_NODE_PMCG                 = 0x05
};


typedef struct acpi_iort_id_mapping
{
    UINT32                  InputBase;          /* Lowest value in input range */
    UINT32                  IdCount;            /* Number of IDs */
    UINT32                  OutputBase;         /* Lowest value in output range */
    UINT32                  OutputReference;    /* A reference to the output node */
    UINT32                  Flags;

} ACPI_IORT_ID_MAPPING;

/* Masks for Flags field above for IORT subtable */

#define ACPI_IORT_ID_SINGLE_MAPPING (1)


typedef struct acpi_iort_memory_access
{
    UINT32                  CacheCoherency;
    UINT8                   Hints;
    UINT16                  Reserved;
    UINT8                   MemoryFlags;

} ACPI_IORT_MEMORY_ACCESS;

/* Values for CacheCoherency field above */

#define ACPI_IORT_NODE_COHERENT         0x00000001  /* The device node is fully coherent */
#define ACPI_IORT_NODE_NOT_COHERENT     0x00000000  /* The device node is not coherent */

/* Masks for Hints field above */

#define ACPI_IORT_HT_TRANSIENT          (1)
#define ACPI_IORT_HT_WRITE              (1<<1)
#define ACPI_IORT_HT_READ               (1<<2)
#define ACPI_IORT_HT_OVERRIDE           (1<<3)

/* Masks for MemoryFlags field above */

#define ACPI_IORT_MF_COHERENCY          (1)
#define ACPI_IORT_MF_ATTRIBUTES         (1<<1)


/*
 * IORT node specific subtables
 */
typedef struct acpi_iort_its_group
{
    UINT32                  ItsCount;
    UINT32                  Identifiers[1];         /* GIC ITS identifier array */

} ACPI_IORT_ITS_GROUP;


typedef struct acpi_iort_named_component
{
    UINT32                  NodeFlags;
    UINT64                  MemoryProperties;       /* Memory access properties */
    UINT8                   MemoryAddressLimit;     /* Memory address size limit */
    char                    DeviceName[1];          /* Path of namespace object */

} ACPI_IORT_NAMED_COMPONENT;

/* Masks for Flags field above */

#define ACPI_IORT_NC_STALL_SUPPORTED    (1)
#define ACPI_IORT_NC_PASID_BITS         (31<<1)

typedef struct acpi_iort_root_complex
{
    UINT64                  MemoryProperties;       /* Memory access properties */
    UINT32                  AtsAttribute;
    UINT32                  PciSegmentNumber;
    UINT8                   MemoryAddressLimit;     /* Memory address size limit */
    UINT8                   Reserved[3];            /* Reserved, must be zero */

} ACPI_IORT_ROOT_COMPLEX;

/* Values for AtsAttribute field above */

#define ACPI_IORT_ATS_SUPPORTED         0x00000001  /* The root complex supports ATS */
#define ACPI_IORT_ATS_UNSUPPORTED       0x00000000  /* The root complex doesn't support ATS */


typedef struct acpi_iort_smmu
{
    UINT64                  BaseAddress;            /* SMMU base address */
    UINT64                  Span;                   /* Length of memory range */
    UINT32                  Model;
    UINT32                  Flags;
    UINT32                  GlobalInterruptOffset;
    UINT32                  ContextInterruptCount;
    UINT32                  ContextInterruptOffset;
    UINT32                  PmuInterruptCount;
    UINT32                  PmuInterruptOffset;
    UINT64                  Interrupts[1];          /* Interrupt array */

} ACPI_IORT_SMMU;

/* Values for Model field above */

#define ACPI_IORT_SMMU_V1               0x00000000  /* Generic SMMUv1 */
#define ACPI_IORT_SMMU_V2               0x00000001  /* Generic SMMUv2 */
#define ACPI_IORT_SMMU_CORELINK_MMU400  0x00000002  /* ARM Corelink MMU-400 */
#define ACPI_IORT_SMMU_CORELINK_MMU500  0x00000003  /* ARM Corelink MMU-500 */
#define ACPI_IORT_SMMU_CORELINK_MMU401  0x00000004  /* ARM Corelink MMU-401 */
#define ACPI_IORT_SMMU_CAVIUM_THUNDERX  0x00000005  /* Cavium ThunderX SMMUv2 */

/* Masks for Flags field above */

#define ACPI_IORT_SMMU_DVM_SUPPORTED    (1)
#define ACPI_IORT_SMMU_COHERENT_WALK    (1<<1)

/* Global interrupt format */

typedef struct acpi_iort_smmu_gsi
{
    UINT32                  NSgIrpt;
    UINT32                  NSgIrptFlags;
    UINT32                  NSgCfgIrpt;
    UINT32                  NSgCfgIrptFlags;

} ACPI_IORT_SMMU_GSI;


typedef struct acpi_iort_smmu_v3
{
    UINT64                  BaseAddress;            /* SMMUv3 base address */
    UINT32                  Flags;
    UINT32                  Reserved;
    UINT64                  VatosAddress;
    UINT32                  Model;
    UINT32                  EventGsiv;
    UINT32                  PriGsiv;
    UINT32                  GerrGsiv;
    UINT32                  SyncGsiv;
    UINT32                  Pxm;
    UINT32                  IdMappingIndex;

} ACPI_IORT_SMMU_V3;

/* Values for Model field above */

#define ACPI_IORT_SMMU_V3_GENERIC           0x00000000  /* Generic SMMUv3 */
#define ACPI_IORT_SMMU_V3_HISILICON_HI161X  0x00000001  /* HiSilicon Hi161x SMMUv3 */
#define ACPI_IORT_SMMU_V3_CAVIUM_CN99XX     0x00000002  /* Cavium CN99xx SMMUv3 */

/* Masks for Flags field above */

#define ACPI_IORT_SMMU_V3_COHACC_OVERRIDE   (1)
#define ACPI_IORT_SMMU_V3_HTTU_OVERRIDE     (3<<1)
#define ACPI_IORT_SMMU_V3_PXM_VALID         (1<<3)

typedef struct acpi_iort_pmcg
{
    UINT64                  Page0BaseAddress;
    UINT32                  OverflowGsiv;
    UINT32                  NodeReference;
    UINT64                  Page1BaseAddress;

} ACPI_IORT_PMCG;


/*******************************************************************************
 *
 * IVRS - I/O Virtualization Reporting Structure
 *        Version 1
 *
 * Conforms to "AMD I/O Virtualization Technology (IOMMU) Specification",
 * Revision 1.26, February 2009.
 *
 ******************************************************************************/

typedef struct acpi_table_ivrs
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Info;               /* Common virtualization info */
    UINT64                  Reserved;

} ACPI_TABLE_IVRS;

/* Values for Info field above */

#define ACPI_IVRS_PHYSICAL_SIZE     0x00007F00  /* 7 bits, physical address size */
#define ACPI_IVRS_VIRTUAL_SIZE      0x003F8000  /* 7 bits, virtual address size */
#define ACPI_IVRS_ATS_RESERVED      0x00400000  /* ATS address translation range reserved */


/* IVRS subtable header */

typedef struct acpi_ivrs_header
{
    UINT8                   Type;               /* Subtable type */
    UINT8                   Flags;
    UINT16                  Length;             /* Subtable length */
    UINT16                  DeviceId;           /* ID of IOMMU */

} ACPI_IVRS_HEADER;

/* Values for subtable Type above */

enum AcpiIvrsType
{
    ACPI_IVRS_TYPE_HARDWARE         = 0x10,
    ACPI_IVRS_TYPE_MEMORY1          = 0x20,
    ACPI_IVRS_TYPE_MEMORY2          = 0x21,
    ACPI_IVRS_TYPE_MEMORY3          = 0x22
};

/* Masks for Flags field above for IVHD subtable */

#define ACPI_IVHD_TT_ENABLE         (1)
#define ACPI_IVHD_PASS_PW           (1<<1)
#define ACPI_IVHD_RES_PASS_PW       (1<<2)
#define ACPI_IVHD_ISOC              (1<<3)
#define ACPI_IVHD_IOTLB             (1<<4)

/* Masks for Flags field above for IVMD subtable */

#define ACPI_IVMD_UNITY             (1)
#define ACPI_IVMD_READ              (1<<1)
#define ACPI_IVMD_WRITE             (1<<2)
#define ACPI_IVMD_EXCLUSION_RANGE   (1<<3)


/*
 * IVRS subtables, correspond to Type in ACPI_IVRS_HEADER
 */

/* 0x10: I/O Virtualization Hardware Definition Block (IVHD) */

typedef struct acpi_ivrs_hardware
{
    ACPI_IVRS_HEADER        Header;
    UINT16                  CapabilityOffset;   /* Offset for IOMMU control fields */
    UINT64                  BaseAddress;        /* IOMMU control registers */
    UINT16                  PciSegmentGroup;
    UINT16                  Info;               /* MSI number and unit ID */
    UINT32                  Reserved;

} ACPI_IVRS_HARDWARE;

/* Masks for Info field above */

#define ACPI_IVHD_MSI_NUMBER_MASK   0x001F      /* 5 bits, MSI message number */
#define ACPI_IVHD_UNIT_ID_MASK      0x1F00      /* 5 bits, UnitID */


/*
 * Device Entries for IVHD subtable, appear after ACPI_IVRS_HARDWARE structure.
 * Upper two bits of the Type field are the (encoded) length of the structure.
 * Currently, only 4 and 8 byte entries are defined. 16 and 32 byte entries
 * are reserved for future use but not defined.
 */
typedef struct acpi_ivrs_de_header
{
    UINT8                   Type;
    UINT16                  Id;
    UINT8                   DataSetting;

} ACPI_IVRS_DE_HEADER;

/* Length of device entry is in the top two bits of Type field above */

#define ACPI_IVHD_ENTRY_LENGTH      0xC0

/* Values for device entry Type field above */

enum AcpiIvrsDeviceEntryType
{
    /* 4-byte device entries, all use ACPI_IVRS_DEVICE4 */

    ACPI_IVRS_TYPE_PAD4             = 0,
    ACPI_IVRS_TYPE_ALL              = 1,
    ACPI_IVRS_TYPE_SELECT           = 2,
    ACPI_IVRS_TYPE_START            = 3,
    ACPI_IVRS_TYPE_END              = 4,

    /* 8-byte device entries */

    ACPI_IVRS_TYPE_PAD8             = 64,
    ACPI_IVRS_TYPE_NOT_USED         = 65,
    ACPI_IVRS_TYPE_ALIAS_SELECT     = 66, /* Uses ACPI_IVRS_DEVICE8A */
    ACPI_IVRS_TYPE_ALIAS_START      = 67, /* Uses ACPI_IVRS_DEVICE8A */
    ACPI_IVRS_TYPE_EXT_SELECT       = 70, /* Uses ACPI_IVRS_DEVICE8B */
    ACPI_IVRS_TYPE_EXT_START        = 71, /* Uses ACPI_IVRS_DEVICE8B */
    ACPI_IVRS_TYPE_SPECIAL          = 72  /* Uses ACPI_IVRS_DEVICE8C */
};

/* Values for Data field above */

#define ACPI_IVHD_INIT_PASS         (1)
#define ACPI_IVHD_EINT_PASS         (1<<1)
#define ACPI_IVHD_NMI_PASS          (1<<2)
#define ACPI_IVHD_SYSTEM_MGMT       (3<<4)
#define ACPI_IVHD_LINT0_PASS        (1<<6)
#define ACPI_IVHD_LINT1_PASS        (1<<7)


/* Types 0-4: 4-byte device entry */

typedef struct acpi_ivrs_device4
{
    ACPI_IVRS_DE_HEADER     Header;

} ACPI_IVRS_DEVICE4;

/* Types 66-67: 8-byte device entry */

typedef struct acpi_ivrs_device8a
{
    ACPI_IVRS_DE_HEADER     Header;
    UINT8                   Reserved1;
    UINT16                  UsedId;
    UINT8                   Reserved2;

} ACPI_IVRS_DEVICE8A;

/* Types 70-71: 8-byte device entry */

typedef struct acpi_ivrs_device8b
{
    ACPI_IVRS_DE_HEADER     Header;
    UINT32                  ExtendedData;

} ACPI_IVRS_DEVICE8B;

/* Values for ExtendedData above */

#define ACPI_IVHD_ATS_DISABLED      (1<<31)

/* Type 72: 8-byte device entry */

typedef struct acpi_ivrs_device8c
{
    ACPI_IVRS_DE_HEADER     Header;
    UINT8                   Handle;
    UINT16                  UsedId;
    UINT8                   Variety;

} ACPI_IVRS_DEVICE8C;

/* Values for Variety field above */

#define ACPI_IVHD_IOAPIC            1
#define ACPI_IVHD_HPET              2


/* 0x20, 0x21, 0x22: I/O Virtualization Memory Definition Block (IVMD) */

typedef struct acpi_ivrs_memory
{
    ACPI_IVRS_HEADER        Header;
    UINT16                  AuxData;
    UINT64                  Reserved;
    UINT64                  StartAddress;
    UINT64                  MemoryLength;

} ACPI_IVRS_MEMORY;


/*******************************************************************************
 *
 * LPIT - Low Power Idle Table
 *
 * Conforms to "ACPI Low Power Idle Table (LPIT)" July 2014.
 *
 ******************************************************************************/

typedef struct acpi_table_lpit
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_LPIT;


/* LPIT subtable header */

typedef struct acpi_lpit_header
{
    UINT32                  Type;               /* Subtable type */
    UINT32                  Length;             /* Subtable length */
    UINT16                  UniqueId;
    UINT16                  Reserved;
    UINT32                  Flags;

} ACPI_LPIT_HEADER;

/* Values for subtable Type above */

enum AcpiLpitType
{
    ACPI_LPIT_TYPE_NATIVE_CSTATE    = 0x00,
    ACPI_LPIT_TYPE_RESERVED         = 0x01      /* 1 and above are reserved */
};

/* Masks for Flags field above  */

#define ACPI_LPIT_STATE_DISABLED    (1)
#define ACPI_LPIT_NO_COUNTER        (1<<1)

/*
 * LPIT subtables, correspond to Type in ACPI_LPIT_HEADER
 */

/* 0x00: Native C-state instruction based LPI structure */

typedef struct acpi_lpit_native
{
    ACPI_LPIT_HEADER        Header;
    ACPI_GENERIC_ADDRESS    EntryTrigger;
    UINT32                  Residency;
    UINT32                  Latency;
    ACPI_GENERIC_ADDRESS    ResidencyCounter;
    UINT64                  CounterFrequency;

} ACPI_LPIT_NATIVE;


/*******************************************************************************
 *
 * MADT - Multiple APIC Description Table
 *        Version 3
 *
 ******************************************************************************/

typedef struct acpi_table_madt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Address;            /* Physical address of local APIC */
    UINT32                  Flags;

} ACPI_TABLE_MADT;

/* Masks for Flags field above */

#define ACPI_MADT_PCAT_COMPAT       (1)         /* 00: System also has dual 8259s */

/* Values for PCATCompat flag */

#define ACPI_MADT_DUAL_PIC          1
#define ACPI_MADT_MULTIPLE_APIC     0


/* Values for MADT subtable type in ACPI_SUBTABLE_HEADER */

enum AcpiMadtType
{
    ACPI_MADT_TYPE_LOCAL_APIC               = 0,
    ACPI_MADT_TYPE_IO_APIC                  = 1,
    ACPI_MADT_TYPE_INTERRUPT_OVERRIDE       = 2,
    ACPI_MADT_TYPE_NMI_SOURCE               = 3,
    ACPI_MADT_TYPE_LOCAL_APIC_NMI           = 4,
    ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE      = 5,
    ACPI_MADT_TYPE_IO_SAPIC                 = 6,
    ACPI_MADT_TYPE_LOCAL_SAPIC              = 7,
    ACPI_MADT_TYPE_INTERRUPT_SOURCE         = 8,
    ACPI_MADT_TYPE_LOCAL_X2APIC             = 9,
    ACPI_MADT_TYPE_LOCAL_X2APIC_NMI         = 10,
    ACPI_MADT_TYPE_GENERIC_INTERRUPT        = 11,
    ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR      = 12,
    ACPI_MADT_TYPE_GENERIC_MSI_FRAME        = 13,
    ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR    = 14,
    ACPI_MADT_TYPE_GENERIC_TRANSLATOR       = 15,
    ACPI_MADT_TYPE_RESERVED                 = 16    /* 16 and greater are reserved */
};


/*
 * MADT Subtables, correspond to Type in ACPI_SUBTABLE_HEADER
 */

/* 0: Processor Local APIC */

typedef struct acpi_madt_local_apic
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   ProcessorId;        /* ACPI processor id */
    UINT8                   Id;                 /* Processor's local APIC id */
    UINT32                  LapicFlags;

} ACPI_MADT_LOCAL_APIC;


/* 1: IO APIC */

typedef struct acpi_madt_io_apic
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   Id;                 /* I/O APIC ID */
    UINT8                   Reserved;           /* Reserved - must be zero */
    UINT32                  Address;            /* APIC physical address */
    UINT32                  GlobalIrqBase;      /* Global system interrupt where INTI lines start */

} ACPI_MADT_IO_APIC;


/* 2: Interrupt Override */

typedef struct acpi_madt_interrupt_override
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   Bus;                /* 0 - ISA */
    UINT8                   SourceIrq;          /* Interrupt source (IRQ) */
    UINT32                  GlobalIrq;          /* Global system interrupt */
    UINT16                  IntiFlags;

} ACPI_MADT_INTERRUPT_OVERRIDE;


/* 3: NMI Source */

typedef struct acpi_madt_nmi_source
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  IntiFlags;
    UINT32                  GlobalIrq;          /* Global system interrupt */

} ACPI_MADT_NMI_SOURCE;


/* 4: Local APIC NMI */

typedef struct acpi_madt_local_apic_nmi
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   ProcessorId;        /* ACPI processor id */
    UINT16                  IntiFlags;
    UINT8                   Lint;               /* LINTn to which NMI is connected */

} ACPI_MADT_LOCAL_APIC_NMI;


/* 5: Address Override */

typedef struct acpi_madt_local_apic_override
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;           /* Reserved, must be zero */
    UINT64                  Address;            /* APIC physical address */

} ACPI_MADT_LOCAL_APIC_OVERRIDE;


/* 6: I/O Sapic */

typedef struct acpi_madt_io_sapic
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   Id;                 /* I/O SAPIC ID */
    UINT8                   Reserved;           /* Reserved, must be zero */
    UINT32                  GlobalIrqBase;      /* Global interrupt for SAPIC start */
    UINT64                  Address;            /* SAPIC physical address */

} ACPI_MADT_IO_SAPIC;


/* 7: Local Sapic */

typedef struct acpi_madt_local_sapic
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   ProcessorId;        /* ACPI processor id */
    UINT8                   Id;                 /* SAPIC ID */
    UINT8                   Eid;                /* SAPIC EID */
    UINT8                   Reserved[3];        /* Reserved, must be zero */
    UINT32                  LapicFlags;
    UINT32                  Uid;                /* Numeric UID - ACPI 3.0 */
    char                    UidString[1];       /* String UID  - ACPI 3.0 */

} ACPI_MADT_LOCAL_SAPIC;


/* 8: Platform Interrupt Source */

typedef struct acpi_madt_interrupt_source
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  IntiFlags;
    UINT8                   Type;               /* 1=PMI, 2=INIT, 3=corrected */
    UINT8                   Id;                 /* Processor ID */
    UINT8                   Eid;                /* Processor EID */
    UINT8                   IoSapicVector;      /* Vector value for PMI interrupts */
    UINT32                  GlobalIrq;          /* Global system interrupt */
    UINT32                  Flags;              /* Interrupt Source Flags */

} ACPI_MADT_INTERRUPT_SOURCE;

/* Masks for Flags field above */

#define ACPI_MADT_CPEI_OVERRIDE     (1)


/* 9: Processor Local X2APIC (ACPI 4.0) */

typedef struct acpi_madt_local_x2apic
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;           /* Reserved - must be zero */
    UINT32                  LocalApicId;        /* Processor x2APIC ID  */
    UINT32                  LapicFlags;
    UINT32                  Uid;                /* ACPI processor UID */

} ACPI_MADT_LOCAL_X2APIC;


/* 10: Local X2APIC NMI (ACPI 4.0) */

typedef struct acpi_madt_local_x2apic_nmi
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  IntiFlags;
    UINT32                  Uid;                /* ACPI processor UID */
    UINT8                   Lint;               /* LINTn to which NMI is connected */
    UINT8                   Reserved[3];        /* Reserved - must be zero */

} ACPI_MADT_LOCAL_X2APIC_NMI;


/* 11: Generic Interrupt - GICC (ACPI 5.0 + ACPI 6.0 + ACPI 6.3 changes) */

typedef struct acpi_madt_generic_interrupt
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;           /* Reserved - must be zero */
    UINT32                  CpuInterfaceNumber;
    UINT32                  Uid;
    UINT32                  Flags;
    UINT32                  ParkingVersion;
    UINT32                  PerformanceInterrupt;
    UINT64                  ParkedAddress;
    UINT64                  BaseAddress;
    UINT64                  GicvBaseAddress;
    UINT64                  GichBaseAddress;
    UINT32                  VgicInterrupt;
    UINT64                  GicrBaseAddress;
    UINT64                  ArmMpidr;
    UINT8                   EfficiencyClass;
    UINT8                   Reserved2[1];
    UINT16                  SpeInterrupt;       /* ACPI 6.3 */

} ACPI_MADT_GENERIC_INTERRUPT;

/* Masks for Flags field above */

/* ACPI_MADT_ENABLED                    (1)      Processor is usable if set */
#define ACPI_MADT_PERFORMANCE_IRQ_MODE  (1<<1)  /* 01: Performance Interrupt Mode */
#define ACPI_MADT_VGIC_IRQ_MODE         (1<<2)  /* 02: VGIC Maintenance Interrupt mode */


/* 12: Generic Distributor (ACPI 5.0 + ACPI 6.0 changes) */

typedef struct acpi_madt_generic_distributor
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;           /* Reserved - must be zero */
    UINT32                  GicId;
    UINT64                  BaseAddress;
    UINT32                  GlobalIrqBase;
    UINT8                   Version;
    UINT8                   Reserved2[3];       /* Reserved - must be zero */

} ACPI_MADT_GENERIC_DISTRIBUTOR;

/* Values for Version field above */

enum AcpiMadtGicVersion
{
    ACPI_MADT_GIC_VERSION_NONE          = 0,
    ACPI_MADT_GIC_VERSION_V1            = 1,
    ACPI_MADT_GIC_VERSION_V2            = 2,
    ACPI_MADT_GIC_VERSION_V3            = 3,
    ACPI_MADT_GIC_VERSION_V4            = 4,
    ACPI_MADT_GIC_VERSION_RESERVED      = 5     /* 5 and greater are reserved */
};


/* 13: Generic MSI Frame (ACPI 5.1) */

typedef struct acpi_madt_generic_msi_frame
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;           /* Reserved - must be zero */
    UINT32                  MsiFrameId;
    UINT64                  BaseAddress;
    UINT32                  Flags;
    UINT16                  SpiCount;
    UINT16                  SpiBase;

} ACPI_MADT_GENERIC_MSI_FRAME;

/* Masks for Flags field above */

#define ACPI_MADT_OVERRIDE_SPI_VALUES   (1)


/* 14: Generic Redistributor (ACPI 5.1) */

typedef struct acpi_madt_generic_redistributor
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;           /* reserved - must be zero */
    UINT64                  BaseAddress;
    UINT32                  Length;

} ACPI_MADT_GENERIC_REDISTRIBUTOR;


/* 15: Generic Translator (ACPI 6.0) */

typedef struct acpi_madt_generic_translator
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;           /* reserved - must be zero */
    UINT32                  TranslationId;
    UINT64                  BaseAddress;
    UINT32                  Reserved2;

} ACPI_MADT_GENERIC_TRANSLATOR;


/*
 * Common flags fields for MADT subtables
 */

/* MADT Local APIC flags */

#define ACPI_MADT_ENABLED           (1)         /* 00: Processor is usable if set */

/* MADT MPS INTI flags (IntiFlags) */

#define ACPI_MADT_POLARITY_MASK     (3)         /* 00-01: Polarity of APIC I/O input signals */
#define ACPI_MADT_TRIGGER_MASK      (3<<2)      /* 02-03: Trigger mode of APIC input signals */

/* Values for MPS INTI flags */

#define ACPI_MADT_POLARITY_CONFORMS       0
#define ACPI_MADT_POLARITY_ACTIVE_HIGH    1
#define ACPI_MADT_POLARITY_RESERVED       2
#define ACPI_MADT_POLARITY_ACTIVE_LOW     3

#define ACPI_MADT_TRIGGER_CONFORMS        (0)
#define ACPI_MADT_TRIGGER_EDGE            (1<<2)
#define ACPI_MADT_TRIGGER_RESERVED        (2<<2)
#define ACPI_MADT_TRIGGER_LEVEL           (3<<2)


/*******************************************************************************
 *
 * MCFG - PCI Memory Mapped Configuration table and subtable
 *        Version 1
 *
 * Conforms to "PCI Firmware Specification", Revision 3.0, June 20, 2005
 *
 ******************************************************************************/

typedef struct acpi_table_mcfg
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   Reserved[8];

} ACPI_TABLE_MCFG;


/* Subtable */

typedef struct acpi_mcfg_allocation
{
    UINT64                  Address;            /* Base address, processor-relative */
    UINT16                  PciSegment;         /* PCI segment group number */
    UINT8                   StartBusNumber;     /* Starting PCI Bus number */
    UINT8                   EndBusNumber;       /* Final PCI Bus number */
    UINT32                  Reserved;

} ACPI_MCFG_ALLOCATION;


/*******************************************************************************
 *
 * MCHI - Management Controller Host Interface Table
 *        Version 1
 *
 * Conforms to "Management Component Transport Protocol (MCTP) Host
 * Interface Specification", Revision 1.0.0a, October 13, 2009
 *
 ******************************************************************************/

typedef struct acpi_table_mchi
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   InterfaceType;
    UINT8                   Protocol;
    UINT64                  ProtocolData;
    UINT8                   InterruptType;
    UINT8                   Gpe;
    UINT8                   PciDeviceFlag;
    UINT32                  GlobalInterrupt;
    ACPI_GENERIC_ADDRESS    ControlRegister;
    UINT8                   PciSegment;
    UINT8                   PciBus;
    UINT8                   PciDevice;
    UINT8                   PciFunction;

} ACPI_TABLE_MCHI;


/*******************************************************************************
 *
 * MPST - Memory Power State Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

#define ACPI_MPST_CHANNEL_INFO \
    UINT8                   ChannelId; \
    UINT8                   Reserved1[3]; \
    UINT16                  PowerNodeCount; \
    UINT16                  Reserved2;

/* Main table */

typedef struct acpi_table_mpst
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    ACPI_MPST_CHANNEL_INFO                      /* Platform Communication Channel */

} ACPI_TABLE_MPST;


/* Memory Platform Communication Channel Info */

typedef struct acpi_mpst_channel
{
    ACPI_MPST_CHANNEL_INFO                      /* Platform Communication Channel */

} ACPI_MPST_CHANNEL;


/* Memory Power Node Structure */

typedef struct acpi_mpst_power_node
{
    UINT8                   Flags;
    UINT8                   Reserved1;
    UINT16                  NodeId;
    UINT32                  Length;
    UINT64                  RangeAddress;
    UINT64                  RangeLength;
    UINT32                  NumPowerStates;
    UINT32                  NumPhysicalComponents;

} ACPI_MPST_POWER_NODE;

/* Values for Flags field above */

#define ACPI_MPST_ENABLED               1
#define ACPI_MPST_POWER_MANAGED         2
#define ACPI_MPST_HOT_PLUG_CAPABLE      4


/* Memory Power State Structure (follows POWER_NODE above) */

typedef struct acpi_mpst_power_state
{
    UINT8                   PowerState;
    UINT8                   InfoIndex;

} ACPI_MPST_POWER_STATE;


/* Physical Component ID Structure (follows POWER_STATE above) */

typedef struct acpi_mpst_component
{
    UINT16                  ComponentId;

} ACPI_MPST_COMPONENT;


/* Memory Power State Characteristics Structure (follows all POWER_NODEs) */

typedef struct acpi_mpst_data_hdr
{
    UINT16                  CharacteristicsCount;
    UINT16                  Reserved;

} ACPI_MPST_DATA_HDR;

typedef struct acpi_mpst_power_data
{
    UINT8                   StructureId;
    UINT8                   Flags;
    UINT16                  Reserved1;
    UINT32                  AveragePower;
    UINT32                  PowerSaving;
    UINT64                  ExitLatency;
    UINT64                  Reserved2;

} ACPI_MPST_POWER_DATA;

/* Values for Flags field above */

#define ACPI_MPST_PRESERVE              1
#define ACPI_MPST_AUTOENTRY             2
#define ACPI_MPST_AUTOEXIT              4


/* Shared Memory Region (not part of an ACPI table) */

typedef struct acpi_mpst_shared
{
    UINT32                  Signature;
    UINT16                  PccCommand;
    UINT16                  PccStatus;
    UINT32                  CommandRegister;
    UINT32                  StatusRegister;
    UINT32                  PowerStateId;
    UINT32                  PowerNodeId;
    UINT64                  EnergyConsumed;
    UINT64                  AveragePower;

} ACPI_MPST_SHARED;


/*******************************************************************************
 *
 * MSCT - Maximum System Characteristics Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_msct
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  ProximityOffset;    /* Location of proximity info struct(s) */
    UINT32                  MaxProximityDomains;/* Max number of proximity domains */
    UINT32                  MaxClockDomains;    /* Max number of clock domains */
    UINT64                  MaxAddress;         /* Max physical address in system */

} ACPI_TABLE_MSCT;


/* Subtable - Maximum Proximity Domain Information. Version 1 */

typedef struct acpi_msct_proximity
{
    UINT8                   Revision;
    UINT8                   Length;
    UINT32                  RangeStart;         /* Start of domain range */
    UINT32                  RangeEnd;           /* End of domain range */
    UINT32                  ProcessorCapacity;
    UINT64                  MemoryCapacity;     /* In bytes */

} ACPI_MSCT_PROXIMITY;


/*******************************************************************************
 *
 * MSDM - Microsoft Data Management table
 *
 * Conforms to "Microsoft Software Licensing Tables (SLIC and MSDM)",
 * November 29, 2011. Copyright 2011 Microsoft
 *
 ******************************************************************************/

/* Basic MSDM table is only the common ACPI header */

typedef struct acpi_table_msdm
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_MSDM;


/*******************************************************************************
 *
 * MTMR - MID Timer Table
 *        Version 1
 *
 * Conforms to "Simple Firmware Interface Specification",
 * Draft 0.8.2, Oct 19, 2010
 * NOTE: The ACPI MTMR is equivalent to the SFI MTMR table.
 *
 ******************************************************************************/

typedef struct acpi_table_mtmr
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_MTMR;

/* MTMR entry */

typedef struct acpi_mtmr_entry
{
    ACPI_GENERIC_ADDRESS    PhysicalAddress;
    UINT32                  Frequency;
    UINT32                  Irq;

} ACPI_MTMR_ENTRY;


/*******************************************************************************
 *
 * NFIT - NVDIMM Interface Table (ACPI 6.0+)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_nfit
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Reserved;           /* Reserved, must be zero */

} ACPI_TABLE_NFIT;

/* Subtable header for NFIT */

typedef struct acpi_nfit_header
{
    UINT16                   Type;
    UINT16                   Length;

} ACPI_NFIT_HEADER;


/* Values for subtable type in ACPI_NFIT_HEADER */

enum AcpiNfitType
{
    ACPI_NFIT_TYPE_SYSTEM_ADDRESS       = 0,
    ACPI_NFIT_TYPE_MEMORY_MAP           = 1,
    ACPI_NFIT_TYPE_INTERLEAVE           = 2,
    ACPI_NFIT_TYPE_SMBIOS               = 3,
    ACPI_NFIT_TYPE_CONTROL_REGION       = 4,
    ACPI_NFIT_TYPE_DATA_REGION          = 5,
    ACPI_NFIT_TYPE_FLUSH_ADDRESS        = 6,
    ACPI_NFIT_TYPE_CAPABILITIES         = 7,
    ACPI_NFIT_TYPE_RESERVED             = 8     /* 8 and greater are reserved */
};

/*
 * NFIT Subtables
 */

/* 0: System Physical Address Range Structure */

typedef struct acpi_nfit_system_address
{
    ACPI_NFIT_HEADER        Header;
    UINT16                  RangeIndex;
    UINT16                  Flags;
    UINT32                  Reserved;           /* Reserved, must be zero */
    UINT32                  ProximityDomain;
    UINT8                   RangeGuid[16];
    UINT64                  Address;
    UINT64                  Length;
    UINT64                  MemoryMapping;

} ACPI_NFIT_SYSTEM_ADDRESS;

/* Flags */

#define ACPI_NFIT_ADD_ONLINE_ONLY       (1)     /* 00: Add/Online Operation Only */
#define ACPI_NFIT_PROXIMITY_VALID       (1<<1)  /* 01: Proximity Domain Valid */

/* Range Type GUIDs appear in the include/acuuid.h file */


/* 1: Memory Device to System Address Range Map Structure */

typedef struct acpi_nfit_memory_map
{
    ACPI_NFIT_HEADER        Header;
    UINT32                  DeviceHandle;
    UINT16                  PhysicalId;
    UINT16                  RegionId;
    UINT16                  RangeIndex;
    UINT16                  RegionIndex;
    UINT64                  RegionSize;
    UINT64                  RegionOffset;
    UINT64                  Address;
    UINT16                  InterleaveIndex;
    UINT16                  InterleaveWays;
    UINT16                  Flags;
    UINT16                  Reserved;           /* Reserved, must be zero */

} ACPI_NFIT_MEMORY_MAP;

/* Flags */

#define ACPI_NFIT_MEM_SAVE_FAILED       (1)     /* 00: Last SAVE to Memory Device failed */
#define ACPI_NFIT_MEM_RESTORE_FAILED    (1<<1)  /* 01: Last RESTORE from Memory Device failed */
#define ACPI_NFIT_MEM_FLUSH_FAILED      (1<<2)  /* 02: Platform flush failed */
#define ACPI_NFIT_MEM_NOT_ARMED         (1<<3)  /* 03: Memory Device is not armed */
#define ACPI_NFIT_MEM_HEALTH_OBSERVED   (1<<4)  /* 04: Memory Device observed SMART/health events */
#define ACPI_NFIT_MEM_HEALTH_ENABLED    (1<<5)  /* 05: SMART/health events enabled */
#define ACPI_NFIT_MEM_MAP_FAILED        (1<<6)  /* 06: Mapping to SPA failed */


/* 2: Interleave Structure */

typedef struct acpi_nfit_interleave
{
    ACPI_NFIT_HEADER        Header;
    UINT16                  InterleaveIndex;
    UINT16                  Reserved;           /* Reserved, must be zero */
    UINT32                  LineCount;
    UINT32                  LineSize;
    UINT32                  LineOffset[1];      /* Variable length */

} ACPI_NFIT_INTERLEAVE;


/* 3: SMBIOS Management Information Structure */

typedef struct acpi_nfit_smbios
{
    ACPI_NFIT_HEADER        Header;
    UINT32                  Reserved;           /* Reserved, must be zero */
    UINT8                   Data[1];            /* Variable length */

} ACPI_NFIT_SMBIOS;


/* 4: NVDIMM Control Region Structure */

typedef struct acpi_nfit_control_region
{
    ACPI_NFIT_HEADER        Header;
    UINT16                  RegionIndex;
    UINT16                  VendorId;
    UINT16                  DeviceId;
    UINT16                  RevisionId;
    UINT16                  SubsystemVendorId;
    UINT16                  SubsystemDeviceId;
    UINT16                  SubsystemRevisionId;
    UINT8                   ValidFields;
    UINT8                   ManufacturingLocation;
    UINT16                  ManufacturingDate;
    UINT8                   Reserved[2];        /* Reserved, must be zero */
    UINT32                  SerialNumber;
    UINT16                  Code;
    UINT16                  Windows;
    UINT64                  WindowSize;
    UINT64                  CommandOffset;
    UINT64                  CommandSize;
    UINT64                  StatusOffset;
    UINT64                  StatusSize;
    UINT16                  Flags;
    UINT8                   Reserved1[6];       /* Reserved, must be zero */

} ACPI_NFIT_CONTROL_REGION;

/* Flags */

#define ACPI_NFIT_CONTROL_BUFFERED          (1)     /* Block Data Windows implementation is buffered */

/* ValidFields bits */

#define ACPI_NFIT_CONTROL_MFG_INFO_VALID    (1)     /* Manufacturing fields are valid */


/* 5: NVDIMM Block Data Window Region Structure */

typedef struct acpi_nfit_data_region
{
    ACPI_NFIT_HEADER        Header;
    UINT16                  RegionIndex;
    UINT16                  Windows;
    UINT64                  Offset;
    UINT64                  Size;
    UINT64                  Capacity;
    UINT64                  StartAddress;

} ACPI_NFIT_DATA_REGION;


/* 6: Flush Hint Address Structure */

typedef struct acpi_nfit_flush_address
{
    ACPI_NFIT_HEADER        Header;
    UINT32                  DeviceHandle;
    UINT16                  HintCount;
    UINT8                   Reserved[6];        /* Reserved, must be zero */
    UINT64                  HintAddress[1];     /* Variable length */

} ACPI_NFIT_FLUSH_ADDRESS;


/* 7: Platform Capabilities Structure */

typedef struct acpi_nfit_capabilities
{
    ACPI_NFIT_HEADER        Header;
    UINT8                   HighestCapability;
    UINT8                   Reserved[3];       /* Reserved, must be zero */
    UINT32                  Capabilities;
    UINT32                  Reserved2;

} ACPI_NFIT_CAPABILITIES;

/* Capabilities Flags */

#define ACPI_NFIT_CAPABILITY_CACHE_FLUSH       (1)     /* 00: Cache Flush to NVDIMM capable */
#define ACPI_NFIT_CAPABILITY_MEM_FLUSH         (1<<1)  /* 01: Memory Flush to NVDIMM capable */
#define ACPI_NFIT_CAPABILITY_MEM_MIRRORING     (1<<2)  /* 02: Memory Mirroring capable */


/*
 * NFIT/DVDIMM device handle support - used as the _ADR for each NVDIMM
 */
typedef struct nfit_device_handle
{
    UINT32                  Handle;

} NFIT_DEVICE_HANDLE;

/* Device handle construction and extraction macros */

#define ACPI_NFIT_DIMM_NUMBER_MASK              0x0000000F
#define ACPI_NFIT_CHANNEL_NUMBER_MASK           0x000000F0
#define ACPI_NFIT_MEMORY_ID_MASK                0x00000F00
#define ACPI_NFIT_SOCKET_ID_MASK                0x0000F000
#define ACPI_NFIT_NODE_ID_MASK                  0x0FFF0000

#define ACPI_NFIT_DIMM_NUMBER_OFFSET            0
#define ACPI_NFIT_CHANNEL_NUMBER_OFFSET         4
#define ACPI_NFIT_MEMORY_ID_OFFSET              8
#define ACPI_NFIT_SOCKET_ID_OFFSET              12
#define ACPI_NFIT_NODE_ID_OFFSET                16

/* Macro to construct a NFIT/NVDIMM device handle */

#define ACPI_NFIT_BUILD_DEVICE_HANDLE(dimm, channel, memory, socket, node) \
    ((dimm)                                         | \
    ((channel) << ACPI_NFIT_CHANNEL_NUMBER_OFFSET)  | \
    ((memory)  << ACPI_NFIT_MEMORY_ID_OFFSET)       | \
    ((socket)  << ACPI_NFIT_SOCKET_ID_OFFSET)       | \
    ((node)    << ACPI_NFIT_NODE_ID_OFFSET))

/* Macros to extract individual fields from a NFIT/NVDIMM device handle */

#define ACPI_NFIT_GET_DIMM_NUMBER(handle) \
    ((handle) & ACPI_NFIT_DIMM_NUMBER_MASK)

#define ACPI_NFIT_GET_CHANNEL_NUMBER(handle) \
    (((handle) & ACPI_NFIT_CHANNEL_NUMBER_MASK) >> ACPI_NFIT_CHANNEL_NUMBER_OFFSET)

#define ACPI_NFIT_GET_MEMORY_ID(handle) \
    (((handle) & ACPI_NFIT_MEMORY_ID_MASK)      >> ACPI_NFIT_MEMORY_ID_OFFSET)

#define ACPI_NFIT_GET_SOCKET_ID(handle) \
    (((handle) & ACPI_NFIT_SOCKET_ID_MASK)      >> ACPI_NFIT_SOCKET_ID_OFFSET)

#define ACPI_NFIT_GET_NODE_ID(handle) \
    (((handle) & ACPI_NFIT_NODE_ID_MASK)        >> ACPI_NFIT_NODE_ID_OFFSET)


/*******************************************************************************
 *
 * PCCT - Platform Communications Channel Table (ACPI 5.0)
 *        Version 2 (ACPI 6.2)
 *
 ******************************************************************************/

typedef struct acpi_table_pcct
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Flags;
    UINT64                  Reserved;

} ACPI_TABLE_PCCT;

/* Values for Flags field above */

#define ACPI_PCCT_DOORBELL              1

/* Values for subtable type in ACPI_SUBTABLE_HEADER */

enum AcpiPcctType
{
    ACPI_PCCT_TYPE_GENERIC_SUBSPACE             = 0,
    ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE          = 1,
    ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE_TYPE2    = 2,    /* ACPI 6.1 */
    ACPI_PCCT_TYPE_EXT_PCC_MASTER_SUBSPACE      = 3,    /* ACPI 6.2 */
    ACPI_PCCT_TYPE_EXT_PCC_SLAVE_SUBSPACE       = 4,    /* ACPI 6.2 */
    ACPI_PCCT_TYPE_RESERVED                     = 5     /* 5 and greater are reserved */
};

/*
 * PCCT Subtables, correspond to Type in ACPI_SUBTABLE_HEADER
 */

/* 0: Generic Communications Subspace */

typedef struct acpi_pcct_subspace
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   Reserved[6];
    UINT64                  BaseAddress;
    UINT64                  Length;
    ACPI_GENERIC_ADDRESS    DoorbellRegister;
    UINT64                  PreserveMask;
    UINT64                  WriteMask;
    UINT32                  Latency;
    UINT32                  MaxAccessRate;
    UINT16                  MinTurnaroundTime;

} ACPI_PCCT_SUBSPACE;


/* 1: HW-reduced Communications Subspace (ACPI 5.1) */

typedef struct acpi_pcct_hw_reduced
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT32                  PlatformInterrupt;
    UINT8                   Flags;
    UINT8                   Reserved;
    UINT64                  BaseAddress;
    UINT64                  Length;
    ACPI_GENERIC_ADDRESS    DoorbellRegister;
    UINT64                  PreserveMask;
    UINT64                  WriteMask;
    UINT32                  Latency;
    UINT32                  MaxAccessRate;
    UINT16                  MinTurnaroundTime;

} ACPI_PCCT_HW_REDUCED;


/* 2: HW-reduced Communications Subspace Type 2 (ACPI 6.1) */

typedef struct acpi_pcct_hw_reduced_type2
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT32                  PlatformInterrupt;
    UINT8                   Flags;
    UINT8                   Reserved;
    UINT64                  BaseAddress;
    UINT64                  Length;
    ACPI_GENERIC_ADDRESS    DoorbellRegister;
    UINT64                  PreserveMask;
    UINT64                  WriteMask;
    UINT32                  Latency;
    UINT32                  MaxAccessRate;
    UINT16                  MinTurnaroundTime;
    ACPI_GENERIC_ADDRESS    PlatformAckRegister;
    UINT64                  AckPreserveMask;
    UINT64                  AckWriteMask;

} ACPI_PCCT_HW_REDUCED_TYPE2;


/* 3: Extended PCC Master Subspace Type 3 (ACPI 6.2) */

typedef struct acpi_pcct_ext_pcc_master
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT32                  PlatformInterrupt;
    UINT8                   Flags;
    UINT8                   Reserved1;
    UINT64                  BaseAddress;
    UINT32                  Length;
    ACPI_GENERIC_ADDRESS    DoorbellRegister;
    UINT64                  PreserveMask;
    UINT64                  WriteMask;
    UINT32                  Latency;
    UINT32                  MaxAccessRate;
    UINT32                  MinTurnaroundTime;
    ACPI_GENERIC_ADDRESS    PlatformAckRegister;
    UINT64                  AckPreserveMask;
    UINT64                  AckSetMask;
    UINT64                  Reserved2;
    ACPI_GENERIC_ADDRESS    CmdCompleteRegister;
    UINT64                  CmdCompleteMask;
    ACPI_GENERIC_ADDRESS    CmdUpdateRegister;
    UINT64                  CmdUpdatePreserveMask;
    UINT64                  CmdUpdateSetMask;
    ACPI_GENERIC_ADDRESS    ErrorStatusRegister;
    UINT64                  ErrorStatusMask;

} ACPI_PCCT_EXT_PCC_MASTER;


/* 4: Extended PCC Slave Subspace Type 4 (ACPI 6.2) */

typedef struct acpi_pcct_ext_pcc_slave
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT32                  PlatformInterrupt;
    UINT8                   Flags;
    UINT8                   Reserved1;
    UINT64                  BaseAddress;
    UINT32                  Length;
    ACPI_GENERIC_ADDRESS    DoorbellRegister;
    UINT64                  PreserveMask;
    UINT64                  WriteMask;
    UINT32                  Latency;
    UINT32                  MaxAccessRate;
    UINT32                  MinTurnaroundTime;
    ACPI_GENERIC_ADDRESS    PlatformAckRegister;
    UINT64                  AckPreserveMask;
    UINT64                  AckSetMask;
    UINT64                  Reserved2;
    ACPI_GENERIC_ADDRESS    CmdCompleteRegister;
    UINT64                  CmdCompleteMask;
    ACPI_GENERIC_ADDRESS    CmdUpdateRegister;
    UINT64                  CmdUpdatePreserveMask;
    UINT64                  CmdUpdateSetMask;
    ACPI_GENERIC_ADDRESS    ErrorStatusRegister;
    UINT64                  ErrorStatusMask;

} ACPI_PCCT_EXT_PCC_SLAVE;


/* Values for doorbell flags above */

#define ACPI_PCCT_INTERRUPT_POLARITY    (1)
#define ACPI_PCCT_INTERRUPT_MODE        (1<<1)


/*
 * PCC memory structures (not part of the ACPI table)
 */

/* Shared Memory Region */

typedef struct acpi_pcct_shared_memory
{
    UINT32                  Signature;
    UINT16                  Command;
    UINT16                  Status;

} ACPI_PCCT_SHARED_MEMORY;


/* Extended PCC Subspace Shared Memory Region (ACPI 6.2) */

typedef struct acpi_pcct_ext_pcc_shared_memory
{
    UINT32                  Signature;
    UINT32                  Flags;
    UINT32                  Length;
    UINT32                  Command;

} ACPI_PCCT_EXT_PCC_SHARED_MEMORY;


/*******************************************************************************
 *
 * PDTT - Platform Debug Trigger Table (ACPI 6.2)
 *        Version 0
 *
 ******************************************************************************/

typedef struct acpi_table_pdtt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   TriggerCount;
    UINT8                   Reserved[3];
    UINT32                  ArrayOffset;

} ACPI_TABLE_PDTT;


/*
 * PDTT Communication Channel Identifier Structure.
 * The number of these structures is defined by TriggerCount above,
 * starting at ArrayOffset.
 */
typedef struct acpi_pdtt_channel
{
    UINT8                   SubchannelId;
    UINT8                   Flags;

} ACPI_PDTT_CHANNEL;

/* Flags for above */

#define ACPI_PDTT_RUNTIME_TRIGGER           (1)
#define ACPI_PDTT_WAIT_COMPLETION           (1<<1)
#define ACPI_PDTT_TRIGGER_ORDER             (1<<2)


/*******************************************************************************
 *
 * PMTT - Platform Memory Topology Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_pmtt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Reserved;

} ACPI_TABLE_PMTT;


/* Common header for PMTT subtables that follow main table */

typedef struct acpi_pmtt_header
{
    UINT8                   Type;
    UINT8                   Reserved1;
    UINT16                  Length;
    UINT16                  Flags;
    UINT16                  Reserved2;

} ACPI_PMTT_HEADER;

/* Values for Type field above */

#define ACPI_PMTT_TYPE_SOCKET           0
#define ACPI_PMTT_TYPE_CONTROLLER       1
#define ACPI_PMTT_TYPE_DIMM             2
#define ACPI_PMTT_TYPE_RESERVED         3 /* 0x03-0xFF are reserved */

/* Values for Flags field above */

#define ACPI_PMTT_TOP_LEVEL             0x0001
#define ACPI_PMTT_PHYSICAL              0x0002
#define ACPI_PMTT_MEMORY_TYPE           0x000C


/*
 * PMTT subtables, correspond to Type in acpi_pmtt_header
 */


/* 0: Socket Structure */

typedef struct acpi_pmtt_socket
{
    ACPI_PMTT_HEADER        Header;
    UINT16                  SocketId;
    UINT16                  Reserved;

} ACPI_PMTT_SOCKET;


/* 1: Memory Controller subtable */

typedef struct acpi_pmtt_controller
{
    ACPI_PMTT_HEADER        Header;
    UINT32                  ReadLatency;
    UINT32                  WriteLatency;
    UINT32                  ReadBandwidth;
    UINT32                  WriteBandwidth;
    UINT16                  AccessWidth;
    UINT16                  Alignment;
    UINT16                  Reserved;
    UINT16                  DomainCount;

} ACPI_PMTT_CONTROLLER;

/* 1a: Proximity Domain substructure */

typedef struct acpi_pmtt_domain
{
    UINT32                  ProximityDomain;

} ACPI_PMTT_DOMAIN;


/* 2: Physical Component Identifier (DIMM) */

typedef struct acpi_pmtt_physical_component
{
    ACPI_PMTT_HEADER        Header;
    UINT16                  ComponentId;
    UINT16                  Reserved;
    UINT32                  MemorySize;
    UINT32                  BiosHandle;

} ACPI_PMTT_PHYSICAL_COMPONENT;


/*******************************************************************************
 *
 * PPTT - Processor Properties Topology Table (ACPI 6.2)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_pptt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_PPTT;

/* Values for Type field above */

enum AcpiPpttType
{
    ACPI_PPTT_TYPE_PROCESSOR            = 0,
    ACPI_PPTT_TYPE_CACHE                = 1,
    ACPI_PPTT_TYPE_ID                   = 2,
    ACPI_PPTT_TYPE_RESERVED             = 3
};


/* 0: Processor Hierarchy Node Structure */

typedef struct acpi_pptt_processor
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;
    UINT32                  Flags;
    UINT32                  Parent;
    UINT32                  AcpiProcessorId;
    UINT32                  NumberOfPrivResources;

} ACPI_PPTT_PROCESSOR;

/* Flags */

#define ACPI_PPTT_PHYSICAL_PACKAGE          (1)
#define ACPI_PPTT_ACPI_PROCESSOR_ID_VALID   (1<<1)
#define ACPI_PPTT_ACPI_PROCESSOR_IS_THREAD  (1<<2)  /* ACPI 6.3 */
#define ACPI_PPTT_ACPI_LEAF_NODE            (1<<3)  /* ACPI 6.3 */
#define ACPI_PPTT_ACPI_IDENTICAL            (1<<4)  /* ACPI 6.3 */


/* 1: Cache Type Structure */

typedef struct acpi_pptt_cache
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;
    UINT32                  Flags;
    UINT32                  NextLevelOfCache;
    UINT32                  Size;
    UINT32                  NumberOfSets;
    UINT8                   Associativity;
    UINT8                   Attributes;
    UINT16                  LineSize;

} ACPI_PPTT_CACHE;

/* Flags */

#define ACPI_PPTT_SIZE_PROPERTY_VALID       (1)     /* Physical property valid */
#define ACPI_PPTT_NUMBER_OF_SETS_VALID      (1<<1)  /* Number of sets valid */
#define ACPI_PPTT_ASSOCIATIVITY_VALID       (1<<2)  /* Associativity valid */
#define ACPI_PPTT_ALLOCATION_TYPE_VALID     (1<<3)  /* Allocation type valid */
#define ACPI_PPTT_CACHE_TYPE_VALID          (1<<4)  /* Cache type valid */
#define ACPI_PPTT_WRITE_POLICY_VALID        (1<<5)  /* Write policy valid */
#define ACPI_PPTT_LINE_SIZE_VALID           (1<<6)  /* Line size valid */

/* Masks for Attributes */

#define ACPI_PPTT_MASK_ALLOCATION_TYPE      (0x03)  /* Allocation type */
#define ACPI_PPTT_MASK_CACHE_TYPE           (0x0C)  /* Cache type */
#define ACPI_PPTT_MASK_WRITE_POLICY         (0x10)  /* Write policy */

/* Attributes describing cache */
#define ACPI_PPTT_CACHE_READ_ALLOCATE       (0x0)   /* Cache line is allocated on read */
#define ACPI_PPTT_CACHE_WRITE_ALLOCATE      (0x01)  /* Cache line is allocated on write */
#define ACPI_PPTT_CACHE_RW_ALLOCATE         (0x02)  /* Cache line is allocated on read and write */
#define ACPI_PPTT_CACHE_RW_ALLOCATE_ALT     (0x03)  /* Alternate representation of above */

#define ACPI_PPTT_CACHE_TYPE_DATA           (0x0)   /* Data cache */
#define ACPI_PPTT_CACHE_TYPE_INSTR          (1<<2)  /* Instruction cache */
#define ACPI_PPTT_CACHE_TYPE_UNIFIED        (2<<2)  /* Unified I & D cache */
#define ACPI_PPTT_CACHE_TYPE_UNIFIED_ALT    (3<<2)  /* Alternate representation of above */

#define ACPI_PPTT_CACHE_POLICY_WB           (0x0)   /* Cache is write back */
#define ACPI_PPTT_CACHE_POLICY_WT           (1<<4)  /* Cache is write through */

/* 2: ID Structure */

typedef struct acpi_pptt_id
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;
    UINT32                  VendorId;
    UINT64                  Level1Id;
    UINT64                  Level2Id;
    UINT16                  MajorRev;
    UINT16                  MinorRev;
    UINT16                  SpinRev;

} ACPI_PPTT_ID;


/*******************************************************************************
 *
 * RASF - RAS Feature Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_rasf
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   ChannelId[12];

} ACPI_TABLE_RASF;

/* RASF Platform Communication Channel Shared Memory Region */

typedef struct acpi_rasf_shared_memory
{
    UINT32                  Signature;
    UINT16                  Command;
    UINT16                  Status;
    UINT16                  Version;
    UINT8                   Capabilities[16];
    UINT8                   SetCapabilities[16];
    UINT16                  NumParameterBlocks;
    UINT32                  SetCapabilitiesStatus;

} ACPI_RASF_SHARED_MEMORY;

/* RASF Parameter Block Structure Header */

typedef struct acpi_rasf_parameter_block
{
    UINT16                  Type;
    UINT16                  Version;
    UINT16                  Length;

} ACPI_RASF_PARAMETER_BLOCK;

/* RASF Parameter Block Structure for PATROL_SCRUB */

typedef struct acpi_rasf_patrol_scrub_parameter
{
    ACPI_RASF_PARAMETER_BLOCK   Header;
    UINT16                      PatrolScrubCommand;
    UINT64                      RequestedAddressRange[2];
    UINT64                      ActualAddressRange[2];
    UINT16                      Flags;
    UINT8                       RequestedSpeed;

} ACPI_RASF_PATROL_SCRUB_PARAMETER;

/* Masks for Flags and Speed fields above */

#define ACPI_RASF_SCRUBBER_RUNNING      1
#define ACPI_RASF_SPEED                 (7<<1)
#define ACPI_RASF_SPEED_SLOW            (0<<1)
#define ACPI_RASF_SPEED_MEDIUM          (4<<1)
#define ACPI_RASF_SPEED_FAST            (7<<1)

/* Channel Commands */

enum AcpiRasfCommands
{
    ACPI_RASF_EXECUTE_RASF_COMMAND      = 1
};

/* Platform RAS Capabilities */

enum AcpiRasfCapabiliities
{
    ACPI_HW_PATROL_SCRUB_SUPPORTED      = 0,
    ACPI_SW_PATROL_SCRUB_EXPOSED        = 1
};

/* Patrol Scrub Commands */

enum AcpiRasfPatrolScrubCommands
{
    ACPI_RASF_GET_PATROL_PARAMETERS     = 1,
    ACPI_RASF_START_PATROL_SCRUBBER     = 2,
    ACPI_RASF_STOP_PATROL_SCRUBBER      = 3
};

/* Channel Command flags */

#define ACPI_RASF_GENERATE_SCI          (1<<15)

/* Status values */

enum AcpiRasfStatus
{
    ACPI_RASF_SUCCESS                   = 0,
    ACPI_RASF_NOT_VALID                 = 1,
    ACPI_RASF_NOT_SUPPORTED             = 2,
    ACPI_RASF_BUSY                      = 3,
    ACPI_RASF_FAILED                    = 4,
    ACPI_RASF_ABORTED                   = 5,
    ACPI_RASF_INVALID_DATA              = 6
};

/* Status flags */

#define ACPI_RASF_COMMAND_COMPLETE      (1)
#define ACPI_RASF_SCI_DOORBELL          (1<<1)
#define ACPI_RASF_ERROR                 (1<<2)
#define ACPI_RASF_STATUS                (0x1F<<3)


/*******************************************************************************
 *
 * SBST - Smart Battery Specification Table
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_sbst
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  WarningLevel;
    UINT32                  LowLevel;
    UINT32                  CriticalLevel;

} ACPI_TABLE_SBST;


/*******************************************************************************
 *
 * SDEI - Software Delegated Exception Interface Descriptor Table
 *
 * Conforms to "Software Delegated Exception Interface (SDEI)" ARM DEN0054A,
 * May 8th, 2017. Copyright 2017 ARM Ltd.
 *
 ******************************************************************************/

typedef struct acpi_table_sdei
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_SDEI;


/*******************************************************************************
 *
 * SDEV - Secure Devices Table (ACPI 6.2)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_sdev
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_SDEV;


typedef struct acpi_sdev_header
{
    UINT8                   Type;
    UINT8                   Flags;
    UINT16                  Length;

} ACPI_SDEV_HEADER;


/* Values for subtable type above */

enum AcpiSdevType
{
    ACPI_SDEV_TYPE_NAMESPACE_DEVICE     = 0,
    ACPI_SDEV_TYPE_PCIE_ENDPOINT_DEVICE = 1,
    ACPI_SDEV_TYPE_RESERVED             = 2     /* 2 and greater are reserved */
};

/* Values for flags above */

#define ACPI_SDEV_HANDOFF_TO_UNSECURE_OS    (1)

/*
 * SDEV subtables
 */

/* 0: Namespace Device Based Secure Device Structure */

typedef struct acpi_sdev_namespace
{
    ACPI_SDEV_HEADER        Header;
    UINT16                  DeviceIdOffset;
    UINT16                  DeviceIdLength;
    UINT16                  VendorDataOffset;
    UINT16                  VendorDataLength;

} ACPI_SDEV_NAMESPACE;

/* 1: PCIe Endpoint Device Based Device Structure */

typedef struct acpi_sdev_pcie
{
    ACPI_SDEV_HEADER        Header;
    UINT16                  Segment;
    UINT16                  StartBus;
    UINT16                  PathOffset;
    UINT16                  PathLength;
    UINT16                  VendorDataOffset;
    UINT16                  VendorDataLength;

} ACPI_SDEV_PCIE;

/* 1a: PCIe Endpoint path entry */

typedef struct acpi_sdev_pcie_path
{
    UINT8                   Device;
    UINT8                   Function;

} ACPI_SDEV_PCIE_PATH;


/* Reset to default packing */

#pragma pack()

#endif /* __ACTBL2_H__ */
